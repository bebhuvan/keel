#include "cleanup.h"

#include "storage.h"
#include "vitals.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <sys/statvfs.h>

namespace cleanup {

namespace {

quint64 parseSize(double value, const QString &unit) {
    // paccache and journalctl both use binary units (KiB/K = 1024).
    const QString u = unit.left(1).toUpper();
    const double scale = u == "K"   ? 1024.0
                         : u == "M" ? 1048576.0
                         : u == "G" ? 1073741824.0
                         : u == "T" ? 1099511627776.0
                                    : 1.0;
    return quint64(value * scale);
}

} // namespace

quint64 parsePaccache(const QByteArray &output) {
    static const QRegularExpression saved("disk space saved:\\s*([0-9.]+)\\s*([KMGT]?i?B)");
    const auto match = saved.match(QString::fromUtf8(output));
    return match.hasMatch() ? parseSize(match.captured(1).toDouble(), match.captured(2)) : 0;
}

quint64 parseJournalUsage(const QByteArray &output) {
    static const QRegularExpression usage("take up\\s*([0-9.]+)\\s*([KMGT]?)");
    const auto match = usage.match(QString::fromUtf8(output));
    return match.hasMatch() ? parseSize(match.captured(1).toDouble(), match.captured(2)) : 0;
}

bool emptyFolder(const QString &home, const QString &relative) {
    if (relative.isEmpty() || relative.startsWith('/') || relative.split('/').contains(".."))
        return false;
    const QString folder = home + "/" + relative;
    const QFileInfo info(folder);
    if (!info.exists() && !info.isSymLink())
        return true;
    const QString realHome = QFileInfo(home).canonicalFilePath();
    if (realHome.isEmpty() || info.isSymLink() || !info.isDir() ||
        info.canonicalFilePath() != realHome + "/" + relative)
        return false;
    bool ok = true;
    // removeRecursively deletes links themselves and never follows them.
    for (const QFileInfo &entry :
         QDir(folder).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
        if (entry.isDir() && !entry.isSymLink())
            ok = QDir(entry.filePath()).removeRecursively() && ok;
        else
            ok = QFile::remove(entry.filePath()) && ok;
    }
    return ok;
}

} // namespace cleanup

namespace {

// The system steps run through Keel's helper, installed with the package,
// which accepts only "clean-system packages|logs" and shows its own prompt.
const QString helperPath = "/usr/lib/keel/keel-helper";

QList<cleanup::Item> catalogue() {
    return {
        {"trash", "trash", "deleted files. once cleared they're gone for good.", 0, true, false, false},
        {"thumbnails", "thumbnails", "small previews of your files; made again when needed.", 0, true, false,
         false},
        {"aur", "aur build leftovers", "files left from installing aur packages.", 0, true, false, false},
        {"packages", "old package versions", "keeps the last two of each, so you can still go back.", 0, true,
         true, false},
        {"logs", "old system logs", "logs older than two weeks.", 0, true, true, true},
        {"dev", "developer caches", "npm, pip, uv, gradle, cargo, go. downloaded again when you next build.",
         0, false, false, false},
    };
}

} // namespace

QStringList Cleanup::folders(const QString &id) {
    if (id == "trash")
        return {".local/share/Trash/files", ".local/share/Trash/info", ".local/share/Trash/expunged"};
    if (id == "thumbnails")
        return {".cache/thumbnails"};
    if (id == "aur")
        return {".cache/yay", ".cache/paru"};
    if (id == "dev")
        return {".npm/_cacache",         ".cache/pip",     ".cache/uv", ".gradle/caches",
                ".cargo/registry/cache", ".cache/go-build"};
    return {};
}

Cleanup::Cleanup(const QString &home, QObject *parent)
    : QObject(parent), m_home(home.isEmpty() ? QDir::homePath() : home), m_items(catalogue()) {}

Cleanup::~Cleanup() {
    if (m_worker && m_worker->joinable())
        m_worker->join();
    if (m_root && m_root->state() != QProcess::NotRunning)
        m_root->waitForFinished(60000);
}

double Cleanup::freeBytes() const {
    struct statvfs info{};
    return statvfs(QFile::encodeName(m_home).constData(), &info) == 0
               ? double(info.f_bavail) * double(info.f_frsize)
               : 0;
}

QVariantList Cleanup::items() const {
    QVariantList list;
    for (const cleanup::Item &item : m_items) {
        const bool empty = item.bytes == 0;
        list.append(QVariantMap{
            {"label", item.label},
            {"note", item.note},
            {"size", empty ? QString("nothing to clear")
                           : (item.estimate ? "up to " : "") + vitals::bytesText(double(item.bytes))},
            {"selected", item.selected && !empty},
            {"empty", empty},
            {"password", item.needsPassword}});
    }
    return list;
}

QString Cleanup::total() const {
    quint64 sum = 0;
    bool estimate = false;
    for (const cleanup::Item &item : m_items)
        if (item.selected && item.bytes > 0) {
            sum += item.bytes;
            estimate = estimate || item.estimate;
        }
    return (estimate ? "up to " : "") + vitals::bytesText(double(sum));
}

void Cleanup::measure() {
    if (busy())
        return;
    QList<cleanup::Item> items = m_items;
    const QString home = m_home;
    m_worker = std::make_unique<std::thread>([this, items, home]() mutable {
        for (cleanup::Item &item : items) {
            item.bytes = 0;
            for (const QString &folder : folders(item.id)) {
                storage::Progress progress;
                item.bytes += storage::scan(home + "/" + folder, 0, 0, progress).bytes;
            }
            // A dry run needs no password; it only reports what would go.
            if (item.id == "packages") {
                for (const QStringList &arguments : {QStringList{"-dk2"}, QStringList{"-duk0"}}) {
                    QProcess paccache;
                    paccache.start("paccache", arguments);
                    if (paccache.waitForFinished(15000))
                        item.bytes += cleanup::parsePaccache(paccache.readAllStandardOutput());
                }
            } else if (item.id == "logs") {
                QProcess journal;
                journal.start("journalctl", {"--disk-usage"});
                if (journal.waitForFinished(15000))
                    item.bytes = cleanup::parseJournalUsage(journal.readAllStandardOutput());
            }
        }
        QMetaObject::invokeMethod(this, [this, items] { measured(items); }, Qt::QueuedConnection);
    });
    emit changed();
}

void Cleanup::measured(QList<cleanup::Item> items) {
    m_worker->join();
    m_worker.reset();
    m_items = items;
    emit changed();
}

void Cleanup::toggle(int index) {
    if (busy() || index < 0 || index >= m_items.size() || m_items[index].bytes == 0)
        return;
    m_items[index].selected = !m_items[index].selected;
    emit changed();
}

void Cleanup::clean() {
    if (busy())
        return;
    m_result.clear();
    m_failures.clear();
    m_freeBefore = freeBytes();

    QStringList userFolders; // relative to the home folder
    QStringList rootSteps;
    for (const cleanup::Item &item : m_items) {
        if (!item.selected || item.bytes == 0)
            continue;
        userFolders += folders(item.id);
        if (item.id == "packages" || item.id == "logs")
            rootSteps.append(item.id);
    }
    m_pendingParts = (userFolders.isEmpty() ? 0 : 1) + (rootSteps.isEmpty() ? 0 : 1);
    if (m_pendingParts == 0)
        return;

    if (!userFolders.isEmpty())
        m_worker = std::make_unique<std::thread>([this, userFolders, home = m_home] {
            QStringList failed;
            for (const QString &folder : userFolders)
                if (!cleanup::emptyFolder(home, folder))
                    failed.append(folder);
            QMetaObject::invokeMethod(this, [this, failed] { userCleaned(failed); }, Qt::QueuedConnection);
        });

    if (!rootSteps.isEmpty() && !QFileInfo::exists(helperPath)) {
        m_failures.append("system steps need keel installed (bin/install)");
        rootSteps.clear();
        if (--m_pendingParts == 0) {
            m_pendingParts = 1;
            finish();
            return;
        }
    }
    if (!rootSteps.isEmpty()) {
        // One prompt for all system steps; only the fixed lines above run.
        m_root = new QProcess(this);
        connect(m_root, &QProcess::finished, this, [this](int code) {
            // pkexec answers 126 when the password prompt is dismissed.
            if (code == 126 || code == 127)
                m_failures.append("system steps skipped: no password given");
            else if (code != 0)
                m_failures.append("some system steps didn't finish");
            m_root->deleteLater();
            m_root = nullptr;
            finish();
        });
        connect(m_root, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart)
                return;
            m_failures.append("couldn't ask for the password");
            m_root->deleteLater();
            m_root = nullptr;
            finish();
        });
        m_root->start("pkexec", QStringList{helperPath, "clean-system"} + rootSteps);
    }
    emit changed();
}

void Cleanup::userCleaned(QStringList failed) {
    m_worker->join();
    m_worker.reset();
    if (!failed.isEmpty())
        m_failures.append("couldn't clear everything in " + failed.join(", "));
    finish();
}

void Cleanup::finish() {
    if (--m_pendingParts > 0) {
        emit changed();
        return;
    }
    const double freed = qMax(0.0, freeBytes() - m_freeBefore);
    m_result = "freed " + vitals::bytesText(freed) + "." +
               (m_failures.isEmpty() ? QString() : " " + m_failures.join(". ") + ".");
    emit cleaned();
    emit changed();
    measure();
}
