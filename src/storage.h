#pragma once

// What is filling the disk. A native walk of the home folder that uses every
// core, never follows links and never leaves the home folder's own disk,
// keeping only the folders and files big enough to matter. The last result
// is saved, so the page opens instantly and a rescan is the user's choice.

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <atomic>
#include <memory>
#include <mutex>
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

// A large file nobody has changed in a long time: a likely candidate to
// archive or delete. `path` is relative to the scanned folder.
struct StaleFile {
    QString path;
    quint64 bytes = 0;
    qint64 modified = 0; // seconds since epoch
};

struct Progress {
    std::atomic<quint64> files{0};
    std::atomic<bool> cancel{false};
    // When staleMin is set, files at least that big and last changed before
    // staleBefore are collected here.
    quint64 staleMin = 0;
    qint64 staleBefore = 0;
    std::mutex staleLock;
    QList<StaleFile> stale;
};

// How much a folder grew between two scans. `path` is relative to the home.
struct Growth {
    QString path;
    qint64 delta = 0;
};

// Folder sizes by relative path ("Documents/Work"), down to `maxDepth`.
QHash<QString, quint64> flatten(const Node &root, int maxDepth);
// The folders that grew most, each at least `minDelta`. A folder is left out
// when one inside it accounts for most of its growth, so the list names
// where the space actually went rather than every folder above it.
QList<Growth> growth(const QHash<QString, quint64> &before, const QHash<QString, quint64> &after,
                     quint64 minDelta, int limit);

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
    Q_PROPERTY(QVariantList grown READ grown NOTIFY changed)
    Q_PROPERTY(QString grownSince READ grownSince NOTIFY changed)
    Q_PROPERTY(QVariantList stale READ stale NOTIFY changed)

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
    QVariantList grown() const;
    QString grownSince() const;
    QVariantList stale() const;
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
    Q_INVOKABLE void revealGrown(int index);
    Q_INVOKABLE void revealStale(int index);

  signals:
    void changed();

  private:
    void startScan(const QStringList &trail);
    void finished(const QStringList &trail, storage::Node node, QList<storage::StaleFile> stale);
    // Opens a folder under the home folder; anything else is refused.
    void revealInside(const QString &relative) const;
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
    QHash<QString, quint64> m_previous; // folder sizes from the scan before
    qint64 m_previousAt = 0;
    QList<storage::StaleFile> m_stale;
    QVariantList m_spots;
    std::unique_ptr<std::thread> m_worker;
    std::shared_ptr<storage::Progress> m_progress;
    QTimer m_tick;
};
