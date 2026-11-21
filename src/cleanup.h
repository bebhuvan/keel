#pragma once

// One-click "free up space": a short list of places that are safe to clear,
// each measured first and shown with what it frees, so the click is never a
// surprise. Only fixed, known paths and commands are ever touched.

#include <QObject>
#include <QString>
#include <QVariantList>
#include <memory>
#include <thread>

namespace cleanup {

struct Item {
    QString id;
    QString label;
    QString note;
    quint64 bytes = 0;
    bool selected = true;
    bool needsPassword = false;
    bool estimate = false; // the size is an upper bound, not exact
};

// "finished dry run: 3 candidates (disk space saved: 5.71 MiB)" → bytes.
quint64 parsePaccache(const QByteArray &output);
// "… take up 95.8M in the file system." → bytes.
quint64 parseJournalUsage(const QByteArray &output);
// Deletes everything inside home/relative but not the folder itself. Refuses
// unless the folder's real location is exactly that path, so a symlink
// planted anywhere along the way can't redirect the deletion elsewhere.
bool emptyFolder(const QString &home, const QString &relative);

} // namespace cleanup

class Cleanup : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString total READ total NOTIFY changed)
    Q_PROPERTY(QString result READ result NOTIFY changed)

  public:
    explicit Cleanup(const QString &home = {}, QObject *parent = nullptr);
    ~Cleanup() override;

    QVariantList items() const;
    bool busy() const { return m_worker != nullptr || m_root != nullptr; }
    QString total() const;
    QString result() const { return m_result; }

    // Paths a clean-up item clears, relative to the home folder.
    static QStringList folders(const QString &id);

    Q_INVOKABLE void measure();
    Q_INVOKABLE void toggle(int index);
    Q_INVOKABLE void clean();

  signals:
    void changed();
    // After a clean, so the storage page can refresh what it shows.
    void cleaned();

  private:
    void measured(QList<cleanup::Item> items);
    void userCleaned(QStringList failed);
    void finish();
    double freeBytes() const;

    QString m_home;
    QList<cleanup::Item> m_items;
    QString m_result;
    double m_freeBefore = 0;
    QStringList m_failures;
    int m_pendingParts = 0;
    std::unique_ptr<std::thread> m_worker;
    class QProcess *m_root = nullptr;
};
