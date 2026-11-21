#include "apps.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace apps {

bool parseStat(const QByteArray &stat, Process &process) {
    const int open = stat.indexOf('('), close = stat.lastIndexOf(')');
    if (open < 0 || close < open)
        return false;
    // After ')' come fields 3 onward: state(3) ... utime(14) stime(15) ...
    // starttime(22) vsize(23) rss(24).
    const QList<QByteArray> fields = stat.mid(close + 2).simplified().split(' ');
    if (fields.size() < 22)
        return false;
    process.comm = QString::fromUtf8(stat.mid(open + 1, close - open - 1));
    process.ticks = fields[11].toULongLong() + fields[12].toULongLong();
    process.start = fields[19].toULongLong();
    process.rssPages = fields[21].toULongLong();
    return true;
}

QList<Window> parseClients(const QByteArray &json) {
    QList<Window> windows;
    for (const QJsonValue &value : QJsonDocument::fromJson(json).array()) {
        const QJsonObject client = value.toObject();
        const int pid = client.value("pid").toInt();
        if (pid > 0)
            windows.append({pid, client.value("address").toString(), client.value("class").toString()});
    }
    return windows;
}

QString friendlyName(const QString &appClass) {
    QString name = appClass.section('.', -1).toLower();
    for (const char *suffix : {"-stable", "-bin", "-browser", "-desktop"})
        if (name.endsWith(QLatin1String(suffix)))
            name.chop(int(qstrlen(suffix)));
    return name.isEmpty() ? appClass.toLower() : name;
}

QList<Usage> rank(const QHash<int, Process> &before, const QHash<int, Process> &now,
                  quint64 totalTicksElapsed, quint64 pageSize, const QList<Window> &windows, int limit) {
    struct Group {
        quint64 ticks = 0, pages = 0;
        QString comm;
        QString appClass;
        QStringList windows;
        QList<int> pids;
    };
    QHash<QString, Group> groups;
    for (auto it = now.begin(); it != now.end(); ++it) {
        Group &group = groups[it->program];
        const auto previous = before.constFind(it.key());
        // A process that started since the last sample has no baseline yet.
        if (previous != before.constEnd() && previous->program == it->program && it->ticks >= previous->ticks)
            group.ticks += it->ticks - previous->ticks;
        group.pages += it->rssPages;
        group.pids.append(it.key());
        if (group.comm.isEmpty())
            group.comm = it->comm;
    }
    for (const Window &window : windows) {
        const auto process = now.constFind(window.pid);
        if (process == now.constEnd())
            continue;
        Group &group = groups[process->program];
        if (group.appClass.isEmpty())
            group.appClass = window.appClass;
        group.windows.append(window.address);
    }

    QList<Usage> usages;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        Usage usage;
        usage.name = it->appClass.isEmpty() ? it->comm.toLower() : friendlyName(it->appClass);
        usage.cpu =
            totalTicksElapsed ? qMin(100.0, 100.0 * double(it->ticks) / double(totalTicksElapsed)) : 0;
        usage.memoryBytes = it->pages * pageSize;
        usage.windows = it->windows;
        usage.pids = it->pids;
        usages.append(usage);
    }
    // A background program can share its name with an app (Claude's window
    // and the claude command line tool), so say which is which.
    QSet<QString> appNames;
    for (const Usage &usage : usages)
        if (!usage.windows.isEmpty())
            appNames.insert(usage.name);
    for (Usage &usage : usages)
        if (usage.windows.isEmpty() && appNames.contains(usage.name))
            usage.name += " (background)";

    // Processor use first; memory breaks ties, which matters when the
    // machine is idle and every app reads 0%.
    std::sort(usages.begin(), usages.end(), [](const Usage &a, const Usage &b) {
        if (qAbs(a.cpu - b.cpu) >= 0.5)
            return a.cpu > b.cpu;
        return a.memoryBytes > b.memoryBytes;
    });
    if (usages.size() > limit)
        usages.resize(limit);
    return usages;
}

QList<ProcessUsage> topProcesses(const QHash<int, Process> &before, const QHash<int, Process> &now,
                                 quint64 totalTicksElapsed, quint64 pageSize, bool byMemory, int limit,
                                 const QString &filter) {
    QList<ProcessUsage> rows;
    rows.reserve(now.size());
    const QString needle = filter.trimmed();
    for (auto it = now.begin(); it != now.end(); ++it) {
        if (!needle.isEmpty() && !it->comm.contains(needle, Qt::CaseInsensitive) &&
            !QString::number(it.key()).startsWith(needle))
            continue;
        ProcessUsage row;
        row.pid = it.key();
        row.name = it->comm;
        const auto previous = before.constFind(it.key());
        if (previous != before.constEnd() && previous->program == it->program &&
            it->ticks >= previous->ticks && totalTicksElapsed)
            row.cpu = qMin(100.0, 100.0 * double(it->ticks - previous->ticks) / double(totalTicksElapsed));
        row.memoryBytes = it->rssPages * pageSize;
        rows.append(row);
    }
    auto byCpu = [](const ProcessUsage &a, const ProcessUsage &b) {
        if (qAbs(a.cpu - b.cpu) >= 0.05)
            return a.cpu > b.cpu;
        return a.memoryBytes > b.memoryBytes;
    };
    auto byRam = [](const ProcessUsage &a, const ProcessUsage &b) { return a.memoryBytes > b.memoryBytes; };
    const int keep = qMin(limit, int(rows.size()));
    if (byMemory)
        std::partial_sort(rows.begin(), rows.begin() + keep, rows.end(), byRam);
    else
        std::partial_sort(rows.begin(), rows.begin() + keep, rows.end(), byCpu);
    rows.resize(keep);
    return rows;
}

bool endProcess(int pid, bool force) {
    if (pid <= 1 || pid == int(QCoreApplication::applicationPid()))
        return false;
    if (QFileInfo("/proc/" + QString::number(pid)).ownerId() != getuid())
        return false;
    return kill(pid, force ? SIGKILL : SIGTERM) == 0;
}

// Plain system calls rather than QDir and QFile: this runs every two seconds
// over hundreds of processes, and the Qt wrappers cost several times more.
QHash<int, Process> readProcesses(const QHash<int, Process> &known) {
    QHash<int, Process> processes;
    processes.reserve(known.size() + 16);
    const uid_t uid = getuid();
    const int self = int(getpid());
    DIR *proc = opendir("/proc");
    if (!proc)
        return processes;
    char path[64];
    char buffer[1024];
    while (const dirent *entry = readdir(proc)) {
        if (entry->d_name[0] < '1' || entry->d_name[0] > '9')
            continue;
        const int pid = atoi(entry->d_name);
        if (pid == self)
            continue;
        const auto previous = known.constFind(pid);
        snprintf(path, sizeof path, "/proc/%d", pid);
        struct stat info{};
        if (previous == known.constEnd() && (stat(path, &info) != 0 || info.st_uid != uid))
            continue;
        snprintf(path, sizeof path, "/proc/%d/stat", pid);
        const int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            continue; // The process exited while we looked; that is normal.
        const ssize_t size = read(fd, buffer, sizeof buffer - 1);
        close(fd);
        Process process;
        if (size <= 0 || !parseStat(QByteArray(buffer, int(size)), process))
            continue;
        if (previous != known.constEnd() && previous->start == process.start) {
            process.program = previous->program;
        } else {
            // A new process, or a pid the kernel has handed to a new one.
            snprintf(path, sizeof path, "/proc/%d", pid);
            if (stat(path, &info) != 0 || info.st_uid != uid)
                continue;
            snprintf(path, sizeof path, "/proc/%d/exe", pid);
            char target[4096];
            const ssize_t length = readlink(path, target, sizeof target - 1);
            // Kernel threads have no executable; they are not apps.
            if (length <= 0)
                continue;
            process.program = QString::fromLocal8Bit(target, int(length));
        }
        processes.insert(pid, process);
    }
    closedir(proc);
    return processes;
}

} // namespace apps
