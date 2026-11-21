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
    // One warm colour, only for what needs looking at (heat, a full disk, a
    // hungry app). Everything else is ink on white or near-black.
    QString warn() const { return dark() ? "#ff7a2e" : "#e2540a"; }
    QString background() const { return dark() ? "#0f0f10" : "#ffffff"; }
    // The faint fill behind a hovered or picked row.
    QString bar() const { return dark() ? "#1b1b1c" : "#f4f4f4"; }
    QString foreground() const { return dark() ? "#ededed" : "#111111"; }
    QString secondary() const { return dark() ? "#9c9c9c" : "#666666"; }
    // Ticks, empty lamps and placeholders, never words that must be read.
    QString muted() const { return dark() ? "#5a5a5a" : "#a6a6a6"; }
    QString line() const { return dark() ? "#262627" : "#e6e6e6"; }
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
