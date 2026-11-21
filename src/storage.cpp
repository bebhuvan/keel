#include "storage.h"

#include "vitals.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <dirent.h>
#include <fcntl.h>
#include <mutex>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <unordered_set>

namespace storage {

namespace {

void sortLargestFirst(QList<Node> &nodes) {
    std::sort(nodes.begin(), nodes.end(), [](const Node &a, const Node &b) { return a.bytes > b.bytes; });
}

bool isDots(const char *name) { return name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)); }

// Folders this close to the top become their own tasks, so every core gets
// work even when one folder (Documents, say) holds nearly everything.
// Deeper folders are walked within the task that found them.
constexpr int splitDepth = 5;

// Each level of folders being walked holds one open handle, and the walk
// recurses, so an absurdly deep tree (tens of thousands of levels, which
// only a script makes) would exhaust handles or the stack. Below this depth
// folders are counted but not entered.
constexpr int maxDepth = 512;

// Cap queued open directory descriptors so a very wide tree can never exhaust
// RLIMIT_NOFILE and silently drop folders or starve the rest of Keel.
constexpr std::size_t maxQueuedTasks = 256;

struct LinkKey {
    dev_t dev;
    ino_t ino;
    bool operator==(const LinkKey &other) const { return dev == other.dev && ino == other.ino; }
};

struct LinkKeyHash {
    std::size_t operator()(const LinkKey &k) const noexcept {
        return std::hash<dev_t>{}(k.dev) ^ (std::hash<ino_t>{}(k.ino) * 0x9e3779b97f4a7c15ull);
    }
};

// A folder near the top while the scan runs. Its subfolders that became
// tasks are kept apart from what was walked inline, and summed at the end.
struct Folder {
    QString name;
    QString path; // relative to the scanned folder
    int depth = 0;
    quint64 own = 0;
    QList<Node> kept;
    std::vector<std::unique_ptr<Folder>> folders;
};

struct Shared {
    dev_t device = 0;
    int keepDepth = 0;
    quint64 keepMin = 0;
    Progress *progress = nullptr;

    // Anything mounted inside the scanned folder: a network drive whose
    // server has gone can hang any look at it, so these are skipped by name
    // before they're touched. Keyed by the folder that holds them.
    QHash<QString, QSet<QByteArray>> mountsIn;

    bool isMount(const QString &where, const char *name) const {
        if (mountsIn.isEmpty())
            return false;
        const auto it = mountsIn.constFind(where);
        return it != mountsIn.constEnd() && it->contains(QByteArray(name));
    }

    std::mutex lock;
    std::condition_variable wake;
    std::deque<std::pair<Folder *, int>> queue;
    int busy = 0;

    // A file with several hard links is the same space however many names
    // it has, so it is counted at the first name met.
    std::mutex linksLock;
    std::unordered_set<LinkKey, LinkKeyHash> links;

    // Notes a big, long-unchanged file. The path is only built for these.
    void maybeStale(const struct stat &info, quint64 size, const QString &where, const char *name) {
        if (!progress->staleMin || size < progress->staleMin || info.st_mtime >= progress->staleBefore)
            return;
        const QString file = QString::fromLocal8Bit(name);
        std::lock_guard guard(progress->staleLock);
        progress->stale.append({where.isEmpty() ? file : where + "/" + file, size, qint64(info.st_mtime)});
    }

    bool firstSight(const struct stat &info) {
        if (info.st_nlink < 2 || S_ISDIR(info.st_mode))
            return true;
        std::lock_guard guard(linksLock);
        return links.insert({info.st_dev, info.st_ino}).second;
    }
};

// Walks a folder entirely within this thread. Plain system calls on directory
// handles (openat, fstatat): no path strings per file, no links followed.
void walkInline(int fd, int depth, const QString &where, Node &node, Shared &shared) {
    DIR *dir = fdopendir(fd);
    if (!dir) {
        close(fd);
        return;
    }
    const int handle = dirfd(dir);
    node.detailed = depth < shared.keepDepth;
    while (const dirent *entry = readdir(dir)) {
        if (shared.progress->cancel.load(std::memory_order_relaxed))
            break;
        if (isDots(entry->d_name) || shared.isMount(where, entry->d_name))
            continue;
        struct stat info{};
        if (fstatat(handle, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0 || !shared.firstSight(info))
            continue; // Vanished, unreadable, or already counted under another name.
        const quint64 size = quint64(info.st_blocks) * 512;
        node.bytes += size;
        if (S_ISDIR(info.st_mode)) {
            if (info.st_dev != shared.device || depth >= maxDepth)
                continue; // Another disk mounted inside, or too deep to enter.
            const int child = openat(handle, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (child < 0)
                continue;
            Node sub;
            sub.name = QString::fromLocal8Bit(entry->d_name);
            sub.directory = true;
            walkInline(child, depth + 1, where + "/" + sub.name, sub, shared);
            node.bytes += sub.bytes;
            if (node.detailed && sub.bytes >= shared.keepMin)
                node.children.append(std::move(sub));
        } else {
            shared.progress->files.fetch_add(1, std::memory_order_relaxed);
            shared.maybeStale(info, size, where, entry->d_name);
            if (node.detailed && size >= shared.keepMin)
                node.children.append({QString::fromLocal8Bit(entry->d_name), size, false, true, {}});
        }
    }
    closedir(dir);
    sortLargestFirst(node.children);
}

// Reads one near-the-top folder: files are counted, close subfolders are
// queued as tasks, deeper ones walked inline.
void runTask(Folder &folder, int fd, Shared &shared) {
    DIR *dir = fdopendir(fd);
    if (!dir) {
        close(fd);
        return;
    }
    const int handle = dirfd(dir);
    const bool keep = folder.depth < shared.keepDepth;
    while (const dirent *entry = readdir(dir)) {
        if (shared.progress->cancel.load(std::memory_order_relaxed))
            break;
        if (isDots(entry->d_name) || shared.isMount(folder.path, entry->d_name))
            continue;
        struct stat info{};
        if (fstatat(handle, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0 || !shared.firstSight(info))
            continue;
        const quint64 size = quint64(info.st_blocks) * 512;
        folder.own += size;
        if (!S_ISDIR(info.st_mode)) {
            shared.progress->files.fetch_add(1, std::memory_order_relaxed);
            shared.maybeStale(info, size, folder.path, entry->d_name);
            if (keep && size >= shared.keepMin)
                folder.kept.append({QString::fromLocal8Bit(entry->d_name), size, false, true, {}});
            continue;
        }
        if (info.st_dev != shared.device)
            continue;
        const int child = openat(handle, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (child < 0)
            continue;
        bool queued = false;
        if (folder.depth + 1 <= splitDepth) {
            std::unique_lock guard(shared.lock);
            if (shared.queue.size() < maxQueuedTasks) {
                auto sub = std::make_unique<Folder>();
                sub->name = QString::fromLocal8Bit(entry->d_name);
                sub->depth = folder.depth + 1;
                sub->path = folder.path.isEmpty() ? sub->name : folder.path + "/" + sub->name;
                Folder *task = sub.get();
                folder.folders.push_back(std::move(sub));
                shared.queue.emplace_back(task, child);
                guard.unlock();
                shared.wake.notify_one();
                queued = true;
            }
        }
        if (!queued) {
            Node sub;
            sub.name = QString::fromLocal8Bit(entry->d_name);
            sub.directory = true;
            walkInline(child, folder.depth + 1,
                       folder.path.isEmpty() ? sub.name : folder.path + "/" + sub.name, sub, shared);
            folder.own += sub.bytes;
            if (keep && sub.bytes >= shared.keepMin)
                folder.kept.append(std::move(sub));
        }
    }
    closedir(dir);
}

void worker(Shared &shared) {
    std::unique_lock guard(shared.lock);
    for (;;) {
        shared.wake.wait(guard, [&] { return !shared.queue.empty() || shared.busy == 0; });
        if (shared.queue.empty())
            return; // Nothing queued and nobody working: the scan is complete.
        const auto [folder, fd] = shared.queue.front();
        shared.queue.pop_front();
        ++shared.busy;
        guard.unlock();
        if (shared.progress->cancel.load(std::memory_order_relaxed))
            close(fd);
        else
            runTask(*folder, fd, shared);
        guard.lock();
        --shared.busy;
        if (shared.busy == 0 && shared.queue.empty())
            shared.wake.notify_all();
    }
}

Node assemble(Folder &folder, const Shared &shared) {
    Node node;
    node.name = folder.name;
    node.directory = true;
    node.detailed = folder.depth < shared.keepDepth;
    node.bytes = folder.own;
    node.children = std::move(folder.kept);
    for (auto &sub : folder.folders) {
        Node child = assemble(*sub, shared);
        node.bytes += child.bytes;
        if (node.detailed && child.bytes >= shared.keepMin)
            node.children.append(std::move(child));
    }
    folder.folders.clear();
    sortLargestFirst(node.children);
    return node;
}

// Mount points below `root`, from /proc/self/mountinfo (field 5, with
// spaces and the like escaped as \ooo), grouped by their parent folder.
QHash<QString, QSet<QByteArray>> mountsInside(const QByteArray &root) {
    QHash<QString, QSet<QByteArray>> mounts;
    QFile file("/proc/self/mountinfo");
    if (!file.open(QIODevice::ReadOnly))
        return mounts;
    for (const QByteArray &line : file.readAll().split('\n')) {
        const QList<QByteArray> fields = line.split(' ');
        if (fields.size() < 5)
            continue;
        QByteArray point;
        const QByteArray &raw = fields[4];
        for (qsizetype i = 0; i < raw.size(); ++i) {
            if (raw[i] == '\\' && i + 3 < raw.size()) {
                point.append(char(raw.mid(i + 1, 3).toInt(nullptr, 8)));
                i += 3;
            } else {
                point.append(raw[i]);
            }
        }
        if (!point.startsWith(root + "/"))
            continue;
        const QByteArray relative = point.mid(root.size() + 1);
        const qsizetype slash = relative.lastIndexOf('/');
        mounts[slash < 0 ? QString() : QString::fromLocal8Bit(relative.left(slash))].insert(
            relative.mid(slash + 1));
    }
    return mounts;
}

} // namespace

Node scan(const QString &path, int keepDepth, quint64 keepMinBytes, Progress &progress) {
    const int fd = open(QFile::encodeName(path).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    struct stat self{};
    if (fd < 0 || fstat(fd, &self) != 0) {
        if (fd >= 0)
            close(fd);
        Node empty;
        empty.name = QFileInfo(path).fileName();
        empty.directory = true;
        return empty;
    }
    // Up to 16 threads each hold a handle per level; the usual soft limit of
    // 1024 open files is too low for that, so raise it toward the hard limit.
    struct rlimit files{};
    if (getrlimit(RLIMIT_NOFILE, &files) == 0 && files.rlim_cur < files.rlim_max) {
        files.rlim_cur = qMin<rlim_t>(files.rlim_max, 65536);
        setrlimit(RLIMIT_NOFILE, &files);
    }

    Shared shared;
    shared.device = self.st_dev;
    const QString canonical = QFileInfo(path).canonicalFilePath();
    shared.mountsIn =
        mountsInside(QFile::encodeName(canonical.isEmpty() ? QDir::cleanPath(path) : canonical));
    shared.keepDepth = keepDepth;
    shared.keepMin = keepMinBytes;
    shared.progress = &progress;

    Folder root;
    root.name = QFileInfo(path).fileName();
    shared.queue.emplace_back(&root, fd);
    const int threads = int(qBound(1u, std::thread::hardware_concurrency(), 16u));
    std::vector<std::thread> pool;
    for (int i = 1; i < threads; ++i)
        pool.emplace_back(worker, std::ref(shared));
    worker(shared);
    for (std::thread &thread : pool)
        thread.join();
    return assemble(root, shared);
}

namespace {
void flattenInto(const Node &node, const QString &path, int depth, int maxDepth,
                 QHash<QString, quint64> &out) {
    for (const Node &child : node.children) {
        if (!child.directory)
            continue;
        const QString here = path.isEmpty() ? child.name : path + "/" + child.name;
        out.insert(here, child.bytes);
        if (depth < maxDepth)
            flattenInto(child, here, depth + 1, maxDepth, out);
    }
}
} // namespace

QHash<QString, quint64> flatten(const Node &root, int maxDepth) {
    QHash<QString, quint64> out;
    flattenInto(root, {}, 1, maxDepth, out);
    return out;
}

QList<Growth> growth(const QHash<QString, quint64> &before, const QHash<QString, quint64> &after,
                     quint64 minDelta, int limit) {
    QList<Growth> grew;
    for (auto it = after.begin(); it != after.end(); ++it) {
        const auto old = before.constFind(it.key());
        if (old != before.constEnd() && it.value() > *old && it.value() - *old >= minDelta)
            grew.append({it.key(), qint64(qMin<quint64>(it.value() - *old, quint64(INT64_MAX)))});
    }
    QList<Growth> kept;
    for (const Growth &candidate : grew) {
        const bool explainedInside = std::any_of(grew.cbegin(), grew.cend(), [&](const Growth &other) {
            return other.path.startsWith(candidate.path + "/") &&
                   double(other.delta) >= double(candidate.delta) * 0.8;
        });
        if (!explainedInside)
            kept.append(candidate);
    }
    std::sort(kept.begin(), kept.end(), [](const Growth &a, const Growth &b) { return a.delta > b.delta; });
    if (kept.size() > limit)
        kept.resize(limit);
    return kept;
}

const Node *find(const Node &root, const QStringList &names) {
    const Node *node = &root;
    for (const QString &name : names) {
        const auto it = std::find_if(node->children.cbegin(), node->children.cend(),
                                     [&](const Node &child) { return child.name == name; });
        if (it == node->children.cend())
            return nullptr;
        node = &*it;
    }
    return node;
}

QJsonObject toJson(const Node &node) {
    QJsonObject object{{"n", node.name}, {"b", double(node.bytes)}};
    if (node.directory)
        object.insert("d", true);
    if (!node.detailed)
        object.insert("x", true);
    if (!node.children.isEmpty()) {
        QJsonArray children;
        for (const Node &child : node.children)
            children.append(toJson(child));
        object.insert("c", children);
    }
    return object;
}

Node fromJson(const QJsonObject &object) {
    Node node;
    node.name = object.value("n").toString();
    node.bytes = quint64(qMax(0.0, object.value("b").toDouble()));
    node.directory = object.value("d").toBool();
    node.detailed = !object.value("x").toBool();
    for (const QJsonValue &child : object.value("c").toArray()) {
        Node sub = fromJson(child.toObject());
        if (sub.name.isEmpty() || sub.name == "." || sub.name == ".." || sub.name.contains('/'))
            continue;
        node.children.append(std::move(sub));
    }
    return node;
}

} // namespace storage

namespace {

// Folder detail is kept four levels down for anything of at least 64 MB:
// enough to find what matters, small enough to keep in memory and on disk.
constexpr int keepDepth = 4;
constexpr quint64 keepMin = 64ull << 20;

QString ago(qint64 ms) {
    const qint64 minutes = (QDateTime::currentMSecsSinceEpoch() - ms) / 60000;
    if (minutes < 1)
        return "just now";
    if (minutes < 60)
        return QString("%1 min ago").arg(minutes);
    if (minutes < 48 * 60)
        return QString("%1 h ago").arg(minutes / 60);
    return QString("%1 days ago").arg(minutes / 1440);
}

} // namespace

Storage::Storage(const QString &home, const QString &cacheFile, QObject *parent)
    : QObject(parent), m_home(home.isEmpty() ? QDir::homePath() : home),
      m_cacheFile(cacheFile.isEmpty()
                      ? QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
                            "/keel/storage.json"
                      : cacheFile) {
    QFile mounts("/proc/mounts");
    if (mounts.open(QIODevice::ReadOnly))
        m_compressed = mountCompresses(mounts.readAll(), m_home);
    m_tick.setInterval(250);
    connect(&m_tick, &QTimer::timeout, this, &Storage::changed);
}

Storage::~Storage() {
    if (m_progress) {
        std::lock_guard guard(m_progress->aliveLock);
        m_progress->alive = false;
        m_progress->cancel = true;
    }
    // A scan stuck on a hung disk must not keep Keel alive after its window
    // closes. `aliveLock` guarantees the detached thread will never invoke a
    // method on this object after destruction begins.
    if (m_worker && m_worker->joinable())
        m_worker->detach();
}

QString Storage::pathOf(const QStringList &trail) const {
    return trail.isEmpty() ? m_home : m_home + "/" + trail.join('/');
}

const storage::Node *Storage::current() const { return storage::find(m_root, m_trail); }

QString Storage::place() const { return m_trail.isEmpty() ? "~" : "~/" + m_trail.join('/'); }

bool Storage::mountCompresses(const QByteArray &mounts, const QString &path) {
    // The longest mount point that contains the path is the one holding it.
    QString best;
    bool compresses = false;
    for (const QByteArray &line : mounts.split('\n')) {
        const QList<QByteArray> fields = line.split(' ');
        if (fields.size() < 4)
            continue;
        const QString point = QString::fromUtf8(fields[1]).replace("\\040", " ");
        const bool contains = point == "/" || path == point || path.startsWith(point + "/");
        if (contains && point.size() > best.size()) {
            best = point;
            compresses = fields[3].contains("compress");
        }
    }
    return compresses;
}

void Storage::disk(double &total, double &free) const {
    struct statvfs info{};
    total = free = 0;
    if (statvfs(QFile::encodeName(m_home).constData(), &info) == 0) {
        total = double(info.f_blocks) * double(info.f_frsize);
        free = double(info.f_bavail) * double(info.f_frsize);
    }
}

double Storage::usedFraction() const {
    double total, free;
    disk(total, free);
    return total > 0 ? (total - free) / total : 0;
}

QString Storage::usedText() const {
    double total, free;
    disk(total, free);
    return vitals::bytesText(total - free) + " of " + vitals::bytesText(total) + " used";
}

QString Storage::freeText() const {
    double total, free;
    disk(total, free);
    return vitals::bytesText(free);
}

QString Storage::status() const {
    if (scanning())
        return QString("Scanning… %1 files so far").arg(QLocale().toString(m_progress->files.load()));
    if (!m_loaded)
        return QString();
    return "Scanned " + ago(m_scannedAt);
}

QVariantList Storage::entries() const {
    QVariantList list;
    const storage::Node *node = current();
    if (!node || node->bytes == 0)
        return list;
    quint64 shown = 0;
    for (const storage::Node &child : node->children) {
        shown += child.bytes;
        list.append(QVariantMap{{"name", child.name},
                                {"size", vitals::bytesText(double(child.bytes))},
                                {"fraction", double(child.bytes) / double(node->bytes)},
                                {"directory", child.directory},
                                {"other", false}});
    }
    if (node->bytes > shown && !node->children.isEmpty())
        list.append(QVariantMap{{"name", "everything smaller"},
                                {"size", vitals::bytesText(double(node->bytes - shown))},
                                {"fraction", double(node->bytes - shown) / double(node->bytes)},
                                {"directory", false},
                                {"other", true}});
    return list;
}

void Storage::open() {
    if (m_loaded || scanning())
        return;
    QFile file(m_cacheFile);
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonObject saved = QJsonDocument::fromJson(file.readAll()).object();
        if (saved.value("version").toInt() == 1 && saved.value("home").toString() == m_home) {
            m_root = storage::fromJson(saved.value("root").toObject());
            m_scannedAt = qint64(saved.value("at").toDouble());
            m_spots = saved.value("spots").toArray().toVariantList();
            m_previousAt = qint64(saved.value("previousAt").toDouble());
            const QJsonObject previous = saved.value("previous").toObject();
            for (auto it = previous.begin(); it != previous.end(); ++it)
                m_previous.insert(it.key(), quint64(qMax(0.0, it.value().toDouble())));
            for (const QJsonValue &value : saved.value("stale").toArray()) {
                const QJsonObject file = value.toObject();
                m_stale.append({file.value("p").toString(), quint64(qMax(0.0, file.value("b").toDouble())),
                                qint64(file.value("m").toDouble())});
            }
            m_loaded = true;
            emit changed();
            return;
        }
    }
    rescan();
}

void Storage::rescan() {
    m_trail.clear();
    startScan({});
}

void Storage::startScan(const QStringList &trail) {
    if (scanning())
        return;
    m_progress = std::make_shared<storage::Progress>();
    if (trail.isEmpty()) {
        // Files of a gigabyte or more, unchanged for six months.
        m_progress->staleMin = 1ull << 30;
        m_progress->staleBefore = QDateTime::currentSecsSinceEpoch() - 182 * 86400;
    }
    const QString path = pathOf(trail);
    auto progress = m_progress;
    m_worker = std::make_unique<std::thread>([this, path, trail, progress] {
        storage::Node node = storage::scan(path, keepDepth, keepMin, *progress);
        qint64 packageCacheBytes = -1;
        if (trail.isEmpty() && !progress->cancel) {
            const QString packages = "/var/cache/pacman/pkg";
            if (QFileInfo(packages).isReadable()) {
                storage::Progress pkgProgress;
                packageCacheBytes = qint64(storage::scan(packages, 0, keepMin, pkgProgress).bytes);
            }
        }
        if (progress->cancel)
            return;
        QList<storage::StaleFile> stale;
        {
            std::lock_guard guard(progress->staleLock);
            stale = progress->stale;
        }
        std::lock_guard guard(progress->aliveLock);
        if (!progress->alive || progress->cancel)
            return;
        QMetaObject::invokeMethod(
            this,
            [this, trail, node = std::move(node), stale, packageCacheBytes]() mutable {
                finished(trail, std::move(node), stale, packageCacheBytes);
            },
            Qt::QueuedConnection);
    });
    m_tick.start();
    emit changed();
}

void Storage::finished(const QStringList &trail, storage::Node node, QList<storage::StaleFile> stale,
                       qint64 packageCacheBytes) {
    m_worker->join();
    m_worker.reset();
    m_tick.stop();
    if (trail.isEmpty()) {
        // The scan being replaced becomes the one "what grew" compares with.
        if (m_loaded && m_scannedAt > 0) {
            m_previous = storage::flatten(m_root, 3);
            m_previousAt = m_scannedAt;
        }
        m_root = std::move(node);
        m_scannedAt = QDateTime::currentMSecsSinceEpoch();
        std::sort(stale.begin(), stale.end(),
                  [](const storage::StaleFile &a, const storage::StaleFile &b) { return a.bytes > b.bytes; });
        if (stale.size() > 8)
            stale.resize(8);
        m_stale = stale;
        m_packageCacheBytes = packageCacheBytes;
    } else if (storage::Node *target = const_cast<storage::Node *>(storage::find(m_root, trail))) {
        // A deeper scan of one folder: graft its detail into the tree and
        // propagate any size delta up to ancestors so parent bars stay true.
        const QString name = target->name;
        const quint64 oldBytes = target->bytes;
        const quint64 newBytes = node.bytes;
        *target = std::move(node);
        target->name = name;
        storage::Node *ancestor = &m_root;
        for (qsizetype i = 0; i < trail.size(); ++i) {
            if (newBytes >= oldBytes)
                ancestor->bytes += newBytes - oldBytes;
            else
                ancestor->bytes -= qMin(ancestor->bytes, oldBytes - newBytes);
            if (i + 1 < trail.size()) {
                auto it = std::find_if(ancestor->children.begin(), ancestor->children.end(),
                                       [&](const storage::Node &c) { return c.name == trail[i]; });
                if (it == ancestor->children.end())
                    break;
                ancestor = &*it;
            }
        }
    }
    m_loaded = true;
    buildSpots();
    save();
    emit changed();
}

// The usual places space quietly piles up, sized from the scan where they
// live in the home folder, plus the system package cache.
void Storage::buildSpots() {
    struct Spot {
        QString label, path;
    };
    const QList<Spot> places{
        {"Trash", ".local/share/Trash"}, {"App caches", ".cache"}, {"Downloads", "Downloads"}};
    m_spots.clear();
    for (const Spot &spot : places) {
        const storage::Node *node = storage::find(m_root, spot.path.split('/'));
        const QString size = node ? vitals::bytesText(double(node->bytes))
                                  : QString("under %1").arg(vitals::bytesText(double(keepMin)));
        m_spots.append(
            QVariantMap{{"label", spot.label}, {"path", m_home + "/" + spot.path}, {"size", size}});
    }
    if (m_packageCacheBytes >= 0)
        m_spots.append(QVariantMap{{"label", "Package cache"},
                                   {"path", "/var/cache/pacman/pkg"},
                                   {"size", vitals::bytesText(double(m_packageCacheBytes))}});
}

void Storage::save() const {
    QDir().mkpath(QFileInfo(m_cacheFile).absolutePath());
    QSaveFile file(m_cacheFile);
    if (!file.open(QIODevice::WriteOnly))
        return;
    QJsonObject previous;
    for (auto it = m_previous.begin(); it != m_previous.end(); ++it)
        previous.insert(it.key(), double(it.value()));
    QJsonArray staleList;
    for (const storage::StaleFile &file : m_stale)
        staleList.append(
            QJsonObject{{"p", file.path}, {"b", double(file.bytes)}, {"m", double(file.modified)}});
    const QJsonObject saved{{"version", 1},
                            {"home", m_home},
                            {"at", double(m_scannedAt)},
                            {"spots", QJsonArray::fromVariantList(m_spots)},
                            {"root", storage::toJson(m_root)},
                            {"previousAt", double(m_previousAt)},
                            {"previous", previous},
                            {"stale", staleList}};
    file.write(QJsonDocument(saved).toJson(QJsonDocument::Compact));
    file.commit();
}

void Storage::enter(int index) {
    const storage::Node *node = current();
    if (!node || index < 0 || index >= node->children.size() || !node->children[index].directory)
        return;
    if (!node->children[index].detailed && scanning())
        return;
    m_trail.append(node->children[index].name);
    // Past the kept detail, look closer with a scan of just this folder.
    if (!node->children[index].detailed)
        startScan(m_trail);
    emit changed();
}

void Storage::up() {
    if (m_trail.isEmpty())
        return;
    m_trail.removeLast();
    emit changed();
}

void Storage::reveal(int index) {
    const storage::Node *node = current();
    if (!node || index < 0 || index >= node->children.size())
        return;
    const storage::Node &child = node->children[index];
    // A file opens its folder, so the file manager shows where it lives.
    QStringList parts = m_trail;
    if (child.directory)
        parts.append(child.name);
    revealInside(parts.isEmpty() ? QString(".") : parts.join('/'));
}

QVariantList Storage::grown() const {
    QVariantList list;
    if (m_previous.isEmpty())
        return list;
    // At least 256 MB, so ordinary churn stays out of it.
    for (const storage::Growth &grew :
         storage::growth(m_previous, storage::flatten(m_root, 3), 256ull << 20, 5))
        list.append(
            QVariantMap{{"path", "~/" + grew.path}, {"size", "+" + vitals::bytesText(double(grew.delta))}});
    return list;
}

QString Storage::grownSince() const {
    return m_previousAt > 0 ? ago(m_previousAt).replace(" ago", "") : QString();
}

QVariantList Storage::stale() const {
    QVariantList list;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const storage::StaleFile &file : m_stale) {
        const qint64 months = (now - file.modified) / (30 * 86400);
        list.append(QVariantMap{
            {"path", "~/" + file.path},
            {"size", vitals::bytesText(double(file.bytes))},
            {"age", months >= 24 ? QString("%1 years").arg(months / 12) : QString("%1 months").arg(months)}});
    }
    return list;
}

void Storage::revealInside(const QString &relative) const {
    // Paths come from the saved scan file, so they're checked before use:
    // relative, no "..", a real directory, and still inside the home folder
    // after resolving symlinks.
    if (relative.isEmpty() || relative.startsWith('/') || relative.split('/').contains(".."))
        return;
    const QString canonicalHome = QFileInfo(m_home).canonicalFilePath();
    if (canonicalHome.isEmpty())
        return;
    const QFileInfo target(QDir::cleanPath(m_home + "/" + relative));
    const QString resolved = target.canonicalFilePath();
    if (resolved.isEmpty() || !target.isDir() ||
        (resolved != canonicalHome && !resolved.startsWith(canonicalHome + "/")))
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(resolved));
}

void Storage::revealGrown(int index) {
    const QVariantList list = grown();
    if (index >= 0 && index < list.size())
        revealInside(list[index].toMap().value("path").toString().mid(2));
}

void Storage::revealStale(int index) {
    if (index >= 0 && index < m_stale.size())
        revealInside(QFileInfo(m_stale[index].path).path() == "." ? QString(".")
                                                                  : QFileInfo(m_stale[index].path).path());
}

void Storage::revealSpot(int index) {
    if (index < 0 || index >= m_spots.size())
        return;
    // The saved file says which spot this is, but only these folders open.
    const QString path = m_spots[index].toMap().value("path").toString();
    const QStringList known{m_home + "/.local/share/Trash", m_home + "/.cache", m_home + "/Downloads",
                            "/var/cache/pacman/pkg"};
    if (known.contains(path))
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
