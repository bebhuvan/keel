// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include "hyprland.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>

Theme::Theme(const QString &directory, QObject *parent, bool askHyprland)
    : QObject(parent),
      m_directory(directory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current" : directory),
      m_askHyprland(askHyprland) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);
    reload();
}

QString Theme::readKey(const QByteArray &toml, const QString &key) {
    const QRegularExpression expression("^\\s*" + QRegularExpression::escape(key) +
                                            "\\s*=\\s*[\"']([^\"']*)[\"']",
                                        QRegularExpression::MultilineOption);
    const auto match = expression.match(QString::fromUtf8(toml));
    return match.hasMatch() ? match.captured(1) : QString();
}

// Black or white, whichever reads on the accent (WCAG relative luminance).
QString Theme::onAccent() const {
    const QColor c(m_accent);
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    const double luminance =
        .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
    return luminance > .179 ? "#000000" : "#ffffff";
}

void Theme::flip() {
    m_override = !m_override;
    emit changed();
}

void Theme::reload() {
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    // A theme switch replaces symlinks and files, so watch the parents too and
    // re-arm after every change.
    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    QStringList candidates{ancestor, QFileInfo(ancestor).absolutePath(), m_directory, m_directory + "/theme",
                           m_directory + "/theme/colors.toml"};
    candidates.removeDuplicates();
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    QFile file(m_directory + "/theme/colors.toml");
    const QByteArray toml = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    const QString accent = readKey(toml, "accent");
    m_accent = QColor::isValidColorName(accent) && accent.startsWith('#') ? accent : "#2f6bed";
    // A new theme brings its own mode, so a manual flip ends with it.
    const bool themeDark = readKey(toml, "mode") != "light";
    if (themeDark != m_themeDark)
        m_override = false;
    m_themeDark = themeDark;

    // Rounding only changes with the theme or the user's config, so it is read
    // here rather than polled.
    if (m_askHyprland) {
        const auto value = QJsonDocument::fromJson(hyprland::request("j/getoption decoration:rounding"))
                               .object()
                               .value("int");
        if (value.isDouble() && value.toInt() >= 0)
            m_radius = value.toInt();
    }
    emit changed();
}
