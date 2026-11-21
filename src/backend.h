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
    Q_PROPERTY(QVariantList cores READ cores NOTIFY detailsSampled)
    Q_PROPERTY(QVariantList sections READ sections NOTIFY detailsSampled)
    Q_PROPERTY(QVariantList processes READ processes NOTIFY detailsSampled)
    Q_PROPERTY(bool sortByMemory READ sortByMemory WRITE setSortByMemory NOTIFY viewChanged)
    Q_PROPERTY(bool detailed READ detailed WRITE setDetailed NOTIFY viewChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY viewChanged)
    Q_PROPERTY(QVariantList cpuHistory READ cpuHistory NOTIFY sampled)
    Q_PROPERTY(QVariantList downHistory READ downHistory NOTIFY sampled)
    Q_PROPERTY(QVariantList upHistory READ upHistory NOTIFY sampled)
    Q_PROPERTY(QString downPeak READ downPeak NOTIFY sampled)
    Q_PROPERTY(QString upPeak READ upPeak NOTIFY sampled)
    Q_PROPERTY(Controls *controls READ controls CONSTANT)

  public:
    explicit Backend(QObject *parent = nullptr);

    QString headline() const { return m_summary.headline; }
    QString line() const { return m_summary.line; }
    bool calm() const { return m_summary.calm; }
    QVariantList meters() const { return m_meters; }
    QVariantList apps() const { return m_apps; }
    QVariantList cores() const { return m_cores; }
    QVariantList sections() const { return m_sections; }
    QVariantList processes() const { return m_processRows; }
    bool sortByMemory() const { return m_sortByMemory; }
    void setSortByMemory(bool byMemory);
    bool detailed() const { return m_detailed; }
    void setDetailed(bool detailed);
    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    // The last two minutes, oldest first, each value 0..1 of its own peak.
    QVariantList cpuHistory() const { return scaled(m_cpuHistory, 100); }
    QVariantList downHistory() const { return scaled(m_downHistory, peak(m_downHistory)); }
    QVariantList upHistory() const { return scaled(m_upHistory, peak(m_upHistory)); }
    QString downPeak() const;
    QString upPeak() const;
    Controls *controls() { return &m_controls; }

    // Closes every window of an app on the busiest list gracefully, as its
    // close button would, so it can ask to save.
    Q_INVOKABLE bool quitApp(int index);
    // Ends every process of an app on the busiest list (for background
    // programs, which have no window to close).
    Q_INVOKABLE bool endApp(int index, bool force);
    Q_INVOKABLE bool endProcess(int pid, bool force);

  signals:
    // Separate signals so a tick on the overview never re-evaluates the
    // details page, and a sort or search never re-reads the overview.
    void sampled();
    void detailsSampled();
    void viewChanged();

  private:
    void sample();
    void findSensors();
    void publishProcesses();
    static void push(QList<double> &history, double value);
    static double peak(const QList<double> &history);
    // The mean of the last three readings (six seconds).
    static double recent(const QList<double> &history);
    static QVariantList scaled(const QList<double> &history, double top);

    Controls m_controls;
    QTimer m_timer;
    qint64 m_lastSampleMs = 0;
    quint64 m_tick = 0;

    vitals::CpuTimes m_cpu;
    QList<vitals::CpuTimes> m_coreTimes;
    vitals::Counters m_disk, m_net;
    QHash<int, apps::Process> m_procs, m_previousProcs;
    quint64 m_ticksElapsed = 0;

    QString m_batteryPath, m_cpuTempPath, m_diskTempPath, m_fanPath;
    quint64 m_pageSize = 4096;

    vitals::Battery m_battery;
    vitals::Summary m_summary;
    QVariantList m_meters, m_apps, m_cores, m_sections, m_processRows;
    QList<apps::Usage> m_usages;
    bool m_sortByMemory = false;
    bool m_detailed = false;
    QString m_filter;
    QList<double> m_cpuHistory, m_heatHistory, m_downHistory, m_upHistory;
};
