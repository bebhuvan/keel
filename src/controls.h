#pragma once

// The laptop's switches and levels, and the four modes that set the switches
// together. Every change goes through the same Omarchy command (or the tool
// Omarchy itself uses) as the rest of the desktop, so they always agree.
// Commands run in the background; the window never waits on one.

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <functional>

class QProcess;

class Controls : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap states READ states NOTIFY changed)
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString network READ network NOTIFY changed)
    Q_PROPERTY(double brightness READ brightness NOTIFY changed)
    Q_PROPERTY(double volume READ volume NOTIFY changed)
    Q_PROPERTY(bool muted READ muted NOTIFY changed)
    Q_PROPERTY(QString display READ display NOTIFY changed)
    Q_PROPERTY(QString monitor READ monitor NOTIFY changed)
    Q_PROPERTY(QString devices READ devices NOTIFY changed)
    Q_PROPERTY(bool batteryCare READ batteryCare NOTIFY changed)
    Q_PROPERTY(bool batteryCareAvailable READ batteryCareAvailable NOTIFY changed)

  public:
    // The directories are parameters so tests can point them at temp dirs.
    explicit Controls(const QString &stateDirectory = {}, const QString &backlightDirectory = {},
                      QObject *parent = nullptr);
    // Lets a change that is still running finish, so closing Keel right after
    // pressing a key never leaves Bluetooth or Wi-Fi half switched.
    ~Controls() override;

    static const QStringList &ids();
    static QStringList modes();
    // What each switch is in a mode.
    static QHash<QString, bool> modeStates(const QString &mode);
    // The mode whose switches all match, or "custom".
    static QString matchMode(const QHash<QString, bool> &states);
    // The Wi-Fi network name from `nmcli -t -f TYPE,STATE,CONNECTION device`.
    static QString parseNetwork(const QByteArray &devices);
    // Volume 0..1 and whether it is muted, from `wpctl get-volume`.
    static bool parseVolume(const QByteArray &reply, double &volume, bool &muted);

    // How the screens are arranged, from Hyprland's `j/monitors all`:
    // "both", "monitor" (laptop screen off), "mirror", or "laptop" when no
    // external monitor is connected. `name` gets the monitor's model name.
    static QString parseDisplay(const QByteArray &monitorsJson, QString &name);
    static QStringList displayChoices() { return {"both", "monitor"}; }

    QVariantMap states() const;
    QString mode() const { return matchMode(m_states); }
    QString error() const { return m_error; }
    QString network() const { return m_network; }
    double brightness() const { return m_brightness; }
    double volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    QString display() const { return m_display; }
    QString monitor() const { return m_monitor; }
    // Connected devices that report a battery: "wh-1000xm4 80% · mx master 3 55%".
    QString devices() const { return m_devices; }
    // Battery care stops charging at 80% (and resumes below 75%), which keeps
    // a battery that's mostly plugged in healthy for longer.
    bool batteryCare() const { return m_batteryCare; }
    bool batteryCareAvailable() const { return !m_batteryDirectory.isEmpty(); }

    static QString formatDevices(const QList<QPair<QString, int>> &devices);

    Q_INVOKABLE void toggle(const QString &id);
    Q_INVOKABLE void applyMode(const QString &mode);
    Q_INVOKABLE void setBrightness(double level);
    Q_INVOKABLE void setVolume(double level);
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void setDisplay(const QString &choice);
    Q_INVOKABLE void cycleDisplay();
    // Asks for the password once (the system prompt), then writes the limit
    // and a rule so it survives restarts.
    Q_INVOKABLE void toggleBatteryCare();

    // Cheap reads from sysfs and D-Bus, for every tick.
    void refresh();
    // Reads that start a process, run less often.
    void refreshSlow();

  signals:
    void changed();

  private:
    void set(const QString &id, bool on);
    void run(const QString &id, const QString &failure, const QString &program, const QStringList &arguments);
    void query(const QString &name, const QString &program, const QStringList &arguments,
               const std::function<void(const QByteArray &)> &onReply);
    void queryPowerSaver();
    void queryDevices();
    void runDisplayStep(const QString &target, bool fromMirror);
    void checkDisplaySettled();
    void update(const QString &id, bool on);
    void applyLevel(const QString &id);
    void fail(const QString &message);

    QString m_stateDirectory;
    QString m_backlightDirectory;
    QHash<QString, bool> m_states;
    QSet<QString> m_pending;
    QHash<QString, QProcess *> m_queries;
    QHash<QString, double> m_wantedLevels; // set while a level change waits its turn
    bool m_nightKnown = false;
    bool m_powerQueryPending = false;
    bool m_devicesQueryPending = false;
    QString m_displayTarget;
    bool m_displayNeedStep2 = false;
    int m_displayAttempts = 0;
    QTimer m_displaySettle;
    QTimer m_slowDebounce;
    QString m_error;
    QString m_network;
    double m_brightness = -1;
    double m_volume = -1;
    bool m_muted = false;
    QString m_display = "laptop";
    QString m_monitor;
    QString m_devices;
    QString m_batteryDirectory;
    bool m_batteryCare = false;
    QTimer m_errorClear;
};
