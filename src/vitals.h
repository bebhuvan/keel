#pragma once

// Turning raw kernel numbers into the few words Keel shows. Everything here
// takes text or values and returns values, so it is tested without touching
// the machine; backend.cpp does the reading.

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

namespace vitals {

struct CpuTimes {
    quint64 busy = 0;
    quint64 total = 0;
};

struct Memory {
    quint64 totalKb = 0;
    quint64 availableKb = 0;
    quint64 cachedKb = 0; // page cache plus buffers: memory lent out, given back on demand
    quint64 swapTotalKb = 0;
    quint64 swapFreeKb = 0;
    double usedFraction() const;
};

struct Battery {
    bool present = false;
    int percent = 0;
    QString status; // Charging, Discharging, Full, Not charging, Unknown
    double energyNow = 0;
    double energyFull = 0;
    double energyFullDesign = 0;
    double powerNow = 0; // µW
    int cycles = -1;
    // How much of its original capacity the battery still holds, 0..1, or -1.
    double health() const;
};

// Cumulative byte counters; the backend turns two readings into a rate.
struct Counters {
    quint64 in = 0;
    quint64 out = 0;
};

// What the headline and line say, and whether anything needs the user.
struct Summary {
    QString headline;
    QString line;
    bool calm = true;
};

CpuTimes parseCpu(const QByteArray &procStat);
// One entry per core, from the cpu0, cpu1, ... lines.
QList<CpuTimes> parseCores(const QByteArray &procStat);
// Bytes read (in) and written (out) by whole disks, partitions left out so
// nothing is counted twice.
Counters parseDiskstats(const QByteArray &diskstats);
// Bytes received (in) and sent (out) by every interface except loopback.
Counters parseNetDev(const QByteArray &netDev);
QString bytesText(double bytes);
QString rateText(double bytesPerSecond);
double cpuPercent(const CpuTimes &before, const CpuTimes &after);
Memory parseMeminfo(const QByteArray &meminfo);
Battery parseBattery(const QByteArray &uevent);

// Minutes until empty while discharging, or until full while charging, or -1
// when the battery does not report a usable power draw.
int minutesLeft(const Battery &battery);
bool onBattery(const Battery &battery);

QString durationText(int minutes);
QString workloadWord(double percent);
QString memoryWord(double usedFraction);
QString heatWord(double celsius);

// Everything the headline weighs, read at one moment.
struct Reading {
    Battery battery;
    double cpu = 0; // percent
    Memory memory;
    double celsius = -1;
    double diskFreeBytes = 0;
    double diskTotalBytes = 0;
    double uptimeSeconds = 0;
};

bool diskNearlyFull(const Reading &reading);

// The headline names the one thing that needs attention, most urgent first;
// when nothing does, it is the calm headline of the current mode.
Summary summarize(const Reading &reading, const QString &calmHeadline, const QDateTime &now);

} // namespace vitals
