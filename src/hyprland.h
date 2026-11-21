#pragma once

#include <QByteArray>
#include <QString>

// One request over Hyprland's control socket, the same channel hyprctl uses,
// without starting a process. Returns an empty reply when Hyprland is not
// running or does not answer within a fraction of a second.
namespace hyprland {
QByteArray request(const QByteArray &command);

// Closes a window the way its close button would, so the app can ask to
// save. Accepts only a Hyprland address (0x and hex digits), because the
// command is a line of Lua and nothing else may reach it.
bool closeWindow(const QString &address);
bool isAddress(const QString &address);
} // namespace hyprland
