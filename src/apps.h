#pragma once

// Which apps are using the laptop the most. Processes are grouped by the
// program they run, so Chromium's dozens of helpers count as one Chromium.
// Parsing and ranking are pure; readProcesses() and the Hyprland calls do
// the reading.

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

namespace apps {

struct Process {
    QString program; // path of the executable, the grouping key
    QString comm;    // the kernel's short name, used when there is no window
    quint64 ticks = 0;
    quint64 rssPages = 0;
    quint64 start = 0; // boot-relative start time; tells a reused pid apart
};

struct Window {
    int pid = 0;
    QString address;
    QString appClass;
};

struct Usage {
    QString name;
    double cpu = 0; // percent of the whole machine
    quint64 memoryBytes = 0;
    QStringList windows; // Hyprland addresses; empty means a background program
    QList<int> pids;
    bool essential = false; // keeps the desktop running; never offered for ending
};

// Fills comm, ticks (utime + stime), start time and resident pages from
// /proc/<pid>/stat. The name sits in parentheses and may itself contain
// spaces and parentheses, so fields are counted from the last ')'.
bool parseStat(const QByteArray &stat, Process &process);

QList<Window> parseClients(const QByteArray &json);

// "com.mitchellh.ghostty" → "ghostty", "vivaldi-stable" → "vivaldi".
QString friendlyName(const QString &appClass);

// Groups the processes, charges each group the processor time it used
// between the two samples, and returns the hungriest first.
QList<Usage> rank(const QHash<int, Process> &before, const QHash<int, Process> &now,
                  quint64 totalTicksElapsed, quint64 pageSize, const QList<Window> &windows, int limit);

// One row of the details page: a single process, not a group.
struct ProcessUsage {
    int pid = 0;
    QString name;
    double cpu = 0;
    quint64 memoryBytes = 0;
    bool essential = false;
};

// Programs the desktop session stands on: ending one logs the user out or
// breaks sound, the bar or the screen. Keel never ends them, even by force.
bool isEssential(const QString &comm);

// The key processes are grouped by: the program, except that scripts run
// by an interpreter (python, node …) are told apart by their own name, so
// one Python tool is never mistaken for another.
QString groupKey(const Process &process);

// The busiest processes, or with a filter every process whose name contains
// it (any case) or whose pid starts with it.
QList<ProcessUsage> topProcesses(const QHash<int, Process> &before, const QHash<int, Process> &now,
                                 quint64 totalTicksElapsed, quint64 pageSize, bool byMemory, int limit,
                                 const QString &filter = {});

// Every process of the user's own, except Keel itself. Processes already in
// `known` keep their program path, so a steady machine costs one small read
// per process rather than three.
QHash<int, Process> readProcesses(const QHash<int, Process> &known);

// Asks one of the user's own processes to exit (SIGTERM), which lets it save
// and clean up, or with `force` stops it outright (SIGKILL) for one that
// won't listen. Returns false when it is not theirs or already gone.
bool endProcess(int pid, bool force);

} // namespace apps
