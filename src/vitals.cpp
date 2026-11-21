#include "vitals.h"

#include <QList>
#include <QRegularExpression>
#include <cmath>

namespace vitals {

double Memory::usedFraction() const {
    if (totalKb == 0)
        return 0;
    return double(totalKb - qMin(availableKb, totalKb)) / double(totalKb);
}

double Battery::health() const {
    if (energyFullDesign <= 0 || energyFull <= 0)
        return -1;
    return qMin(1.0, energyFull / energyFullDesign);
}

// A /proc/stat cpu line: cpu user nice system idle iowait irq softirq steal ...
// Idle and iowait are time the processor had nothing to do.
static CpuTimes parseCpuLine(const QByteArray &line) {
    CpuTimes times;
    const QList<QByteArray> fields = line.simplified().split(' ');
    for (int i = 1; i < fields.size() && i <= 8; ++i) {
        const quint64 value = fields[i].toULongLong();
        times.total += value;
        if (i != 4 && i != 5)
            times.busy += value;
    }
    return times;
}

// The first line of /proc/stat is the sum over all cores.
CpuTimes parseCpu(const QByteArray &procStat) {
    const QByteArray line = procStat.left(procStat.indexOf('\n'));
    return line.startsWith("cpu ") ? parseCpuLine(line) : CpuTimes{};
}

QList<CpuTimes> parseCores(const QByteArray &procStat) {
    QList<CpuTimes> cores;
    for (const QByteArray &line : procStat.split('\n'))
        if (line.startsWith("cpu") && line.size() > 3 && line[3] >= '0' && line[3] <= '9')
            cores.append(parseCpuLine(line));
    return cores;
}

// Fields: major minor name reads merged sectors-read ms writes merged
// sectors-written ... A sector here is always 512 bytes, whatever the disk.
Counters parseDiskstats(const QByteArray &diskstats) {
    static const QRegularExpression whole("^(nvme\\d+n\\d+|sd[a-z]+|vd[a-z]+|mmcblk\\d+)$");
    Counters counters;
    for (const QByteArray &line : diskstats.split('\n')) {
        const QList<QByteArray> fields = line.simplified().split(' ');
        if (fields.size() < 10 || !whole.match(QString::fromUtf8(fields[2])).hasMatch())
            continue;
        counters.in += fields[5].toULongLong() * 512;
        counters.out += fields[9].toULongLong() * 512;
    }
    return counters;
}

// "  wlan0: rx-bytes rx-packets ... (8 receive fields) tx-bytes ..."
Counters parseNetDev(const QByteArray &netDev) {
    Counters counters;
    for (const QByteArray &line : netDev.split('\n')) {
        const int colon = line.indexOf(':');
        if (colon < 0)
            continue;
        const QByteArray name = line.left(colon).trimmed();
        if (name == "lo")
            continue;
        const QList<QByteArray> fields = line.mid(colon + 1).simplified().split(' ');
        if (fields.size() < 9)
            continue;
        counters.in += fields[0].toULongLong();
        counters.out += fields[8].toULongLong();
    }
    return counters;
}

QString bytesText(double bytes) {
    static const char *units[] = {"b", "kb", "mb", "gb", "tb"};
    int unit = 0;
    while (bytes >= 1000 && unit < 4)
        bytes /= 1024, ++unit;
    const int decimals = unit >= 3 || bytes < 10 ? 1 : 0;
    return QString::number(bytes, 'f', unit == 0 ? 0 : decimals) + " " + units[unit];
}

QString rateText(double bytesPerSecond) { return bytesText(qMax(0.0, bytesPerSecond)) + "/s"; }

double cpuPercent(const CpuTimes &before, const CpuTimes &after) {
    if (after.total <= before.total || after.busy < before.busy)
        return 0;
    const double percent = 100.0 * double(after.busy - before.busy) / double(after.total - before.total);
    return qBound(0.0, percent, 100.0);
}

Memory parseMeminfo(const QByteArray &meminfo) {
    Memory memory;
    for (const QByteArray &line : meminfo.split('\n')) {
        const QList<QByteArray> fields = line.simplified().split(' ');
        if (fields.size() < 2)
            continue;
        if (fields[0] == "MemTotal:")
            memory.totalKb = fields[1].toULongLong();
        else if (fields[0] == "MemAvailable:")
            memory.availableKb = fields[1].toULongLong();
        else if (fields[0] == "Cached:" || fields[0] == "Buffers:")
            memory.cachedKb += fields[1].toULongLong();
        else if (fields[0] == "SwapTotal:")
            memory.swapTotalKb = fields[1].toULongLong();
        else if (fields[0] == "SwapFree:")
            memory.swapFreeKb = fields[1].toULongLong();
    }
    return memory;
}

// Batteries report either energy (µWh, µW) or charge (µAh, µA); charge times
// voltage gives energy, so both end up in the same units.
Battery parseBattery(const QByteArray &uevent) {
    Battery battery;
    double chargeNow = -1, chargeFull = -1, chargeFullDesign = -1, current = -1, voltage = 0;
    bool sawEnergy = false;
    for (const QByteArray &line : uevent.split('\n')) {
        const int eq = line.indexOf('=');
        if (eq < 0)
            continue;
        const QByteArray key = line.left(eq), value = line.mid(eq + 1).trimmed();
        if (key == "POWER_SUPPLY_PRESENT")
            battery.present = value == "1";
        else if (key == "POWER_SUPPLY_CAPACITY")
            battery.percent = qBound(0, value.toInt(), 100);
        else if (key == "POWER_SUPPLY_STATUS")
            battery.status = QString::fromUtf8(value);
        else if (key == "POWER_SUPPLY_ENERGY_NOW")
            battery.energyNow = value.toDouble(), sawEnergy = true;
        else if (key == "POWER_SUPPLY_ENERGY_FULL")
            battery.energyFull = value.toDouble();
        else if (key == "POWER_SUPPLY_ENERGY_FULL_DESIGN")
            battery.energyFullDesign = value.toDouble();
        else if (key == "POWER_SUPPLY_CHARGE_FULL_DESIGN")
            chargeFullDesign = value.toDouble();
        else if (key == "POWER_SUPPLY_CYCLE_COUNT")
            battery.cycles = value.toInt();
        else if (key == "POWER_SUPPLY_POWER_NOW")
            battery.powerNow = std::abs(value.toDouble());
        else if (key == "POWER_SUPPLY_CHARGE_NOW")
            chargeNow = value.toDouble();
        else if (key == "POWER_SUPPLY_CHARGE_FULL")
            chargeFull = value.toDouble();
        else if (key == "POWER_SUPPLY_CURRENT_NOW")
            current = std::abs(value.toDouble());
        else if (key == "POWER_SUPPLY_VOLTAGE_NOW")
            voltage = value.toDouble() / 1e6;
    }
    if (!sawEnergy && chargeNow >= 0 && voltage > 0) {
        battery.energyNow = chargeNow * voltage;
        battery.energyFull = chargeFull * voltage;
        battery.energyFullDesign = chargeFullDesign * voltage;
        battery.powerNow = current >= 0 ? current * voltage : 0;
    }
    return battery;
}

bool onBattery(const Battery &battery) { return battery.present && battery.status == "Discharging"; }

int minutesLeft(const Battery &battery) {
    if (!battery.present || battery.powerNow < 1)
        return -1;
    double hours = -1;
    if (battery.status == "Discharging")
        hours = battery.energyNow / battery.powerNow;
    else if (battery.status == "Charging" && battery.energyFull > battery.energyNow)
        hours = (battery.energyFull - battery.energyNow) / battery.powerNow;
    // A momentary spike or idle dip can give absurd estimates; past two days
    // the number means nothing, so say nothing.
    if (hours < 0 || hours > 48)
        return -1;
    return int(std::lround(hours * 60));
}

QString durationText(int minutes) {
    if (minutes < 0)
        return QStringLiteral("—");
    if (minutes < 60)
        return QStringLiteral("%1m").arg(minutes);
    return QStringLiteral("%1h %2m").arg(minutes / 60).arg(minutes % 60);
}

QString workloadWord(double percent) {
    if (percent < 25)
        return QStringLiteral("light");
    if (percent < 60)
        return QStringLiteral("moderate");
    if (percent < 85)
        return QStringLiteral("busy");
    return QStringLiteral("flat out");
}

QString memoryWord(double usedFraction) {
    if (usedFraction < 0.5)
        return QStringLiteral("roomy");
    if (usedFraction < 0.75)
        return QStringLiteral("fine");
    if (usedFraction < 0.9)
        return QStringLiteral("tight");
    return QStringLiteral("nearly full");
}

QString heatWord(double celsius) {
    if (celsius <= 0)
        return QStringLiteral("—");
    if (celsius < 60)
        return QStringLiteral("cool");
    if (celsius < 75)
        return QStringLiteral("warm");
    if (celsius < 88)
        return QStringLiteral("hot");
    return QStringLiteral("very hot");
}

static QString batteryLine(const Battery &battery, const QDateTime &now) {
    if (!battery.present)
        return QStringLiteral("on mains power.");
    const int minutes = minutesLeft(battery);
    const QString clock = minutes >= 0 ? now.addSecs(qint64(minutes) * 60).toString("HH:mm") : QString();
    if (battery.status == "Discharging")
        return minutes >= 0 ? QStringLiteral("battery lasts until %1.").arg(clock)
                            : QStringLiteral("on battery, %1% left.").arg(battery.percent);
    if (battery.status == "Charging")
        return minutes >= 0 ? QStringLiteral("charging, full by %1.").arg(clock)
                            : QStringLiteral("charging, %1% so far.").arg(battery.percent);
    return QStringLiteral("plugged in, %1% charged.").arg(battery.percent);
}

bool diskNearlyFull(const Reading &reading) {
    return reading.diskTotalBytes > 0 && (reading.diskFreeBytes < 5.0 * 1073741824 ||
                                          reading.diskFreeBytes < 0.05 * reading.diskTotalBytes);
}

Summary summarize(const Reading &reading, const QString &calmHeadline, const QDateTime &now) {
    const Battery &battery = reading.battery;
    const QString power = batteryLine(battery, now);
    if (onBattery(battery) && battery.percent <= 10) {
        const int minutes = minutesLeft(battery);
        const QString left = minutes >= 0 ? QStringLiteral("about %1 left.").arg(durationText(minutes))
                                          : QStringLiteral("%1% left.").arg(battery.percent);
        return {QStringLiteral("battery low."), left + QStringLiteral(" plug in soon."), false};
    }
    if (reading.celsius >= 88)
        return {QStringLiteral("running hot."), power + QStringLiteral(" the busiest app is below."), false};
    if (reading.memory.usedFraction() >= 0.9)
        return {QStringLiteral("memory nearly full."),
                power + QStringLiteral(" quitting an app below frees some."), false};
    if (diskNearlyFull(reading))
        return {QStringLiteral("disk nearly full."),
                power + QStringLiteral(" only %1 free.").arg(bytesText(reading.diskFreeBytes)), false};
    if (reading.cpu >= 85)
        return {QStringLiteral("working hard."), power + QStringLiteral(" the busiest app is below."), false};
    // Gentle, not a warning: a week without a restart is when updates and
    // leaks start to pile up.
    const int days = int(reading.uptimeSeconds / 86400);
    if (days >= 7)
        return {calmHeadline,
                power + QStringLiteral(" up %1 days; a restart would freshen things.").arg(days), true};
    return {calmHeadline, power + QStringLiteral(" nothing needs you."), true};
}

} // namespace vitals
