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
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <dirent.h>
#include <fcntl.h>
#include <mutex>
#include <set>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

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

// A folder near the top while the scan runs. Its subfolders that became
// tasks are kept apart from what was walked inline, and summed at the end.
struct Folder {
    QString name;
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

    std::mutex lock;
    std::condition_variable wake;
    std::deque<std::pair<Folder *, int>> queue;
    int busy = 0;

    // A file with several hard links is the same space however many names
    // it has, so it is counted at the first name met.
    std::mutex linksLock;
    std::set<std::pair<dev_t, ino_t>> links;

    bool firstSight(const struct stat &info) {
        if (info.st_nlink < 2 || S_ISDIR(info.st_mode))
            return true;
        std::lock_guard guard(linksLock);
        return links.insert({info.st_dev, info.st_ino}).second;
    }
};

// Walks a folder entirely within this thread. Plain system calls on directory
// handles (openat, fstatat): no path strings per file, no links followed.
void walkInline(int fd, int depth, Node &node, Shared &shared) {
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
        if (isDots(entry->d_name))
            continue;
        struct stat info{};
        if (fstatat(handle, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0 || !shared.firstSight(info))
            continue; // Vanished, unreadable, or already counted under another name.
        const quint64 size = quint64(info.st_blocks) * 512;
        node.bytes += size;
        if (S_ISDIR(info.st_mode)) {
            if (info.st_dev != shared.device)
                continue; // Another disk mounted inside; it isn't this disk's space.
            const int child = openat(handle, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (child < 0)
                continue;
            Node sub;
            sub.name = QString::fromLocal8Bit(entry->d_name);
            sub.directory = true;
            walkInline(child, depth + 1, sub, shared);
            node.bytes += sub.bytes;
            if (node.detailed && sub.bytes >= shared.keepMin)
                node.children.append(std::move(sub));
        } else {
            shared.progress->files.fetch_add(1, std::memory_order_relaxed);
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
        if (isDots(entry->d_name))
            continue;
        struct stat info{};
        if (fstatat(handle, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0 || !shared.firstSight(info))
            continue;
        const quint64 size = quint64(info.st_blocks) * 512;
        folder.own += size;
        if (!S_ISDIR(info.st_mode)) {
            shared.progress->files.fetch_add(1, std::memory_order_relaxed);
            if (keep && size >= shared.keepMin)
                folder.kept.append({QString::fromLocal8Bit(entry->d_name), size, false, true, {}});
            continue;
        }
        if (info.st_dev != shared.device)
            continue;
        const int child = openat(handle, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (child < 0)
            continue;
        if (folder.depth + 1 <= splitDepth) {
            auto sub = std::make_unique<Folder>();
            sub->name = QString::fromLocal8Bit(entry->d_name);
            sub->depth = folder.depth + 1;
            Folder *task = sub.get();
            folder.folders.push_back(std::move(sub));
            std::lock_guard guard(shared.lock);
            shared.queue.emplace_back(task, child);
            shared.wake.notify_one();
        } else {
            Node sub;
            sub.name = QString::fromLocal8Bit(entry->d_name);
            sub.directory = true;
            walkInline(child, folder.depth + 1, sub, shared);
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
    Shared shared;
    shared.device = self.st_dev;
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
    for (const QJsonValue &child : object.value("c").toArray())
        node.children.append(fromJson(child.toObject()));
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
    if (m_progress)
        m_progress->cancel = true;
    if (m_worker && m_worker->joinable())
        m_worker->join();
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
    return vitals::bytesText(free) + " free";
}

QString Storage::status() const {
    if (scanning())
        return QString("scanning… %1 files so far").arg(QLocale().toString(m_progress->files.load()));
    if (!m_loaded)
        return QString();
    return "scanned " + ago(m_scannedAt);
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
    m_scanTrail = trail;
    m_progress = std::make_shared<storage::Progress>();
    const QString path = pathOf(trail);
    auto progress = m_progress;
    m_worker = std::make_unique<std::thread>([this, path, trail, progress] {
        storage::Node node = storage::scan(path, keepDepth, keepMin, *progress);
        if (progress->cancel)
            return;
        QMetaObject::invokeMethod(
            this, [this, trail, node = std::move(node)]() mutable { finished(trail, std::move(node)); },
            Qt::QueuedConnection);
    });
    m_tick.start();
    emit changed();
}

void Storage::finished(const QStringList &trail, storage::Node node) {
    m_worker->join();
    m_worker.reset();
    m_tick.stop();
    if (trail.isEmpty()) {
        m_root = std::move(node);
        m_scannedAt = QDateTime::currentMSecsSinceEpoch();
    } else if (storage::Node *target = const_cast<storage::Node *>(storage::find(m_root, trail))) {
        // A deeper scan of one folder: graft its detail into the tree,
        // keeping the folder's own name and adjusting nothing else.
        const QString name = target->name;
        *target = std::move(node);
        target->name = name;
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
        QString label, path, note, action;
    };
    const QList<Spot> places{
        {"trash", ".local/share/Trash", "deleted files still taking space.", "empty"},
        {"app caches", ".cache", "apps rebuild these, so clearing one is usually safe.", "open"},
        {"downloads", "Downloads", "old installers and archives pile up here.", "open"},
    };
    m_spots.clear();
    for (const Spot &spot : places) {
        const storage::Node *node = storage::find(m_root, spot.path.split('/'));
        const QString size = node ? vitals::bytesText(double(node->bytes))
                                  : QString("under %1").arg(vitals::bytesText(double(keepMin)));
        m_spots.append(QVariantMap{{"label", spot.label},
                                   {"path", m_home + "/" + spot.path},
                                   {"size", size},
                                   {"note", spot.note},
                                   {"action", spot.action}});
    }
    const QString packages = "/var/cache/pacman/pkg";
    if (QFileInfo(packages).isReadable()) {
        storage::Progress progress;
        const storage::Node node = storage::scan(packages, 0, keepMin, progress);
        m_spots.append(QVariantMap{{"label", "package cache"},
                                   {"path", packages},
                                   {"size", vitals::bytesText(double(node.bytes))},
                                   {"note", "old package versions. 'paccache -rk1' trims it."},
                                   {"action", "open"}});
    }
}

void Storage::save() const {
    QDir().mkpath(QFileInfo(m_cacheFile).absolutePath());
    QSaveFile file(m_cacheFile);
    if (!file.open(QIODevice::WriteOnly))
        return;
    const QJsonObject saved{{"version", 1},
                            {"home", m_home},
                            {"at", double(m_scannedAt)},
                            {"spots", QJsonArray::fromVariantList(m_spots)},
                            {"root", storage::toJson(m_root)}};
    file.write(QJsonDocument(saved).toJson(QJsonDocument::Compact));
    file.commit();
}

void Storage::enter(int index) {
    const storage::Node *node = current();
    if (!node || index < 0 || index >= node->children.size() || !node->children[index].directory)
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
    const QString path = pathOf(m_trail) + (child.directory ? "/" + child.name : QString());
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void Storage::revealSpot(int index) {
    if (index >= 0 && index < m_spots.size())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_spots[index].toMap().value("path").toString()));
}

bool Storage::emptyTrash() {
    const QString trash = m_home + "/.local/share/Trash";
    bool ok = true;
    for (const char *part : {"files", "info", "expunged"}) {
        QDir dir(trash + "/" + part);
        if (dir.exists())
            ok = dir.removeRecursively() && ok;
    }
    QDir().mkpath(trash + "/files");
    QDir().mkpath(trash + "/info");

    // Take the trash's size out of every folder above it, so the page is
    // right without a rescan.
    const QStringList trail{".local", "share", "Trash"};
    if (const storage::Node *found = storage::find(m_root, trail)) {
        const quint64 freed = found->bytes;
        storage::Node *node = &m_root;
        node->bytes -= qMin(node->bytes, freed);
        for (const QString &name : trail) {
            auto it = std::find_if(node->children.begin(), node->children.end(),
                                   [&](const storage::Node &child) { return child.name == name; });
            if (it == node->children.end())
                break;
            node = &*it;
            node->bytes -= qMin(node->bytes, freed);
            if (name == "Trash")
                node->children.clear();
        }
    }
    buildSpots();
    save();
    emit changed();
    return ok;
}
