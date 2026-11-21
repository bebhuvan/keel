#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtTest>
#include <unistd.h>

#include "apps.h"
#include "controls.h"
#include "hyprland.h"
#include "storage.h"
#include "theme.h"
#include "vitals.h"

using namespace vitals;

class KeelTests : public QObject {
    Q_OBJECT

  private slots:
    void cpuPercentFromTwoReadings() {
        const CpuTimes before = parseCpu("cpu  100 0 100 800 0 0 0 0 0 0\ncpu0 1 2 3 4\n");
        const CpuTimes after = parseCpu("cpu  150 0 150 900 0 0 0 0 0 0\ncpu0 1 2 3 4\n");
        QCOMPARE(before.busy, quint64(200));
        QCOMPARE(before.total, quint64(1000));
        // 100 busy ticks out of 200 elapsed.
        QCOMPARE(cpuPercent(before, after), 50.0);
        QCOMPARE(cpuPercent(after, before), 0.0);
    }

    void coresAreReadSeparately() {
        const QList<CpuTimes> cores = parseCores("cpu  9 9 9 9\ncpu0 1 0 1 8\ncpu1 5 0 5 0\nintr 1 2\n");
        QCOMPARE(cores.size(), 2);
        QCOMPARE(cores[1].busy, quint64(10));
    }

    void memoryUsesAvailableNotFree() {
        const Memory memory = parseMeminfo("MemTotal:       16000000 kB\nMemFree:          500000 kB\n"
                                           "MemAvailable:    8000000 kB\nBuffers:  100 kB\nCached:  900 kB\n"
                                           "SwapTotal: 2000 kB\nSwapFree: 1500 kB\n");
        QCOMPARE(memory.usedFraction(), 0.5);
        QCOMPARE(memory.cachedKb, quint64(1000));
        QCOMPARE(memory.swapTotalKb - memory.swapFreeKb, quint64(500));
    }

    void batteryInEnergyUnits() {
        const Battery battery =
            parseBattery("POWER_SUPPLY_STATUS=Discharging\nPOWER_SUPPLY_PRESENT=1\n"
                         "POWER_SUPPLY_CAPACITY=50\nPOWER_SUPPLY_ENERGY_NOW=20000000\n"
                         "POWER_SUPPLY_ENERGY_FULL=40000000\n"
                         "POWER_SUPPLY_ENERGY_FULL_DESIGN=50000000\n"
                         "POWER_SUPPLY_POWER_NOW=10000000\nPOWER_SUPPLY_CYCLE_COUNT=127\n");
        QVERIFY(onBattery(battery));
        QCOMPARE(minutesLeft(battery), 120);
        QCOMPARE(battery.health(), 0.8);
        QCOMPARE(battery.cycles, 127);
    }

    void batteryInChargeUnits() {
        // 2 Ah at 10 V is 20 Wh; 1 A at 10 V is 10 W; so two hours.
        const Battery battery =
            parseBattery("POWER_SUPPLY_STATUS=Discharging\nPOWER_SUPPLY_PRESENT=1\n"
                         "POWER_SUPPLY_CHARGE_NOW=2000000\nPOWER_SUPPLY_CHARGE_FULL=4000000\n"
                         "POWER_SUPPLY_CURRENT_NOW=1000000\nPOWER_SUPPLY_VOLTAGE_NOW=10000000\n");
        QCOMPARE(minutesLeft(battery), 120);
    }

    void batteryWithoutDrawHasNoEstimate() {
        Battery battery;
        battery.present = true;
        battery.status = "Discharging";
        battery.energyNow = 1000;
        QCOMPARE(minutesLeft(battery), -1);
        battery.powerNow = 0.5e-3; // a glitch reading, not a real draw
        QCOMPARE(minutesLeft(battery), -1);
    }

    void diskCountsWholeDisksOnly() {
        const Counters disk = parseDiskstats(" 259 0 nvme0n1 10 0 100 0 20 0 200 0 0 0 0\n"
                                             " 259 1 nvme0n1p1 10 0 100 0 20 0 200 0 0 0 0\n"
                                             "   7 0 loop0 10 0 999 0 20 0 999 0 0 0 0\n");
        QCOMPARE(disk.in, quint64(100 * 512));
        QCOMPARE(disk.out, quint64(200 * 512));
    }

    void networkSkipsLoopback() {
        const Counters net = parseNetDev("Inter-|   Receive\n face |bytes\n"
                                         "    lo: 999 1 0 0 0 0 0 0 999 1 0 0 0 0 0 0\n"
                                         " wlan0: 1000 1 0 0 0 0 0 0 500 1 0 0 0 0 0 0\n");
        QCOMPARE(net.in, quint64(1000));
        QCOMPARE(net.out, quint64(500));
    }

    void words() {
        QCOMPARE(workloadWord(10), QString("light"));
        QCOMPARE(workloadWord(90), QString("flat out"));
        QCOMPARE(memoryWord(0.95), QString("nearly full"));
        QCOMPARE(heatWord(48), QString("cool"));
        QCOMPARE(heatWord(-1), QString("—"));
        QCOMPARE(durationText(220), QString("3h 40m"));
        QCOMPARE(durationText(45), QString("45m"));
        QCOMPARE(bytesText(1536 * 1024), QString("1.5 MB"));
        QCOMPARE(rateText(-5), QString("0 B/s"));
    }

    void headlineIsCalmWhenNothingNeedsTheUser() {
        Reading reading;
        reading.battery.present = true;
        reading.battery.status = "Discharging";
        reading.battery.percent = 70;
        reading.battery.energyNow = 30e6;
        reading.battery.powerNow = 10e6;
        reading.cpu = 10;
        reading.memory = {100, 80, 0, 0, 0};
        reading.celsius = 50;
        reading.diskFreeBytes = 100e9;
        reading.diskTotalBytes = 500e9;
        const QDateTime now(QDate(2026, 11, 21), QTime(16, 0));
        const Summary summary = summarize(reading, "heads down.", now);
        QVERIFY(summary.calm);
        QCOMPARE(summary.headline, QString("heads down."));
        QCOMPARE(summary.line, QString("Battery lasts until 19:00. Nothing needs you."));
    }

    void aLongUptimeGetsAGentleNudge() {
        Reading reading;
        reading.uptimeSeconds = 9 * 86400 + 5;
        const Summary summary = summarize(reading, "all calm.", QDateTime::currentDateTime());
        QVERIFY(summary.calm);
        QCOMPARE(summary.line, QString("On mains power. Up 9 days; a restart would freshen things."));
    }

    void headlineNamesTheMostUrgentProblem() {
        Reading reading;
        reading.battery.present = true;
        reading.battery.status = "Discharging";
        reading.battery.percent = 8;
        reading.cpu = 95;
        reading.memory = {100, 5, 0, 0, 0};
        reading.celsius = 95;
        reading.diskFreeBytes = 1e9;
        reading.diskTotalBytes = 500e9;
        const QDateTime now = QDateTime::currentDateTime();
        // Low battery beats heat, memory, disk and load, in that order.
        QCOMPARE(summarize(reading, "all calm.", now).headline, QString("Battery low."));
        QCOMPARE(summarize(reading, "all calm.", now).line, QString("8% left. Plug in soon."));
        reading.battery.status = "Charging";
        QCOMPARE(summarize(reading, "all calm.", now).headline, QString("Running hot."));
        reading.celsius = 50;
        QCOMPARE(summarize(reading, "all calm.", now).headline, QString("Memory nearly full."));
        reading.memory.availableKb = 80;
        QCOMPARE(summarize(reading, "all calm.", now).headline, QString("Disk nearly full."));
        reading.diskFreeBytes = 100e9;
        QCOMPARE(summarize(reading, "all calm.", now).headline, QString("Working hard."));
    }

    void diskWarnsOnlyWhenTrulyLow() {
        Reading reading;
        reading.diskTotalBytes = 475e9;
        reading.diskFreeBytes = 37e9; // 92% used, plenty left
        QVERIFY(!diskNearlyFull(reading));
        reading.diskFreeBytes = 4e9;
        QVERIFY(diskNearlyFull(reading));
    }

    void statFieldsAreCountedFromTheLastParenthesis() {
        apps::Process process;
        const QByteArray stat =
            "123 (Web (Content) x) S 1 1 1 0 -1 0 0 0 0 0 70 30 0 0 20 0 1 0 100 1000 250 0";
        QVERIFY(apps::parseStat(stat, process));
        QCOMPARE(process.comm, QString("Web (Content) x"));
        QCOMPARE(process.ticks, quint64(100));
        QCOMPARE(process.start, quint64(100));
        QCOMPARE(process.rssPages, quint64(250));
        QVERIFY(!apps::parseStat("garbage", process));
    }

    void appsAreGroupedAndRanked() {
        QHash<int, apps::Process> before{{1, {"/usr/lib/chromium/chromium", "chromium", 100, 10}},
                                         {2, {"/usr/lib/chromium/chromium", "chromium", 100, 10}},
                                         {3, {"/usr/bin/python", "python", 0, 5}},
                                         {4, {"/usr/bin/claude", "claude", 0, 100}}};
        QHash<int, apps::Process> now = before;
        now[1].ticks = 130;
        now[2].ticks = 120;
        now[3].ticks = 10;
        now.insert(5, {"/usr/bin/new", "new", 999, 1}); // no baseline: charged nothing
        const QList<apps::Window> windows{{1, "0xabc", "chromium"}, {9, "0xdef", "gone"}};

        const QList<apps::Usage> ranked = apps::rank(before, now, 100, 4096, windows, 3);
        QCOMPARE(ranked.size(), 3);
        QCOMPARE(ranked[0].name, QString("chromium"));
        QCOMPARE(ranked[0].cpu, 50.0);
        QCOMPARE(ranked[0].memoryBytes, quint64(20 * 4096));
        QCOMPARE(ranked[0].windows, QStringList{"0xabc"});
        QCOMPARE(ranked[1].name, QString("python"));
        QVERIFY(ranked[1].windows.isEmpty());
        // At equal processor use, memory decides.
        QCOMPARE(ranked[2].name, QString("claude"));
    }

    void backgroundNamesThatClashWithAnAppAreLabelled() {
        QHash<int, apps::Process> procs{{1, {"/opt/Claude/claude-desktop", "claude-desktop", 0, 10}},
                                        {2, {"/usr/bin/claude", "claude", 0, 5}}};
        const QList<apps::Usage> ranked =
            apps::rank(procs, procs, 100, 4096, {{1, "0x1", "com.anthropic.Claude"}}, 3);
        QCOMPARE(ranked[0].name, QString("claude"));
        QCOMPARE(ranked[1].name, QString("claude (background)"));
    }

    void topProcessesSortByEitherColumn() {
        QHash<int, apps::Process> before{{1, {"/a", "a", 0, 100}}, {2, {"/b", "b", 0, 5}}};
        QHash<int, apps::Process> now = before;
        now[2].ticks = 50;
        QCOMPARE(apps::topProcesses(before, now, 100, 4096, false, 10).first().pid, 2);
        QCOMPARE(apps::topProcesses(before, now, 100, 4096, true, 10).first().pid, 1);
        QCOMPARE(apps::topProcesses(before, now, 100, 4096, true, 1).size(), 1);
    }

    void searchMatchesNamesAndPids() {
        QHash<int, apps::Process> procs{{4242, {"/a", "Chromium", 0, 1}}, {77, {"/b", "python", 0, 1}}};
        QCOMPARE(apps::topProcesses(procs, procs, 100, 4096, false, 10, "chrom").first().pid, 4242);
        QCOMPARE(apps::topProcesses(procs, procs, 100, 4096, false, 10, "77").first().pid, 77);
        QVERIFY(apps::topProcesses(procs, procs, 100, 4096, false, 10, "nothing").isEmpty());
    }

    void appGroupsKnowTheirProcesses() {
        QHash<int, apps::Process> procs{{1, {"/x", "x", 0, 1}}, {2, {"/x", "x", 0, 1}}};
        const auto ranked = apps::rank(procs, procs, 100, 4096, {}, 3);
        QCOMPARE(ranked.size(), 1);
        QCOMPARE(ranked[0].pids.size(), 2);
    }

    void endingRefusesKeelAndInit() {
        QVERIFY(!apps::endProcess(1, 0, false));
        QVERIFY(!apps::endProcess(1, 0, true));
        QVERIFY(!apps::endProcess(int(QCoreApplication::applicationPid()), 0, true));
    }

    void endingChecksItIsStillTheSameProcess() {
        QProcess sleeper;
        sleeper.start("sleep", {"30"});
        QVERIFY(sleeper.waitForStarted());
        const int pid = int(sleeper.processId());
        QFile stat("/proc/" + QString::number(pid) + "/stat");
        QVERIFY(stat.open(QIODevice::ReadOnly));
        apps::Process process;
        QVERIFY(apps::parseStat(stat.readAll(), process));
        // A different start time means the pid was reused: nothing is sent.
        QVERIFY(!apps::endProcess(pid, process.start + 1, false));
        QCOMPARE(sleeper.state(), QProcess::Running);
        QVERIFY(apps::endProcess(pid, process.start, false));
        QVERIFY(sleeper.waitForFinished(3000));
    }

    void theSessionIsNeverOfferedForEnding() {
        QVERIFY(apps::isEssential("uwsm"));
        QVERIFY(apps::isEssential("Hyprland"));
        QVERIFY(apps::isEssential("systemd-logind"));
        QVERIFY(apps::isEssential("pipewire"));
        QVERIFY(apps::isEssential("start-hyprland"));
        QVERIFY(apps::isEssential("(sd-pam)"));
        QVERIFY(apps::isEssential("gvfsd-fuse"));
        QVERIFY(apps::isEssential("omarchy-shell"));
        QVERIFY(!apps::isEssential("chromium"));
        QVERIFY(!apps::isEssential("python"));

        // uwsm is a Python script: it must not share a group with other
        // Python tools, or ending "python" would end the session too.
        QHash<int, apps::Process> procs{{10, {"/usr/bin/python3.14", "uwsm", 0, 50}},
                                        {11, {"/usr/bin/python3.14", "python", 50, 40}}};
        QHash<int, apps::Process> before = procs;
        before[11].ticks = 0;
        const auto ranked = apps::rank(before, procs, 100, 4096, {}, 3);
        QCOMPARE(ranked.size(), 2);
        QCOMPARE(ranked[0].name, QString("python"));
        QCOMPARE(ranked[0].pids, QList<int>{11});
        QVERIFY(!ranked[0].essential);
        QCOMPARE(ranked[1].name, QString("uwsm"));
        QVERIFY(ranked[1].essential);
        QVERIFY(apps::topProcesses(procs, procs, 100, 4096, true, 5).first().essential);
    }

    void windowClassesBecomeFriendlyNames() {
        QCOMPARE(apps::friendlyName("com.mitchellh.ghostty"), QString("ghostty"));
        QCOMPARE(apps::friendlyName("vivaldi-stable"), QString("vivaldi"));
        QCOMPARE(apps::friendlyName("Chromium"), QString("chromium"));
    }

    void clientsAreParsed() {
        const auto windows =
            apps::parseClients(R"([{"address":"0x1","class":"a","pid":10},{"address":"0x2","pid":-1}])");
        QCOMPARE(windows.size(), 1);
        QCOMPARE(windows[0].pid, 10);
    }

    void modesMatchTheirSwitches() {
        for (const QString &mode : Controls::modes())
            QCOMPARE(Controls::matchMode(Controls::modeStates(mode)), mode);
        QHash<QString, bool> states = Controls::modeStates("focus");
        states["night"] = true;
        QCOMPARE(Controls::matchMode(states), QString("custom"));
        // Travel never cuts Wi-Fi.
        QVERIFY(Controls::modeStates("travel").value("wifi"));
    }

    void displayStateFollowsHyprland() {
        QString name;
        const QByteArray laptopOnly = R"([{"name":"eDP-1","disabled":false,"mirrorOf":"none"}])";
        QCOMPARE(Controls::parseDisplay(laptopOnly, name), QString("laptop"));
        const QByteArray monitorOnly =
            R"([{"name":"eDP-1","disabled":true,"mirrorOf":"none"},
                {"name":"HDMI-A-1","disabled":false,"mirrorOf":"none",
                 "description":"Acer Technologies VG271U M3 2326176234207"}])";
        QCOMPARE(Controls::parseDisplay(monitorOnly, name), QString("monitor"));
        QCOMPARE(name, QString("Acer VG271U M3"));
        const QByteArray both = R"([{"name":"eDP-1","disabled":false},{"name":"DP-2","disabled":false}])";
        QCOMPARE(Controls::parseDisplay(both, name), QString("both"));
        const QByteArray mirror = R"([{"name":"eDP-1","disabled":false},{"name":"DP-2","mirrorOf":"eDP-1"}])";
        QCOMPARE(Controls::parseDisplay(mirror, name), QString("mirror"));
        // Hyprland's stand-in output is not a monitor; a desktop with no
        // internal panel hides laptop screen switching.
        QCOMPARE(Controls::parseDisplay(R"([{"name":"FALLBACK","disabled":false}])", name),
                 QString("desktop"));
        QCOMPARE(Controls::parseDisplay(R"([{"name":"DP-1","disabled":false}])", name), QString("desktop"));
    }

    void wifiNameIsReadFromNmcli() {
        QCOMPARE(
            Controls::parseNetwork("ethernet:unavailable:\nwifi:connected:Home\\:Net 5G\nlo:connected:lo\n"),
            QString("Home:Net 5G"));
        QVERIFY(Controls::parseNetwork("wifi:disconnected:\n").isEmpty());
    }

    void volumeIsReadFromWpctl() {
        double volume = 0;
        bool muted = true;
        QVERIFY(Controls::parseVolume("Volume: 0.40\n", volume, muted));
        QCOMPARE(volume, 0.40);
        QVERIFY(!muted);
        QVERIFY(Controls::parseVolume("Volume: 1.20 [MUTED]", volume, muted));
        QCOMPARE(volume, 1.0);
        QVERIFY(muted);
        QVERIFY(!Controls::parseVolume("error", volume, muted));
    }

    void batteryChargingRespectsCareCap() {
        Battery battery;
        battery.present = true;
        battery.status = "Charging";
        battery.percent = 60;
        battery.energyNow = 24e6;
        battery.energyFull = 40e6;
        battery.powerNow = 8e6; // 8 W: 16 Wh to 100% (120m), 8 Wh to 80% (60m)
        QCOMPARE(minutesLeft(battery, 100), 120);
        QCOMPARE(minutesLeft(battery, 80), 60);

        Reading reading;
        reading.battery = battery;
        reading.chargeCapPercent = 80;
        const QDateTime now(QDate(2026, 11, 21), QTime(16, 0));
        QCOMPARE(summarize(reading, "all calm.", now).line,
                 QString("Charging to 80% by 17:00. Nothing needs you."));
    }

    void deletedExecutablesStillSplitInterpretersAndReusedPidsDoNotInflateCpu() {
        QHash<int, apps::Process> before{{10, {"/usr/bin/python3.14 (deleted)", "uwsm", 100, 10, 1000}},
                                         {11, {"/usr/bin/python3.14 (deleted)", "worker", 100, 10, 1001}}};
        QHash<int, apps::Process> now = before;
        now[11].ticks = 150;
        // Same PID and program, different start time -> reused PID, must not charge delta.
        now[10].start = 2000;
        now[10].ticks = 190;
        const auto ranked = apps::rank(before, now, 100, 4096, {}, 5);
        QCOMPARE(ranked.size(), 2);
        QCOMPARE(ranked[0].name, QString("worker"));
        QCOMPARE(ranked[0].cpu, 50.0);
        QCOMPARE(ranked[1].name, QString("uwsm"));
        QCOMPARE(ranked[1].cpu, 0.0);
        QVERIFY(ranked[1].essential);
    }

    void onlyHyprlandAddressesReachTheLuaCommand() {
        QVERIFY(hyprland::isAddress("0x557024b7b040"));
        QVERIFY(!hyprland::isAddress("0x1\n"));
        QVERIFY(!hyprland::isAddress("0x1\" }) os.execute(\"rm\""));
        QVERIFY(!hyprland::isAddress(""));
        QVERIFY(!hyprland::closeWindow("not an address"));
    }

    void kernelUpdateGetsAGentleNudge() {
        Reading reading;
        reading.kernelUpdated = true;
        const Summary summary = summarize(reading, "All calm.", QDateTime::currentDateTime());
        QVERIFY(summary.calm);
        QCOMPARE(summary.line, QString("On mains power. Kernel updated; restart when you can."));
    }

    void brightnessIsReadFromOmarchyDisplay() {
        double level = -1;
        QVERIFY(Controls::parseBrightness("75\n", level));
        QCOMPARE(level, 0.75);
        QVERIFY(Controls::parseBrightness("0", level));
        QCOMPARE(level, 0.0);
        QVERIFY(!Controls::parseBrightness("105", level));
        QVERIFY(!Controls::parseBrightness("error", level));
    }

    void audioOutputIsReadFromWpctlInspect() {
        const QByteArray inspect = "  * node.description = \"Meteor Lake-P HD Audio Controller Speaker\"\n"
                                   "  * node.nick = \"Speaker\"\n";
        QCOMPARE(Controls::parseAudioOutput(inspect), QString("Speaker"));
        QCOMPARE(Controls::parseAudioOutput("  * node.description = \"WH-1000XM4\"\n"),
                 QString("WH-1000XM4"));
        QVERIFY(Controls::parseAudioOutput("").isEmpty());
    }

    void displayChoicesExcludeMirror() {
        QCOMPARE(Controls::displayChoices(), QStringList({"both", "monitor"}));
    }

    void storageScanSizesFoldersAndKeepsOnlyBigOnes() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("big/inner");
        QDir(dir.path()).mkpath("small");
        writeBytes(dir.path() + "/big/inner/a.bin", 300 * 1024);
        writeBytes(dir.path() + "/big/b.bin", 200 * 1024);
        writeBytes(dir.path() + "/small/c.bin", 4 * 1024);
        // A link is never followed, so this doesn't count the big folder twice.
        QVERIFY(QFile::link(dir.path() + "/big", dir.path() + "/shortcut"));

        storage::Progress progress;
        const storage::Node root = storage::scan(dir.path(), 2, 100 * 1024, progress);
        QCOMPARE(progress.files.load(), quint64(4)); // three files and the link itself
        QVERIFY(root.bytes >= 504 * 1024);
        QVERIFY(root.bytes < 700 * 1024);
        QCOMPARE(root.children.first().name, QString("big"));
        // "small" is under the keep threshold, so it's counted but not listed.
        QVERIFY(!storage::find(root, {"small"}));
        const storage::Node *inner = storage::find(root, {"big", "inner"});
        QVERIFY(inner);
        // Depth 2 is as far as detail goes; going in starts a fresh scan.
        QVERIFY(!inner->detailed);
        QVERIFY(inner->children.isEmpty());
    }

    void storageCountsHardLinksOnce() {
        QTemporaryDir dir;
        writeBytes(dir.path() + "/one.bin", 512 * 1024);
        QVERIFY(::link(QFile::encodeName(dir.path() + "/one.bin").constData(),
                       QFile::encodeName(dir.path() + "/two.bin").constData()) == 0);
        storage::Progress progress;
        const storage::Node root = storage::scan(dir.path(), 1, 0, progress);
        QVERIFY(root.bytes >= 512 * 1024);
        QVERIFY(root.bytes < 1024 * 1024);
    }

    void storageResultSurvivesSaving() {
        storage::Node root{
            "home", 1000, true, true, {{"Documents", 900, true, false, {}}, {"a.iso", 100, false, true, {}}}};
        const storage::Node back = storage::fromJson(storage::toJson(root));
        QCOMPARE(back.bytes, quint64(1000));
        QCOMPARE(back.children.size(), 2);
        QCOMPARE(back.children[0].name, QString("Documents"));
        QVERIFY(back.children[0].directory);
        QVERIFY(!back.children[0].detailed);
        QVERIFY(!back.children[1].directory);
    }

    void storageKnowsWhenTheDiskCompresses() {
        const QByteArray mounts = "/dev/root / btrfs rw,compress=zstd:3,subvol=/@ 0 0\n"
                                  "/dev/root /home btrfs rw,compress=zstd:3,subvol=/@home 0 0\n"
                                  "/dev/sdb1 /home/me/Media ext4 rw,relatime 0 0\n";
        QVERIFY(Storage::mountCompresses(mounts, "/home/me"));
        QVERIFY(!Storage::mountCompresses(mounts, "/home/me/Media"));
        QVERIFY(!Storage::mountCompresses("/dev/x / ext4 rw 0 0\n", "/home/me"));
    }

    void storagePageLoadsSavedResultsInstantly() {
        QTemporaryDir home, cache;
        QDir(home.path()).mkpath(".local/share/Trash/files");
        writeBytes(home.path() + "/.local/share/Trash/files/old.bin", 64 * 1024);
        const QString saved = cache.path() + "/storage.json";
        {
            Storage first(home.path(), saved);
            first.open(); // nothing saved yet, so it scans
            QTRY_VERIFY(!first.scanning());
            QVERIFY(QFile::exists(saved));
            QCOMPARE(first.spots().first().toMap().value("label").toString(), QString("Trash"));
        }
        Storage second(home.path(), saved);
        second.open();
        QVERIFY(!second.scanning()); // loaded, not scanned
        QVERIFY(second.status().startsWith("Scanned"));
    }

    void growthNamesWhereTheSpaceWent() {
        const QHash<QString, quint64> before{
            {"Documents", 100}, {"Documents/Work", 50}, {"Videos", 10}, {".cache", 5}};
        const QHash<QString, quint64> after{
            {"Documents", 400}, {"Documents/Work", 340}, {"Videos", 200}, {".cache", 6}};
        const auto grew = storage::growth(before, after, 100, 5);
        // Work explains nearly all of Documents' growth, so Documents itself
        // is left out; the cache's tiny change is under the threshold.
        QCOMPARE(grew.size(), 2);
        QCOMPARE(grew[0].path, QString("Documents/Work"));
        QCOMPARE(grew[0].delta, qint64(290));
        QCOMPARE(grew[1].path, QString("Videos"));
    }

    void flattenListsFoldersOnly() {
        const storage::Node root{
            "home",
            10,
            true,
            true,
            {{"A", 6, true, true, {{"B", 4, true, true, {}}, {"f.iso", 2, false, true, {}}}},
             {"g.iso", 4, false, true, {}}}};
        const auto flat = storage::flatten(root, 3);
        QCOMPARE(flat.size(), 2);
        QCOMPARE(flat.value("A/B"), quint64(4));
    }

    void scanFindsBigUntouchedFiles() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("old");
        writeBytes(dir.path() + "/old/archive.bin", 200 * 1024);
        writeBytes(dir.path() + "/fresh.bin", 200 * 1024);
        QFile old(dir.path() + "/old/archive.bin");
        QVERIFY(old.open(QIODevice::ReadWrite));
        QVERIFY(
            old.setFileTime(QDateTime::currentDateTime().addYears(-2), QFileDevice::FileModificationTime));
        old.close();
        storage::Progress progress;
        progress.staleMin = 100 * 1024;
        progress.staleBefore = QDateTime::currentSecsSinceEpoch() - 182 * 86400;
        storage::scan(dir.path(), 2, 0, progress);
        QCOMPARE(progress.stale.size(), 1);
        QCOMPARE(progress.stale.first().path, QString("old/archive.bin"));
    }

    void heaviestSinceOpenedRanksByLifetime() {
        QHash<int, apps::Process> procs{{1, {"/a", "a", 9000, 1}}, {2, {"/b", "b", 100, 1}}};
        QHash<int, apps::Process> before = procs;
        before[2].ticks = 0; // b is busy right now, a has done more overall
        QCOMPARE(apps::rank(before, procs, 100, 4096, {}, 2).first().name, QString("b"));
        const auto heavy = apps::rank(before, procs, 100, 4096, {}, 2, true, 100);
        QCOMPARE(heavy.first().name, QString("a"));
        QCOMPARE(heavy.first().cpuSeconds, 90.0);
    }

    void deviceBatteriesReadNicely() {
        QCOMPARE(Controls::formatDevices({{"WH-1000XM4", 80}, {"MX Master 3", 55}}),
                 QString("WH-1000XM4 80%  ·  MX Master 3 55%"));
        QVERIFY(Controls::formatDevices({}).isEmpty());
    }

    void theHelperRefusesAnythingButItsFixedSteps() {
        // Only the refusals are tried: each exits before doing anything, and
        // none of this runs as root.
        const QString helper = QFINDTESTDATA("../pkgbuild/keel-helper");
        QVERIFY(!helper.isEmpty());
        for (const QStringList &arguments : QList<QStringList>{{},
                                                               {"rm", "-rf", "/"},
                                                               {"clean-system"},
                                                               {"clean-system", "packages", "; rm -rf /"},
                                                               {"battery-care"},
                                                               {"battery-care", "maybe"},
                                                               {"battery-care", "on", "extra"}}) {
            QProcess run;
            run.start("sh", QStringList{helper} + arguments);
            QVERIFY(run.waitForFinished(5000));
            QCOMPARE(run.exitCode(), 2);
        }
    }

    void stayAwakeIsReadFromOmarchysFlag() {
        QTemporaryDir dir;
        Controls controls(dir.path());
        controls.refresh();
        QCOMPARE(controls.states().value("awake").toBool(), false);
        QFile flag(dir.path() + "/stay-awake");
        QVERIFY(flag.open(QIODevice::WriteOnly));
        flag.close();
        controls.refresh();
        QCOMPARE(controls.states().value("awake").toBool(), true);
    }

    void themeFallsBackWhenOmarchyIsMissing() {
        QTemporaryDir dir;
        Theme theme(dir.path(), nullptr, false);
        QCOMPARE(theme.accent(), QString("#2f6bed"));
        QVERIFY(theme.dark());
        QCOMPARE(theme.background(), QString("#0f0f10"));
    }

    void themeReadsAccentAndLightMode() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QFile colors(dir.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("mode = \"light\"\n\naccent = \"#1e66f5\"\nbackground = \"#eff1f5\"\n");
        colors.close();

        Theme theme(dir.path(), nullptr, false);
        QCOMPARE(theme.accent(), QString("#1e66f5"));
        QVERIFY(!theme.dark());
        // Keel's own white, never the theme's tinted background.
        QCOMPARE(theme.background(), QString("#ffffff"));
        QCOMPARE(theme.onAccent(), QString("#ffffff"));
        theme.flip();
        QVERIFY(theme.dark());
    }

    void storageFromJsonDropsTraversalNames() {
        const QJsonObject bad{{"n", "home"},
                              {"b", 100.0},
                              {"d", true},
                              {"c", QJsonArray{QJsonObject{{"n", "../../etc"}, {"b", 50.0}, {"d", true}},
                                               QJsonObject{{"n", "a/b"}, {"b", 20.0}, {"d", true}},
                                               QJsonObject{{"n", "Docs"}, {"b", 30.0}, {"d", true}}}}};
        const storage::Node node = storage::fromJson(bad);
        QCOMPARE(node.children.size(), 1);
        QCOMPARE(node.children.first().name, QString("Docs"));
    }

    void storageDestructionMidScanIsSafe() {
        QTemporaryDir home, cache;
        for (int i = 0; i < 40; ++i) {
            QDir(home.path()).mkpath(QString("d%1").arg(i));
            writeBytes(home.path() + QString("/d%1/f.bin").arg(i), 4096);
        }
        {
            Storage s(home.path(), cache.path() + "/s.json");
            s.rescan();
        }
        QTest::qWait(50);
    }

    void themeIgnoresAnInvalidAccent() {
        QCOMPARE(Theme::readKey("accent = \"#123456\"\n", "accent"), QString("#123456"));
        QCOMPARE(Theme::readKey("mode = 'dark'\n", "mode"), QString("dark"));
        QVERIFY(Theme::readKey("other = \"x\"\n", "accent").isEmpty());

        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        for (const QByteArray &bad : QList<QByteArray>{"red", "#zzz", "rgb(1,2,3)", "' onclick='x"}) {
            QFile colors(dir.path() + "/theme/colors.toml");
            QVERIFY(colors.open(QIODevice::WriteOnly | QIODevice::Truncate));
            colors.write("accent = \"" + bad + "\"\n");
            colors.close();
            Theme theme(dir.path(), nullptr, false);
            QCOMPARE(theme.accent(), QString("#2f6bed"));
        }
    }

  private:
    static void writeBytes(const QString &path, int size) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        // Random bytes, so a compressing disk can't shrink them.
        QByteArray data(size, Qt::Uninitialized);
        for (int i = 0; i < size; ++i)
            data[i] = char(QRandomGenerator::global()->generate());
        file.write(data);
    }
};

QTEST_MAIN(KeelTests)
#include "keel_tests.moc"
