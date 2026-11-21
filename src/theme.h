#pragma once

// Follows the Omarchy theme live: its accent, its light or dark mode, and
// Hyprland's window rounding. The surfaces are Keel's own pure white or
// neutral near-black rather than the theme's background, so a cream or
// tinted theme still gives a clean window; only the accent is borrowed.

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString onAccent READ onAccent NOTIFY changed)
    Q_PROPERTY(QString good READ good NOTIFY changed)
    Q_PROPERTY(QString warn READ warn NOTIFY changed)
    Q_PROPERTY(QString background READ background NOTIFY changed)
    Q_PROPERTY(QString bar READ bar NOTIFY changed)
    Q_PROPERTY(QString foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QString secondary READ secondary NOTIFY changed)
    Q_PROPERTY(QString muted READ muted NOTIFY changed)
    Q_PROPERTY(QString line READ line NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)

  public:
    // The directory and rounding reader are parameters so tests can use a
    // temp directory and no Hyprland.
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, bool askHyprland = true);

    bool dark() const { return m_override ? !m_themeDark : m_themeDark; }
    QString accent() const { return m_accent; }
    QString onAccent() const;
    QString good() const { return dark() ? "#4ade80" : "#16a34a"; }
    QString warn() const { return dark() ? "#f87171" : "#dc2626"; }
    QString background() const { return dark() ? "#111113" : "#ffffff"; }
    QString bar() const { return dark() ? "#18181b" : "#f5f6f8"; }
    QString foreground() const { return dark() ? "#e4e4e7" : "#14161b"; }
    QString secondary() const { return dark() ? "#a1a1aa" : "#4b5059"; }
    QString muted() const { return dark() ? "#71717a" : "#8b909a"; }
    QString line() const { return dark() ? "#27272a" : "#e4e6ea"; }
    int radius() const { return m_radius; }

    // Flips light and dark for this run only; the Omarchy theme is untouched.
    Q_INVOKABLE void flip();

    // The value of `key = "..."` in a colors.toml, or an empty string.
    static QString readKey(const QByteArray &toml, const QString &key);

  signals:
    void changed();

  private:
    void reload();

    QString m_directory;
    bool m_askHyprland;
    QString m_accent = "#2f6bed";
    bool m_themeDark = true;
    bool m_override = false;
    int m_radius = 0;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};
