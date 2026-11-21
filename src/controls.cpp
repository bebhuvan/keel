#include "controls.h"

#include "hyprland.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <memory>

namespace {

QByteArray readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed() : QByteArray();
}

// A radio counts as on when no rfkill switch of that type blocks it. Reading
// sysfs is free, where asking bluetoothctl or nmcli takes a process each.
bool radioOn(const QByteArray &type) {
    for (const QFileInfo &entry : QDir("/sys/class/rfkill").entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (readFile(entry.filePath() + "/type") == type && readFile(entry.filePath() + "/soft") == "0" &&
            readFile(entry.filePath() + "/hard") == "0")
            return true;
    return false;
}

QString displayGuardPath() {
    const QString path = QDir::homePath() + "/.local/bin/omarchy-display-guard";
    const QFileInfo info(path);
    return info.isFile() && info.isExecutable() ? path : QString();
}

const QString helperPath = "/usr/lib/keel/keel-helper";

QString label(const QString &id) {
    static const QHash<QString, QString> names{{"wifi", "wi-fi"},          {"bluetooth", "bluetooth"},
                                               {"dnd", "do not disturb"},  {"night", "night light"},
                                               {"saver", "battery saver"}, {"awake", "stay awake"}};
    return names.value(id, id);
}

} // namespace

Controls::Controls(const QString &stateDirectory, const QString &backlightDirectory, QObject *parent)
    : QObject(parent),
      m_stateDirectory(stateDirectory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/indicators"
                                                : stateDirectory) {
    if (!backlightDirectory.isEmpty()) {
        m_backlightDirectory = backlightDirectory;
    } else {
        const QStringList panels = QDir("/sys/class/backlight").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (!panels.isEmpty())
            m_backlightDirectory = "/sys/class/backlight/" + panels.first();
    }
    for (const QFileInfo &supply :
         QDir("/sys/class/power_supply").entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (QFileInfo::exists(supply.filePath() + "/charge_control_end_threshold") &&
            m_batteryDirectory.isEmpty() &&
            QRegularExpression("^BAT[0-9]{1,2}$").match(supply.fileName()).hasMatch())
            m_batteryDirectory = supply.filePath();
    for (const QString &id : ids())
        m_states.insert(id, false);
    m_errorClear.setSingleShot(true);
    m_errorClear.setInterval(6000);
    connect(&m_errorClear, &QTimer::timeout, this, [this] {
        m_error.clear();
        emit changed();
    });
    m_displaySettle.setInterval(250);
    connect(&m_displaySettle, &QTimer::timeout, this, &Controls::checkDisplaySettled);
    m_slowDebounce.setSingleShot(true);
    m_slowDebounce.setInterval(150);
    connect(&m_slowDebounce, &QTimer::timeout, this, &Controls::refreshSlow);
}

Controls::~Controls() {
    m_displaySettle.stop();
    m_slowDebounce.stop();
    QElapsedTimer budget;
    budget.start();
    for (QProcess *process : findChildren<QProcess *>()) {
        if (process->state() == QProcess::NotRunning)
            continue;
        if (m_queries.values().contains(process) || process->program() == "pkexec") {
            process->kill();
            process->waitForFinished(100);
        } else {
            const int left = int(qMax<qint64>(50, 1000 - budget.elapsed()));
            if (!process->waitForFinished(left)) {
                process->terminate();
                process->waitForFinished(100);
            }
        }
    }
}

const QStringList &Controls::ids() {
    static const QStringList list{"wifi", "bluetooth", "dnd", "night", "saver", "awake"};
    return list;
}

QStringList Controls::modes() { return {"everyday", "focus", "travel", "night"}; }

QHash<QString, bool> Controls::modeStates(const QString &mode) {
    // In ids() order: wi-fi, bluetooth, do not disturb, night light, battery
    // saver, stay awake. Travel keeps Wi-Fi on: being cut off is worse than
    // the minutes it would save.
    static const QHash<QString, QList<bool>> table{
        {"everyday", {true, true, false, false, false, false}},
        {"focus", {true, false, true, false, false, true}},
        {"travel", {true, false, false, false, true, false}},
        {"night", {true, true, true, true, false, false}},
    };
    QHash<QString, bool> states;
    const QList<bool> values = table.value(mode);
    for (int i = 0; i < values.size(); ++i)
        states.insert(ids()[i], values[i]);
    return states;
}

QString Controls::matchMode(const QHash<QString, bool> &states) {
    for (const QString &mode : modes()) {
        const QHash<QString, bool> wanted = modeStates(mode);
        bool all = true;
        for (auto it = wanted.begin(); it != wanted.end() && all; ++it)
            all = states.value(it.key()) == it.value();
        if (all)
            return mode;
    }
    return "custom";
}

// Lines look like "wifi:connected:Home Net"; nmcli escapes a ':' inside the
// name as "\:".
QString Controls::parseNetwork(const QByteArray &devices) {
    static const QRegularExpression separator("(?<!\\\\):");
    for (const QByteArray &line : devices.split('\n')) {
        const QStringList fields = QString::fromUtf8(line).split(separator);
        if (fields.size() >= 3 && fields[0] == "wifi" && fields[1] == "connected")
            return fields.mid(2).join(':').replace("\\:", ":");
    }
    return {};
}

// "Volume: 0.40" or "Volume: 0.40 [MUTED]".
bool Controls::parseVolume(const QByteArray &reply, double &volume, bool &muted) {
    static const QRegularExpression pattern("^Volume:\\s*([0-9.]+)(\\s*\\[MUTED\\])?");
    const auto match = pattern.match(QString::fromUtf8(reply.trimmed()));
    if (!match.hasMatch())
        return false;
    volume = qBound(0.0, match.captured(1).toDouble(), 1.0);
    muted = match.hasCaptured(2);
    return true;
}

QString Controls::parseDisplay(const QByteArray &monitorsJson, QString &name) {
    static const QRegularExpression internal("^(eDP|LVDS|DSI)-");
    bool laptopOn = false, externalOn = false, mirrored = false;
    name.clear();
    for (const QJsonValue &value : QJsonDocument::fromJson(monitorsJson).array()) {
        const QJsonObject monitor = value.toObject();
        const QString output = monitor.value("name").toString();
        const bool on = !monitor.value("disabled").toBool();
        if (output.isEmpty() || output == "FALLBACK")
            continue;
        if (monitor.value("mirrorOf").toString("none") != "none")
            mirrored = true;
        if (internal.match(output).hasMatch()) {
            laptopOn = laptopOn || on;
        } else if (on) {
            externalOn = true;
            if (name.isEmpty()) {
                // "Acer Technologies VG271U M3 2326176234207" → "Acer VG271U M3".
                QStringList words = monitor.value("description").toString().split(' ', Qt::SkipEmptyParts);
                if (words.size() > 1 && words.last().size() >= 8)
                    words.removeLast(); // the serial number
                if (words.size() > 2 && words[1].startsWith("Technolog"))
                    words.removeAt(1);
                name = words.join(' ');
                if (name.isEmpty())
                    name = output;
            }
        }
    }
    if (!externalOn)
        return "laptop";
    if (mirrored)
        return "mirror";
    return laptopOn ? "both" : "monitor";
}

QString Controls::formatDevices(const QList<QPair<QString, int>> &devices) {
    QStringList parts;
    for (const auto &device : devices)
        parts.append(QString("%1 %2%").arg(device.first).arg(device.second));
    return parts.join("  ·  ");
}

void Controls::toggleBatteryCare() {
    if (m_batteryDirectory.isEmpty())
        return fail("this battery doesn't support a charge limit.");
    if (m_pending.contains("care"))
        return;
    const bool on = !m_batteryCare;
    m_batteryCare = on;
    emit changed();
    // The helper does the root part; it's installed with Keel and its
    // password prompt says what it's for.
    if (!QFileInfo::exists(helperPath)) {
        m_batteryCare = !on;
        return fail("battery care needs keel installed (bin/install), not run from the build folder.");
    }
    run("care",
        on ? "battery care wasn't turned on. it needs your password."
           : "battery care wasn't turned off. it needs your password.",
        "pkexec", {helperPath, "battery-care", on ? "on" : "off"});
}

QVariantMap Controls::states() const {
    QVariantMap map;
    for (auto it = m_states.begin(); it != m_states.end(); ++it)
        map.insert(it.key(), it.value());
    return map;
}

void Controls::update(const QString &id, bool on) {
    if (m_pending.contains(id) || m_states.value(id) == on)
        return;
    m_states[id] = on;
    emit changed();
}

void Controls::fail(const QString &message) {
    m_error = message;
    m_errorClear.start();
    emit changed();
}

void Controls::query(const QString &name, const QString &program, const QStringList &arguments,
                     const std::function<void(const QByteArray &)> &onReply) {
    if (m_queries.contains(name))
        return; // The last answer is still on its way.
    auto *process = new QProcess(this);
    m_queries.insert(name, process);
    auto done = [this, name, process] {
        m_queries.remove(name);
        process->deleteLater();
    };
    connect(process, &QProcess::finished, this, [process, onReply, done](int code) {
        if (code == 0)
            onReply(process->readAllStandardOutput());
        done();
    });
    connect(process, &QProcess::errorOccurred, this, [done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            done();
    });
    process->start(program, arguments);
}

void Controls::refresh() {
    update("wifi", radioOn("wlan"));
    update("bluetooth", radioOn("bluetooth"));
    update("awake", QFileInfo::exists(m_stateDirectory + "/stay-awake"));
    if (!m_backlightDirectory.isEmpty() && !m_wantedLevels.contains("brightness") &&
        !m_pending.contains("brightness")) {
        const double max = readFile(m_backlightDirectory + "/max_brightness").toDouble();
        const double level = max > 0 ? readFile(m_backlightDirectory + "/brightness").toDouble() / max : -1;
        if (!qFuzzyCompare(level + 2, m_brightness + 2)) {
            m_brightness = level;
            emit changed();
        }
    }
}

void Controls::queryPowerSaver() {
    if (m_powerQueryPending)
        return;
    m_powerQueryPending = true;
    QDBusMessage msg = QDBusMessage::createMethodCall("net.hadess.PowerProfiles", "/net/hadess/PowerProfiles",
                                                      "org.freedesktop.DBus.Properties", "Get");
    msg << QString("net.hadess.PowerProfiles") << QString("ActiveProfile");
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(msg, 2000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        m_powerQueryPending = false;
        const QDBusPendingReply<QDBusVariant> reply = *w;
        if (reply.isValid())
            update("saver", reply.value().variant().toString() == "power-saver");
        w->deleteLater();
    });
}

void Controls::queryDevices() {
    if (m_devicesQueryPending)
        return;
    m_devicesQueryPending = true;
    QDBusMessage msg = QDBusMessage::createMethodCall("org.freedesktop.UPower", "/org/freedesktop/UPower",
                                                      "org.freedesktop.UPower", "EnumerateDevices");
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(msg, 2000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<QList<QDBusObjectPath>> reply = *w;
        w->deleteLater();
        if (!reply.isValid()) {
            m_devicesQueryPending = false;
            return;
        }
        QStringList targets;
        for (const QDBusObjectPath &path : reply.value()) {
            const QString name = path.path().section('/', -1);
            if (!name.startsWith("battery_BAT") && !name.startsWith("line_power") && name != "DisplayDevice")
                targets.append(path.path());
        }
        if (targets.isEmpty()) {
            m_devicesQueryPending = false;
            if (!m_devices.isEmpty()) {
                m_devices.clear();
                emit changed();
            }
            return;
        }
        struct Batch {
            int remaining = 0;
            QList<QPair<QString, int>> devices;
        };
        auto batch = std::make_shared<Batch>();
        batch->remaining = int(targets.size());
        for (const QString &objectPath : targets) {
            QDBusMessage props = QDBusMessage::createMethodCall("org.freedesktop.UPower", objectPath,
                                                                "org.freedesktop.DBus.Properties", "GetAll");
            props << QString("org.freedesktop.UPower.Device");
            auto *pw = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(props, 2000), this);
            connect(pw, &QDBusPendingCallWatcher::finished, this,
                    [this, batch](QDBusPendingCallWatcher *devWatcher) {
                        const QDBusPendingReply<QVariantMap> devReply = *devWatcher;
                        devWatcher->deleteLater();
                        if (devReply.isValid()) {
                            const QVariantMap map = devReply.value();
                            const QString model = map.value("Model").toString().trimmed();
                            const int percent = qRound(map.value("Percentage").toDouble());
                            if (!model.isEmpty() && percent > 0)
                                batch->devices.append({model, percent});
                        }
                        if (--batch->remaining == 0) {
                            m_devicesQueryPending = false;
                            const QString formatted = formatDevices(batch->devices);
                            if (formatted != m_devices) {
                                m_devices = formatted;
                                emit changed();
                            }
                        }
                    });
        }
    });
}

void Controls::refreshSlow() {
    if (!m_batteryDirectory.isEmpty() && !m_pending.contains("care")) {
        bool ok = false;
        const int limit = readFile(m_batteryDirectory + "/charge_control_end_threshold").toInt(&ok);
        if (ok && limit > 0) {
            const bool care = limit <= 90;
            if (care != m_batteryCare) {
                m_batteryCare = care;
                emit changed();
            }
        }
    }
    queryDevices();
    queryPowerSaver();
    if (!m_pending.contains("display")) {
        QString name;
        const QString display = parseDisplay(hyprland::request("j/monitors all"), name);
        if (display != m_display || name != m_monitor) {
            m_display = display;
            m_monitor = name;
            emit changed();
        }
    }
    query("dnd", "omarchy-shell", {"notifications", "isDnd"},
          [this](const QByteArray &reply) { update("dnd", reply.trimmed() == "on"); });
    query("night", "omarchy-toggle-nightlight", {"--status"}, [this](const QByteArray &reply) {
        m_nightKnown = true;
        update("night", QJsonDocument::fromJson(reply).object().value("enabled").toBool());
    });
    query("network", "nmcli", {"-t", "-f", "TYPE,STATE,CONNECTION", "device"},
          [this](const QByteArray &reply) {
              const QString network = parseNetwork(reply);
              if (network != m_network) {
                  m_network = network;
                  emit changed();
              }
          });
    if (!m_wantedLevels.contains("volume"))
        query("volume", "wpctl", {"get-volume", "@DEFAULT_AUDIO_SINK@"}, [this](const QByteArray &reply) {
            double volume = 0;
            bool muted = false;
            if (parseVolume(reply, volume, muted) && (volume != m_volume || muted != m_muted)) {
                m_volume = volume;
                m_muted = muted;
                emit changed();
            }
        });
}

void Controls::toggle(const QString &id) {
    if (m_states.contains(id))
        set(id, !m_states.value(id));
}

void Controls::applyMode(const QString &mode) {
    const QHash<QString, bool> wanted = modeStates(mode);
    for (const QString &id : ids()) {
        // Night light only supports toggling, so never flip it until its
        // current state has been read at least once.
        if (id == "night" && !m_nightKnown)
            continue;
        if (wanted.contains(id) && wanted.value(id) != m_states.value(id))
            set(id, wanted.value(id));
    }
}

void Controls::set(const QString &id, bool on) {
    if (m_pending.contains(id))
        return;
    // Show the new state at once; the refresh after the command corrects it
    // if the command did not take.
    m_states[id] = on;
    m_pending.insert(id);
    emit changed();

    const QString state = on ? "on" : "off";
    const QString failure = QString("Couldn't turn %1 %2. Try again in a moment.").arg(label(id), state);
    if (id == "wifi")
        run(id, failure, "nmcli", {"radio", "wifi", state});
    else if (id == "bluetooth")
        run(id, failure, "omarchy-bluetooth-power", {state});
    else if (id == "dnd")
        run(id, failure, "omarchy-shell", {"notifications", "setDnd", state});
    else if (id == "night")
        // Omarchy's night light only toggles; set() is only called when the
        // wanted state differs, so a toggle is the right move.
        run(id, failure, "omarchy-toggle-nightlight", {});
    else if (id == "saver")
        run(id, failure, "omarchy-powerprofiles-set", {"autodetect", on ? "power-saver" : "balanced"});
    else if (id == "awake")
        run(id, failure, "omarchy-toggle-idle", {on ? "stay-awake" : "allow-idle"});
}

void Controls::setBrightness(double level) {
    if (m_backlightDirectory.isEmpty())
        return fail("this screen's brightness can't be changed from here.");
    // Never fully dark: a black screen with no way back is no use.
    m_brightness = qBound(0.05, level, 1.0);
    m_wantedLevels.insert("brightness", m_brightness);
    emit changed();
    applyLevel("brightness");
}

void Controls::setVolume(double level) {
    m_volume = qBound(0.0, level, 1.0);
    m_muted = false;
    m_wantedLevels.insert("volume", m_volume);
    emit changed();
    applyLevel("volume");
}

// Through Omarchy's own monitor commands, one reload at a time, waiting for
// Hyprland's output list to settle before allowing another switch. Never
// chains two reloads back to back.
void Controls::setDisplay(const QString &choice) {
    if (m_display == "laptop")
        return fail("no external monitor is connected.");
    if (!displayChoices().contains(choice) || m_pending.contains("display") || choice == m_display)
        return;
    const bool fromMirror = m_display == "mirror";
    m_displayTarget = choice;
    m_displayNeedStep2 = fromMirror && choice == "monitor";
    m_displayAttempts = 0;
    m_display = choice;
    emit changed();
    runDisplayStep(choice, fromMirror);
}

void Controls::runDisplayStep(const QString &target, bool fromMirror) {
    if (fromMirror) {
        run("display", "couldn't switch screens. try again in a moment.",
            "omarchy-hyprland-monitor-internal-mirror", {"off"});
        return;
    }
    // If a custom display guard is present on this laptop, let it flip the
    // internal display so its preference flag stays in step with Omarchy's.
    const QString guard = displayGuardPath();
    if (!guard.isEmpty()) {
        run("display", "couldn't switch screens. try again in a moment.", guard, {"--toggle"});
        return;
    }
    run("display", "couldn't switch screens. try again in a moment.", "omarchy-hyprland-monitor-internal",
        {target == "both" ? "on" : "off"});
}

void Controls::checkDisplaySettled() {
    QString name;
    const QString actual = parseDisplay(hyprland::request("j/monitors all"), name);
    const QString wanted = m_displayNeedStep2 ? QString("both") : m_displayTarget;
    const bool arrived = actual == wanted;
    if (!arrived && ++m_displayAttempts < 16)
        return;
    m_displaySettle.stop();
    if (m_displayNeedStep2 && arrived) {
        m_displayNeedStep2 = false;
        m_displayAttempts = 0;
        runDisplayStep(m_displayTarget, false);
        return;
    }
    m_displayNeedStep2 = false;
    m_displayTarget.clear();
    m_pending.remove("display");
    if (actual != m_display || name != m_monitor) {
        m_display = actual;
        m_monitor = name;
    }
    emit changed();
}

void Controls::cycleDisplay() {
    const QStringList choices = displayChoices();
    const int at = int(choices.indexOf(m_display));
    setDisplay(choices[(at + 1) % choices.size()]);
}

void Controls::toggleMute() {
    if (m_pending.contains("mute"))
        return;
    m_muted = !m_muted;
    emit changed();
    run("mute", "couldn't change mute. try again in a moment.", "wpctl",
        {"set-mute", "@DEFAULT_AUDIO_SINK@", m_muted ? "1" : "0"});
}

// Holding a key sends many changes; only one command runs at a time and the
// latest wanted level goes next, so the screen never lags behind the keys.
void Controls::applyLevel(const QString &id) {
    if (m_pending.contains(id) || !m_wantedLevels.contains(id))
        return;
    const double level = m_wantedLevels.take(id);
    m_pending.insert(id);
    if (id == "brightness")
        run(id, "couldn't change brightness.", "brightnessctl",
            {"-q", "set", QString::number(qRound(level * 100)) + "%"});
    else
        run(id, "couldn't change volume.", "sh",
            {"-c",
             "wpctl set-mute @DEFAULT_AUDIO_SINK@ 0 && wpctl set-volume -l 1 @DEFAULT_AUDIO_SINK@ \"$0\"",
             QString::number(level, 'f', 2)});
}

void Controls::run(const QString &id, const QString &failure, const QString &program,
                   const QStringList &arguments) {
    m_pending.insert(id);
    auto *process = new QProcess(this);
    auto finish = [this, process, id, failure](bool ok) {
        process->deleteLater();
        if (id == "display") {
            if (!ok) {
                m_displayNeedStep2 = false;
                m_displayTarget.clear();
                m_pending.remove(id);
                fail(failure);
                m_slowDebounce.start();
                emit changed();
            } else {
                // Keep "display" locked until Hyprland finishes reloading and
                // reports the new layout (or times out).
                m_displayAttempts = 0;
                m_displaySettle.start();
            }
            return;
        }
        if (!ok)
            fail(failure);
        m_pending.remove(id);
        if (id == "brightness" || id == "volume")
            applyLevel(id);
        refresh();
        if (id != "brightness" && id != "volume")
            m_slowDebounce.start();
        emit changed();
    };
    connect(process, &QProcess::finished, this, [finish](int code, QProcess::ExitStatus status) {
        finish(code == 0 && status == QProcess::NormalExit);
    });
    connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(false);
    });
    process->start(program, arguments);
}
