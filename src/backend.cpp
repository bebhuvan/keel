#include "backend.h"

#include "hyprland.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QVariantMap>
#include <sys/statvfs.h>
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

QVariantMap meter(const QString &label, double level, const QString &word, const QString &detail) {
    return {{"label", label}, {"level", qBound(0.0, level, 1.0)}, {"word", word}, {"detail", detail}};
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

} // namespace

Backend::Backend(QObject *parent) : QObject(parent) {
    const long page = sysconf(_SC_PAGESIZE);
    if (page > 0)
        m_pageSize = quint64(page);
    findSensors();
    connect(&m_timer, &QTimer::timeout, this, &Backend::sample);
    // Two seconds is often enough to feel live and rare enough to cost almost
    // nothing. The first rate needs two readings, so the second comes quickly.
    m_timer.setInterval(2000);
    sample();
    QTimer::singleShot(350, this, [this] {
        sample();
        m_timer.start();
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
    if (m_tick % 5 == 0 && !m_batteryPath.isEmpty())
        m_battery = vitals::parseBattery(readFile(m_batteryPath));
    const vitals::Battery &battery = m_battery;
    const double celsius = m_cpuTempPath.isEmpty() ? -1 : readNumber(m_cpuTempPath) / 1000.0;
    struct statvfs disk{};
    const bool haveDisk = statvfs("/", &disk) == 0;
    const double uptime = readFile("/proc/uptime").split(' ').value(0).toDouble();

    vitals::Reading reading;
    reading.battery = battery;
    // The headline judges the last few seconds, not one reading, so a brief
    // spike in load or heat doesn't flash a warning.
    push(m_cpuHistory, cpuPercent);
    push(m_heatHistory, celsius);
    reading.cpu = recent(m_cpuHistory);
    reading.memory = memory;
    reading.celsius = celsius > 0 ? recent(m_heatHistory) : celsius;
    reading.diskFreeBytes = haveDisk ? double(disk.f_bavail) * double(disk.f_frsize) : 0;
    reading.diskTotalBytes = haveDisk ? double(disk.f_blocks) * double(disk.f_frsize) : 0;
    reading.uptimeSeconds = uptime;
    // The calm headline belongs to the mode, which the window knows; this
    // one stands in when no mode matches.
    m_summary = vitals::summarize(reading, "all calm.", QDateTime::currentDateTime());

    // Network is read every tick, not only on the details page, so its graph
    // already has a history when the page opens.
    const vitals::Counters net = vitals::parseNetDev(readFile("/proc/net/dev"));
    const bool haveNet = seconds > 0 && m_net.in && net.in >= m_net.in && net.out >= m_net.out;
    const double down = haveNet ? double(net.in - m_net.in) / seconds : 0;
    const double up = haveNet ? double(net.out - m_net.out) / seconds : 0;
    m_net = net;
    push(m_downHistory, down);
    push(m_upHistory, up);

    const int minutes = vitals::minutesLeft(battery);
    const QString batteryWord = !battery.present               ? QString("mains")
                                : vitals::onBattery(battery)   ? vitals::durationText(minutes)
                                : battery.status == "Charging" ? QString("charging")
                                                               : QString("plugged in");
    const double usedGb = double(memory.totalKb - qMin(memory.availableKb, memory.totalKb)) / 1048576.0;
    m_meters = {
        meter("battery", battery.percent / 100.0, batteryWord, QString("%1%").arg(battery.percent)),
        meter("workload", cpuPercent / 100.0, vitals::workloadWord(cpuPercent),
              QString("%1% cpu").arg(qRound(cpuPercent))),
        meter("memory", memory.usedFraction(), vitals::memoryWord(memory.usedFraction()),
              QString("%1/%2 gb").arg(usedGb, 0, 'f', 1).arg(memory.totalKb / 1048576.0, 0, 'f', 0)),
        meter("heat", (celsius - 30) / 70.0, vitals::heatWord(celsius),
              celsius > 0 ? QString("%1°c").arg(qRound(celsius)) : QString("—")),
    };

    m_previousProcs = m_procs;
    m_procs = apps::readProcesses(m_procs);
    const QList<apps::Window> windows = apps::parseClients(hyprland::request("j/clients"));
    m_usages = apps::rank(m_previousProcs, m_procs, m_ticksElapsed, m_pageSize, windows, 3);
    m_apps.clear();
    for (const apps::Usage &usage : m_usages)
        m_apps.append(QVariantMap{
            {"name", usage.name},
            {"level", usage.cpu / 100.0},
            {"detail",
             QString("%1% · %2").arg(qRound(usage.cpu)).arg(vitals::bytesText(double(usage.memoryBytes)))},
            {"canQuit", !usage.windows.isEmpty()}});

    // The details page costs a little more to read, so it is only read while
    // it is showing.
    if (m_detailed) {
        const QList<vitals::CpuTimes> cores = vitals::parseCores(stat);
        m_cores.clear();
        for (int i = 0; i < cores.size(); ++i)
            m_cores.append(i < m_coreTimes.size() ? vitals::cpuPercent(m_coreTimes[i], cores[i]) : 0.0);
        m_coreTimes = cores;

        const vitals::Counters io = vitals::parseDiskstats(readFile("/proc/diskstats"));
        const bool haveIo = seconds > 0 && m_disk.in && io.in >= m_disk.in && io.out >= m_disk.out;
        auto ioRate = [&](quint64 now, quint64 before) {
            return haveIo ? vitals::rateText(double(now - before) / seconds) : QString("—");
        };
        const QList<QByteArray> load = readFile("/proc/loadavg").split(' ');
        const double swapUsed =
            double(memory.swapTotalKb - qMin(memory.swapFreeKb, memory.swapTotalKb)) * 1024;
        const double diskTemp = m_diskTempPath.isEmpty() ? -1 : readNumber(m_diskTempPath) / 1000.0;
        const double fan = m_fanPath.isEmpty() ? -1 : readNumber(m_fanPath);
        const double health = battery.health();

        m_sections = {
            section("processor",
                    {row("load",
                         load.size() >= 3
                             ? QString("%1 %2 %3").arg(QString(load[0]), QString(load[1]), QString(load[2]))
                             : QString("—")),
                     row("in use", QString("%1%").arg(qRound(cpuPercent))),
                     row("up for", uptimeText(uptime))}),
            section("memory",
                    {row("in use", vitals::bytesText(usedGb * 1073741824.0) + " / " +
                                       vitals::bytesText(double(memory.totalKb) * 1024)),
                     row("cache", vitals::bytesText(double(memory.cachedKb) * 1024)),
                     row("swap", memory.swapTotalKb ? vitals::bytesText(swapUsed) + " / " +
                                                          vitals::bytesText(double(memory.swapTotalKb) * 1024)
                                                    : QString("none"))}),
            section("disk",
                    {row("space", vitals::bytesText(reading.diskTotalBytes - reading.diskFreeBytes) + " / " +
                                      vitals::bytesText(reading.diskTotalBytes)),
                     row("reading", ioRate(io.in, m_disk.in)), row("writing", ioRate(io.out, m_disk.out)),
                     row("drive temp", diskTemp > 0 ? QString("%1°c").arg(qRound(diskTemp)) : "—")}),
            section("network", {row("down", haveNet ? vitals::rateText(down) : QString("—")),
                                row("up", haveNet ? vitals::rateText(up) : QString("—")),
                                row("on", m_controls.network().isEmpty() ? QString("—")
                                                                         : m_controls.network().toLower())}),
            section("battery",
                    {row("charge", battery.present ? QString("%1%").arg(battery.percent) : QString("none")),
                     row("state", battery.status.toLower()),
                     row("draw", battery.powerNow > 0 ? QString("%1 w").arg(battery.powerNow / 1e6, 0, 'f', 1)
                                                      : QString("—")),
                     row("health", health > 0 ? QString("%1%").arg(qRound(health * 100)) : QString("—")),
                     row("cycles", battery.cycles >= 0 ? QString::number(battery.cycles) : QString("—"))}),
            section("sensors", {row("cpu temp", celsius > 0 ? QString("%1°c").arg(qRound(celsius)) : "—"),
                                row("fan", fan >= 0 ? QString("%1 rpm").arg(qRound(fan)) : QString("—"))}),
        };
        m_disk = io;
        publishProcesses();
        emit detailsSampled();
    }

    m_controls.refresh();
    // The reads that start a process (do not disturb, night light, volume,
    // network name) come every fifth tick; a change made in Keel refreshes
    // them at once anyway.
    if (m_tick++ % 5 == 0)
        m_controls.refreshSlow();
    emit sampled();
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
                                         {"memory", vitals::bytesText(double(process.memoryBytes))}});
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
        // Prime the counters so the first rates appear on the next tick.
        m_coreTimes = vitals::parseCores(readFile("/proc/stat"));
        m_disk = vitals::parseDiskstats(readFile("/proc/diskstats"));
        publishProcesses();
    } else {
        m_filter.clear();
    }
    emit viewChanged();
    emit detailsSampled();
}

bool Backend::quitApp(int index) {
    if (index < 0 || index >= m_usages.size() || m_usages[index].windows.isEmpty())
        return false;
    for (const QString &address : m_usages[index].windows)
        if (!hyprland::closeWindow(address))
            return false;
    return true;
}

bool Backend::endApp(int index, bool force) {
    if (index < 0 || index >= m_usages.size())
        return false;
    bool any = false;
    for (int pid : m_usages[index].pids)
        any = apps::endProcess(pid, force) || any;
    return any;
}

bool Backend::endProcess(int pid, bool force) { return apps::endProcess(pid, force); }

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
