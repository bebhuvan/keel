#pragma once

// What is filling the disk. A native walk of the home folder that uses every
// core, never follows links and never leaves the home folder's own disk,
// keeping only the folders and files big enough to matter. The last result
// is saved, so the page opens instantly and a rescan is the user's choice.

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <atomic>
#include <memory>
#include <thread>

namespace storage {

struct Node {
    QString name;
    quint64 bytes = 0;
    bool directory = false;
    // False when the walk stopped keeping detail below this folder; entering
    // it starts a fresh, smaller scan.
    bool detailed = true;
    QList<Node> children; // largest first
};

struct Progress {
    std::atomic<quint64> files{0};
    std::atomic<bool> cancel{false};
};

// Walks `path`. Children are kept `keepDepth` levels deep and only when at
// least `keepMinBytes`; everything else is still counted in its parent.
// Sizes are space actually used on disk (allocated blocks), as `du` reports.
Node scan(const QString &path, int keepDepth, quint64 keepMinBytes, Progress &progress);

const Node *find(const Node &root, const QStringList &names);
QJsonObject toJson(const Node &node);
Node fromJson(const QJsonObject &object);

} // namespace storage

class Storage : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString place READ place NOTIFY changed)
    Q_PROPERTY(bool atTop READ atTop NOTIFY changed)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(QVariantList spots READ spots NOTIFY changed)
    Q_PROPERTY(double usedFraction READ usedFraction NOTIFY changed)
    Q_PROPERTY(QString usedText READ usedText NOTIFY changed)
    Q_PROPERTY(QString freeText READ freeText NOTIFY changed)
    Q_PROPERTY(bool compressed READ compressed CONSTANT)

  public:
    // The paths are parameters so tests can use temp directories.
    explicit Storage(const QString &home = {}, const QString &cacheFile = {}, QObject *parent = nullptr);
    ~Storage() override;

    bool scanning() const { return m_worker != nullptr; }
    QString status() const;
    void disk(double &total, double &free) const;
    QString place() const;
    bool atTop() const { return m_trail.isEmpty(); }
    QVariantList entries() const;
    QVariantList spots() const { return m_spots; }
    double usedFraction() const;
    QString usedText() const;
    QString freeText() const;
    // True when the home folder's filesystem compresses files (btrfs with
    // compress=…), so sizes here are larger than the space they take.
    bool compressed() const { return m_compressed; }
    // From /proc/mounts: whether the mount holding `path` compresses.
    static bool mountCompresses(const QByteArray &mounts, const QString &path);

    // Loads the saved result, or scans when there is none. Called when the
    // storage page first opens, so Keel pays nothing until then.
    Q_INVOKABLE void open();
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void enter(int index);
    Q_INVOKABLE void up();
    Q_INVOKABLE void reveal(int index);
    Q_INVOKABLE void revealSpot(int index);
    // Permanently deletes everything in the trash. The window asks first.
    Q_INVOKABLE bool emptyTrash();

  signals:
    void changed();

  private:
    void startScan(const QStringList &trail);
    void finished(const QStringList &trail, storage::Node node);
    void buildSpots();
    void save() const;
    QString pathOf(const QStringList &trail) const;
    const storage::Node *current() const;

    QString m_home;
    QString m_cacheFile;
    storage::Node m_root;
    bool m_loaded = false;
    QStringList m_trail; // folder names from the home folder down
    QStringList m_scanTrail;
    qint64 m_scannedAt = 0; // ms since epoch
    bool m_compressed = false;
    QVariantList m_spots;
    std::unique_ptr<std::thread> m_worker;
    std::shared_ptr<storage::Progress> m_progress;
    QTimer m_tick;
};
