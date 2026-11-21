#include "backend.h"

#include "hyprland.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QVariantMap>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace {

QByteArray readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

double readNumber(const QString &path) {
    bool ok = false;
    const double value = readFile(path).trimmed().toDouble(&ok);
    return ok ? value : -1;
}

// `value` and `unit` are the figure split for setting at two sizes; `alert`
// marks the meter the window should draw in the attention colour.
QVariantMap meter(const QString &label, double level, const QString &word, const QString &value,
                  const QString &unit, bool alert) {
    return {{"label", label}, {"level", qBound(0.0, level, 1.0)},
            {"word", word},   {"value", value},
            {"unit", unit},   {"alert", alert}};
}

// [pid, start] pairs for the window to hand back when the user confirms.
// The start time travels as text: it can exceed what a JavaScript number
// holds exactly.
QVariantList targetsOf(const QList<int> &pids, const QList<quint64> &starts) {
    QVariantList list;
    for (qsizetype i = 0; i < pids.size() && i < starts.size(); ++i)
        list.append(QVariant(QVariantList{pids[i], QString::number(starts[i])}));
    return list;
}

QVariantMap row(const QString &label, const QString &value) { return {{"label", label}, {"value", value}}; }

QVariantMap section(const QString &title, const QVariantList &rows) {
    return {{"title", title}, {"rows", rows}};
}

QString uptimeText(double seconds) {
    const int minutes = int(seconds / 60);
    if (minutes >= 24 * 60)
        return QString("%1d %2h").arg(minutes / 1440).arg((minutes % 1440) / 60);
    return vitals::durationText(minutes);
}

// On Arch Linux, upgrading the kernel package replaces /usr/lib/modules/<old>,
// so the running kernel's module directory disappears until the next reboot.
bool isKernelUpdated() {
    if (!QFileInfo::exists("/usr/lib/modules"))
        return false;
    struct utsname u{};
    if (uname(&u) != 0)
        return false;
    return !QFileInfo::exists(QString("/usr/lib/modules/") + QString::fromLocal8Bit(u.release));
}

} // namespace

Backend::Backend(QObject *parent) : QObject(parent) {
    const long page = sysconf(_SC_PAGESIZE);
    if (page > 0)
        m_pageSize = quint64(page);
    findSensors();
    connect(&m_timer, &QTimer::timeout, this, &Backend::sample);
    // Two seconds is often enough to feel live and rare enough to cost almost
    // nothing. Defer the first sample until the window is created so startup
    // stays instant, then take a quick second sample for rates.
    m_timer.setInterval(2000);
    QTimer::singleShot(0, this, [this] {
        sample();
        QTimer::singleShot(350, this, [this] {
            sample();
            m_timer.start();
        });
    });
}

void Backend::findSensors() {
    for (const QFileInfo &supply :
         QDir("/sys/class/power_supply").entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (readFile(supply.filePath() + "/type").trimmed() == "Battery" && m_batteryPath.isEmpty())
            m_batteryPath = supply.filePath() + "/uevent";

    for (const QFileInfo &hwmon : QDir("/sys/class/hwmon").entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QByteArray name = readFile(hwmon.filePath() + "/name").trimmed();
        if (m_cpuTempPath.isEmpty() &&
            (name == "coretemp" || name == "k10temp" || name == "zenpower" || name == "cpu_thermal"))
            m_cpuTempPath = hwmon.filePath() + "/temp1_input";
        else if (m_diskTempPath.isEmpty() && name == "nvme")
            m_diskTempPath = hwmon.filePath() + "/temp1_input";
        if (m_fanPath.isEmpty() && QFileInfo::exists(hwmon.filePath() + "/fan1_input") &&
            readNumber(hwmon.filePath() + "/fan1_input") >= 0 && name != "acpi_fan")
            m_fanPath = hwmon.filePath() + "/fan1_input";
    }
    if (m_cpuTempPath.isEmpty() && QFileInfo::exists("/sys/class/thermal/thermal_zone0/temp"))
        m_cpuTempPath = "/sys/class/thermal/thermal_zone0/temp";
}

void Backend::sample() {
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const double seconds = m_lastSampleMs ? double(nowMs - m_lastSampleMs) / 1000.0 : 0;
    m_lastSampleMs = nowMs;

    const QByteArray stat = readFile("/proc/stat");
    const vitals::CpuTimes cpu = vitals::parseCpu(stat);
    const double cpuPercent = vitals::cpuPercent(m_cpu, cpu);
    m_ticksElapsed = cpu.total > m_cpu.total ? cpu.total - m_cpu.total : 0;
    m_cpu = cpu;

    const vitals::Memory memory = vitals::parseMeminfo(readFile("/proc/meminfo"));
    // The battery is read through the laptop's firmware, which takes
    // milliseconds, and changes slowly, so it is read every fifth tick.
    if (m_tick % 5 == 0 && !m_batteryPath.isEmpty()) {
        m_battery = vitals::parseBattery(readFile(m_batteryPath));
        if (m_battery.status != m_lastBatteryStatus) {
            m_powerHistory.clear();
            m_lastBatteryStatus = m_battery.status;
        }
        if (m_battery.powerNow > 0) {
            m_powerHistory.append(m_battery.powerNow);
            if (m_powerHistory.size() > 4)
                m_powerHistory.removeFirst();
        }
    }
    vitals::Battery battery = m_battery;
    if (!m_powerHistory.isEmpty()) {
        double sum = 0;
        for (double w : m_powerHistory)
            sum += w;
        battery.powerNow = sum / double(m_powerHistory.size());
    }
    const int chargeCap = m_controls.batteryCare() ? 80 : 100;
    const double celsius = m_cpuTempPath.isEmpty() ? -1 : readNumber(m_cpuTempPath) / 1000.0;
    // Check both / and $HOME and report whichever is tighter, so a separate
    // /home partition can never fill up unnoticed.
    auto checkDisk = [](const QByteArray &path, double &freeOut, double &totalOut) {
        struct statvfs info{};
        if (statvfs(path.constData(), &info) != 0 || info.f_blocks == 0)
            return false;
        freeOut = double(info.f_bavail) * double(info.f_frsize);
        totalOut = double(info.f_blocks) * double(info.f_frsize);
        return true;
    };
    double diskFree = 0, diskTotal = 0;
    const bool haveRoot = checkDisk("/", diskFree, diskTotal);
    double homeFree = 0, homeTotal = 0;
    if (checkDisk(QFile::encodeName(QDir::homePath()), homeFree, homeTotal)) {
        if (!haveRoot || (homeTotal > 0 && diskTotal > 0 && homeFree / homeTotal < diskFree / diskTotal)) {
            diskFree = homeFree;
            diskTotal = homeTotal;
        }
    }
    const double uptime = readFile("/proc/uptime").split(' ').value(0).toDouble();

    vitals::Reading reading;
    reading.battery = battery;
    reading.chargeCapPercent = chargeCap;
    // The headline judges the last few seconds, not one reading, so a brief
    // spike in load or heat doesn't flash a warning.
    push(m_cpuHistory, cpuPercent);
    push(m_heatHistory, celsius);
    reading.cpu = recent(m_cpuHistory);
    reading.memory = memory;
    reading.celsius = celsius > 0 ? recent(m_heatHistory) : celsius;
    reading.diskFreeBytes = diskFree;
    reading.diskTotalBytes = diskTotal;
    reading.uptimeSeconds = uptime;
    if (m_tick % 5 == 0)
        m_kernelUpdated = isKernelUpdated();
    reading.kernelUpdated = m_kernelUpdated;
    // The calm headline belongs to the mode, which the window knows; this
    // one stands in when no mode matches.
    m_summary = vitals::summarize(reading, "All calm.", QDateTime::currentDateTime());

    // Network is read every tick, not only on the details page, so its graph
    // already has a history when the page opens.
    const vitals::Counters net = vitals::parseNetDev(readFile("/proc/net/dev"));
    const bool haveNet = seconds > 0 && m_haveNetSample && net.in >= m_net.in && net.out >= m_net.out;
    const double down = haveNet ? double(net.in - m_net.in) / seconds : 0;
    const double up = haveNet ? double(net.out - m_net.out) / seconds : 0;
    m_net = net;
    m_haveNetSample = true;
    push(m_downHistory, down);
    push(m_upHistory, up);

    const int minutes = vitals::minutesLeft(battery, chargeCap);
    const QString batteryWord = !battery.present               ? QString("mains")
                                : vitals::onBattery(battery)   ? vitals::durationText(minutes)
                                : battery.status == "Charging" ? QString("charging")
                                                               : QString("plugged in");
    const double usedGb = double(memory.totalKb - qMin(memory.availableKb, memory.totalKb)) / 1048576.0;
    m_meters = {
        meter("battery", battery.present ? battery.percent / 100.0 : 1.0, batteryWord,
              battery.present ? QString::number(battery.percent) : QString(), battery.present ? "%" : "",
              vitals::onBattery(battery) && battery.percent <= 15),
        meter("workload", cpuPercent / 100.0, vitals::workloadWord(cpuPercent),
              QString::number(qRound(cpuPercent)), "%", cpuPercent >= 85),
        meter("memory", memory.usedFraction(), vitals::memoryWord(memory.usedFraction()),
              QString::number(usedGb, 'f', 1), QString("/ %1 GB").arg(memory.totalKb / 1048576.0, 0, 'f', 0),
              memory.usedFraction() >= 0.9),
        meter("heat", (celsius - 30) / 70.0, vitals::heatWord(celsius),
              celsius > 0 ? QString::number(qRound(celsius)) : QString("—"), celsius > 0 ? "°C" : "",
              celsius >= 75),
    };

    m_previousProcs = m_procs;
    m_procs = apps::readProcesses(m_procs);
    const QList<apps::Window> windows = apps::parseClients(hyprland::request("j/clients"));
    m_usages = apps::rank(m_previousProcs, m_procs, m_ticksElapsed, m_pageSize, windows, 3);
    if (m_showHeavy)
        publishHeavy(windows);
    m_apps.clear();
    for (const apps::Usage &usage : m_usages)
        m_apps.append(QVariantMap{
            {"name", usage.name},
            {"level", usage.cpu / 100.0},
            {"detail",
             QString("%1% · %2").arg(qRound(usage.cpu)).arg(vitals::bytesText(double(usage.memoryBytes)))},
            {"canQuit", !usage.windows.isEmpty() && !usage.essential},
            {"canEnd", !usage.essential},
            {"windows", usage.windows},
            {"targets", targetsOf(usage.pids, usage.starts)}});

    // The details page costs a little more to read, so it is only read while
    // it is showing.
    if (m_detailed)
        publishDetails(stat, cpuPercent, memory, reading, celsius, uptime, nowMs);

    m_controls.refresh();
    // The reads that start a process (do not disturb, night light, volume,
    // network name) come every fifth tick; a change made in Keel refreshes
    // them at once anyway.
    if (m_tick++ % 5 == 0) {
        m_controls.refreshSlow();
        checkFailedServices();
    }
    emit sampled();
}

QStringList Backend::parseFailedUnits(const QByteArray &output) {
    QStringList units;
    for (const QByteArray &line : output.split('\n')) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
        QString unit = QString::fromUtf8(trimmed.split(' ').first());
        if (unit.endsWith(QLatin1String(".service")))
            unit.chop(8);
        if (!unit.isEmpty() && !units.contains(unit))
            units.append(unit);
    }
    return units;
}

void Backend::checkFailedServices() {
    if (m_failedQueryPending)
        return;
    m_failedQueryPending = true;
    auto *process = new QProcess(this);
    auto done = [this, process](bool ok) {
        m_failedQueryPending = false;
        if (ok) {
            const QStringList units = parseFailedUnits(process->readAllStandardOutput());
            if (units != m_failedServices) {
                m_failedServices = units;
                emit sampled();
            }
        }
        process->deleteLater();
    };
    connect(process, &QProcess::finished, this, [done](int code) { done(code == 0); });
    connect(process, &QProcess::errorOccurred, this, [done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            done(false);
    });
    process->start("sh", {"-c", "systemctl --failed --no-legend --plain 2>/dev/null; "
                                "systemctl --user --failed --no-legend --plain 2>/dev/null; true"});
}

void Backend::publishHeavy(const QList<apps::Window> &windows) {
    static const double ticksPerSecond = double(sysconf(_SC_CLK_TCK));
    m_heavy.clear();
    for (const apps::Usage &usage :
         apps::rank(m_previousProcs, m_procs, m_ticksElapsed, m_pageSize, windows, 3, true, ticksPerSecond))
        m_heavy.append(
            QVariantMap{{"name", usage.name},
                        {"detail", vitals::durationText(int(usage.cpuSeconds / 60)) + " of processor"},
                        {"seconds", usage.cpuSeconds}});
}

void Backend::publishDetails(const QByteArray &stat, double cpuPercent, const vitals::Memory &memory,
                             const vitals::Reading &reading, double celsius, double uptime, qint64 nowMs) {
    const QList<vitals::CpuTimes> cores = vitals::parseCores(stat);
    m_cores.clear();
    for (int i = 0; i < cores.size(); ++i)
        m_cores.append(i < m_coreTimes.size() ? vitals::cpuPercent(m_coreTimes[i], cores[i]) : 0.0);
    m_coreTimes = cores;

    const vitals::Counters io = vitals::parseDiskstats(readFile("/proc/diskstats"));
    const double diskSeconds = m_lastDiskMs ? double(nowMs - m_lastDiskMs) / 1000.0 : 0;
    const bool haveIo = diskSeconds > 0 && m_haveDiskSample && io.in >= m_disk.in && io.out >= m_disk.out;
    auto ioRate = [&](quint64 now, quint64 before) {
        return haveIo ? vitals::rateText(double(now - before) / diskSeconds) : QString("—");
    };
    const QList<QByteArray> load = readFile("/proc/loadavg").split(' ');
    const double usedGb = double(memory.totalKb - qMin(memory.availableKb, memory.totalKb)) / 1048576.0;
    const double swapUsed = double(memory.swapTotalKb - qMin(memory.swapFreeKb, memory.swapTotalKb)) * 1024;
    const double diskTemp = m_diskTempPath.isEmpty() ? -1 : readNumber(m_diskTempPath) / 1000.0;
    const double fan = m_fanPath.isEmpty() ? -1 : readNumber(m_fanPath);
    const vitals::Battery &battery = reading.battery;
    const double health = battery.health();
    const bool haveNet = !m_downHistory.isEmpty() && m_haveNetSample;
    const double down = haveNet ? m_downHistory.last() : 0;
    const double up = haveNet ? m_upHistory.last() : 0;

    m_sections = {
        section(
            "processor",
            {row("load", load.size() >= 3
                             ? QString("%1 %2 %3").arg(QString(load[0]), QString(load[1]), QString(load[2]))
                             : QString("—")),
             row("in use", QString("%1%").arg(qRound(cpuPercent))), row("up for", uptimeText(uptime))}),
        section("memory",
                {row("in use", vitals::bytesText(usedGb * 1073741824.0) + " / " +
                                   vitals::bytesText(double(memory.totalKb) * 1024)),
                 row("cache", vitals::bytesText(double(memory.cachedKb) * 1024)),
                 row("swap", memory.swapTotalKb ? vitals::bytesText(swapUsed) + " / " +
                                                      vitals::bytesText(double(memory.swapTotalKb) * 1024)
                                                : QString("none"))}),
        section("disk", {row("space", vitals::bytesText(reading.diskTotalBytes - reading.diskFreeBytes) +
                                          " / " + vitals::bytesText(reading.diskTotalBytes)),
                         row("reading", ioRate(io.in, m_disk.in)), row("writing", ioRate(io.out, m_disk.out)),
                         row("drive temp", diskTemp > 0 ? QString("%1 °C").arg(qRound(diskTemp)) : "—")}),
        section("network", {row("down", haveNet ? vitals::rateText(down) : QString("—")),
                            row("up", haveNet ? vitals::rateText(up) : QString("—")),
                            row("on", m_controls.network().isEmpty() ? QString("—") : m_controls.network())}),
        section("battery",
                {row("charge", battery.present ? QString("%1%").arg(battery.percent) : QString("none")),
                 row("state", battery.present ? battery.status : QString("Mains")),
                 row("draw", battery.powerNow > 0 ? QString("%1 W").arg(battery.powerNow / 1e6, 0, 'f', 1)
                                                  : QString("—")),
                 row("health", health > 0 ? QString("%1%").arg(qRound(health * 100)) : QString("—")),
                 row("cycles", battery.cycles >= 0 ? QString::number(battery.cycles) : QString("—"))}),
        section("sensors", {row("CPU temp", celsius > 0 ? QString("%1 °C").arg(qRound(celsius)) : "—"),
                            row("fan", fan >= 0 ? QString("%1 rpm").arg(qRound(fan)) : QString("—"))}),
    };
    m_disk = io;
    m_lastDiskMs = nowMs;
    m_haveDiskSample = true;
    publishProcesses();
    emit detailsSampled();
}

void Backend::publishProcesses() {
    m_processRows.clear();
    // Searching shows every match; otherwise the forty busiest are plenty.
    for (const apps::ProcessUsage &process :
         apps::topProcesses(m_previousProcs, m_procs, m_ticksElapsed, m_pageSize, m_sortByMemory,
                            m_filter.isEmpty() ? 40 : 500, m_filter))
        m_processRows.append(QVariantMap{{"pid", process.pid},
                                         {"name", process.name},
                                         {"cpu", QString("%1%").arg(process.cpu, 0, 'f', 1)},
                                         {"memory", vitals::bytesText(double(process.memoryBytes))},
                                         {"essential", process.essential},
                                         {"targets", targetsOf({process.pid}, {process.start})}});
}

void Backend::setSortByMemory(bool byMemory) {
    if (byMemory == m_sortByMemory)
        return;
    m_sortByMemory = byMemory;
    publishProcesses();
    emit viewChanged();
    emit detailsSampled();
}

void Backend::setDetailed(bool detailed) {
    if (detailed == m_detailed)
        return;
    m_detailed = detailed;
    if (detailed) {
        // Populate the details page immediately instead of leaving it blank
        // until the next 2-second tick.
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const QByteArray stat = readFile("/proc/stat");
        const vitals::Memory memory = vitals::parseMeminfo(readFile("/proc/meminfo"));
        const double celsius = m_cpuTempPath.isEmpty() ? -1 : readNumber(m_cpuTempPath) / 1000.0;
        const double uptime = readFile("/proc/uptime").split(' ').value(0).toDouble();
        vitals::Reading reading;
        reading.battery = m_battery;
        struct statvfs info{};
        if (statvfs("/", &info) == 0 && info.f_blocks > 0) {
            reading.diskFreeBytes = double(info.f_bavail) * double(info.f_frsize);
            reading.diskTotalBytes = double(info.f_blocks) * double(info.f_frsize);
        }
        m_haveDiskSample = false;
        publishDetails(stat, m_cpuHistory.isEmpty() ? 0.0 : m_cpuHistory.last(), memory, reading, celsius,
                       uptime, nowMs);
    } else {
        m_filter.clear();
        emit detailsSampled();
    }
    emit viewChanged();
}

void Backend::setShowHeavy(bool show) {
    if (show == m_showHeavy)
        return;
    m_showHeavy = show;
    if (show) {
        publishHeavy(apps::parseClients(hyprland::request("j/clients")));
        emit sampled();
    }
    emit viewChanged();
}

bool Backend::closeWindows(const QStringList &addresses) {
    bool all = !addresses.isEmpty();
    for (const QString &address : addresses)
        all = hyprland::closeWindow(address) && all;
    return all;
}

bool Backend::endProcesses(const QVariantList &targets, bool force) {
    bool any = false;
    for (const QVariant &target : targets) {
        const QVariantList pair = target.toList();
        if (pair.size() == 2)
            any = apps::endProcess(pair[0].toInt(), pair[1].toString().toULongLong(), force) || any;
    }
    return any;
}

void Backend::setFilter(const QString &filter) {
    if (filter == m_filter)
        return;
    m_filter = filter;
    publishProcesses();
    emit viewChanged();
    emit detailsSampled();
}

void Backend::push(QList<double> &history, double value) {
    // Sixty readings two seconds apart: the last two minutes.
    history.append(value);
    if (history.size() > 60)
        history.removeFirst();
}

double Backend::recent(const QList<double> &history) {
    const qsizetype count = qMin<qsizetype>(3, history.size());
    double sum = 0;
    for (qsizetype i = history.size() - count; i < history.size(); ++i)
        sum += history[i];
    return count ? sum / double(count) : 0;
}

double Backend::peak(const QList<double> &history) {
    double top = 0;
    for (double value : history)
        top = qMax(top, value);
    return top;
}

QVariantList Backend::scaled(const QList<double> &history, double top) {
    QVariantList values;
    for (double value : history)
        values.append(top > 0 ? qBound(0.0, value / top, 1.0) : 0.0);
    return values;
}

QString Backend::downPeak() const { return vitals::rateText(peak(m_downHistory)); }
QString Backend::upPeak() const { return vitals::rateText(peak(m_upHistory)); }
