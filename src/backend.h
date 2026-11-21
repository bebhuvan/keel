#pragma once

// Reads the machine every two seconds and hands QML ready-to-show text.
// All the judging (words, headline, ranking) is in vitals and apps; this
// class only reads files, keeps the previous sample, and publishes.

#include "apps.h"
#include "controls.h"
#include "vitals.h"

#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantList>

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString headline READ headline NOTIFY sampled)
    Q_PROPERTY(QString line READ line NOTIFY sampled)
    Q_PROPERTY(bool calm READ calm NOTIFY sampled)
    Q_PROPERTY(QVariantList meters READ meters NOTIFY sampled)
    Q_PROPERTY(QVariantList apps READ apps NOTIFY sampled)
    Q_PROPERTY(QVariantList heavy READ heavy NOTIFY sampled)
    Q_PROPERTY(QVariantList cores READ cores NOTIFY detailsSampled)
    Q_PROPERTY(QVariantList sections READ sections NOTIFY detailsSampled)
    Q_PROPERTY(QVariantList processes READ processes NOTIFY detailsSampled)
    Q_PROPERTY(bool sortByMemory READ sortByMemory WRITE setSortByMemory NOTIFY viewChanged)
    Q_PROPERTY(bool detailed READ detailed WRITE setDetailed NOTIFY viewChanged)
    Q_PROPERTY(bool showHeavy READ showHeavy WRITE setShowHeavy NOTIFY viewChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY viewChanged)
    Q_PROPERTY(QVariantList cpuHistory READ cpuHistory NOTIFY sampled)
    Q_PROPERTY(QVariantList downHistory READ downHistory NOTIFY sampled)
    Q_PROPERTY(QVariantList upHistory READ upHistory NOTIFY sampled)
    Q_PROPERTY(QString downPeak READ downPeak NOTIFY sampled)
    Q_PROPERTY(QString upPeak READ upPeak NOTIFY sampled)
    Q_PROPERTY(QStringList failedServices READ failedServices NOTIFY sampled)
    Q_PROPERTY(Controls *controls READ controls CONSTANT)

  public:
    explicit Backend(QObject *parent = nullptr);

    QString headline() const { return m_summary.headline; }
    QString line() const { return m_summary.line; }
    bool calm() const { return m_summary.calm; }
    QVariantList meters() const { return m_meters; }
    QVariantList apps() const { return m_apps; }
    // The apps that have used the most processor time since they opened.
    QVariantList heavy() const { return m_heavy; }
    QVariantList cores() const { return m_cores; }
    QVariantList sections() const { return m_sections; }
    QVariantList processes() const { return m_processRows; }
    bool sortByMemory() const { return m_sortByMemory; }
    void setSortByMemory(bool byMemory);
    bool detailed() const { return m_detailed; }
    void setDetailed(bool detailed);
    bool showHeavy() const { return m_showHeavy; }
    void setShowHeavy(bool show);
    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    // The last two minutes, oldest first, each value 0..1 of its own peak.
    QVariantList cpuHistory() const { return scaled(m_cpuHistory, 100); }
    QVariantList downHistory() const { return scaled(m_downHistory, peak(m_downHistory)); }
    QVariantList upHistory() const { return scaled(m_upHistory, peak(m_upHistory)); }
    QString downPeak() const;
    QString upPeak() const;
    QStringList failedServices() const { return m_failedServices; }
    Controls *controls() { return &m_controls; }

    // Unit names from `systemctl --failed --no-legend --plain`.
    static QStringList parseFailedUnits(const QByteArray &output);

    // Closes windows gracefully, as their close buttons would, so apps can
    // ask to save. Takes the addresses the window showed, not a list
    // position, because the list re-sorts every two seconds.
    Q_INVOKABLE bool closeWindows(const QStringList &addresses);
    // Ends the processes the user was asked about: a list of [pid, start]
    // pairs, each checked again so a reused pid is never hit.
    Q_INVOKABLE bool endProcesses(const QVariantList &targets, bool force);

  signals:
    // Separate signals so a tick on the overview never re-evaluates the
    // details page, and a sort or search never re-reads the overview.
    void sampled();
    void detailsSampled();
    void viewChanged();

  private:
    void sample();
    void findSensors();
    void checkFailedServices();
    void publishHeavy(const QList<apps::Window> &windows);
    void publishDetails(const QByteArray &stat, double cpuPercent, const vitals::Memory &memory,
                        const vitals::Reading &reading, double celsius, double uptime, qint64 nowMs);
    void publishProcesses();
    static void push(QList<double> &history, double value);
    static double peak(const QList<double> &history);
    // The mean of the last three readings (six seconds).
    static double recent(const QList<double> &history);
    static QVariantList scaled(const QList<double> &history, double top);

    Controls m_controls;
    QTimer m_timer;
    qint64 m_lastSampleMs = 0;
    qint64 m_lastDiskMs = 0;
    quint64 m_tick = 0;
    bool m_haveNetSample = false;
    bool m_haveDiskSample = false;

    vitals::CpuTimes m_cpu;
    QList<vitals::CpuTimes> m_coreTimes;
    vitals::Counters m_disk, m_net;
    QHash<int, apps::Process> m_procs, m_previousProcs;
    quint64 m_ticksElapsed = 0;

    QString m_batteryPath, m_cpuTempPath, m_diskTempPath, m_fanPath;
    quint64 m_pageSize = 4096;

    vitals::Battery m_battery;
    QString m_lastBatteryStatus;
    QList<double> m_powerHistory;
    vitals::Summary m_summary;
    QVariantList m_heavy;
    QVariantList m_meters, m_apps, m_cores, m_sections, m_processRows;
    QList<apps::Usage> m_usages;
    bool m_sortByMemory = false;
    bool m_detailed = false;
    bool m_showHeavy = false;
    bool m_kernelUpdated = false;
    bool m_failedQueryPending = false;
    QStringList m_failedServices;
    QString m_filter;
    QList<double> m_cpuHistory, m_heatHistory, m_downHistory, m_upHistory;
};
