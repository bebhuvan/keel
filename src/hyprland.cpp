#include "hyprland.h"

#include <QLocalSocket>
#include <QRegularExpression>
#include <QString>

namespace hyprland {

QByteArray request(const QByteArray &command) {
    const QString signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    if (signature.isEmpty())
        return {};
    QLocalSocket socket;
    socket.connectToServer(qEnvironmentVariable("XDG_RUNTIME_DIR") + "/hypr/" + signature + "/.socket.sock");
    if (!socket.waitForConnected(150))
        return {};
    socket.write(command);
    socket.flush();
    // Hyprland answers and then closes the connection.
    QByteArray reply;
    while (socket.waitForReadyRead(150))
        reply += socket.readAll();
    reply += socket.readAll();
    return reply;
}

bool isAddress(const QString &address) {
    static const QRegularExpression valid("^0x[0-9a-fA-F]{1,16}$");
    return valid.match(address).hasMatch();
}

bool closeWindow(const QString &address) {
    if (!isAddress(address))
        return false;
    // Hyprland with a Lua config takes Lua dispatches; older releases take the
    // classic form and answer the Lua one with an error, so try both.
    QByteArray reply =
        request("dispatch hl.dsp.window.close({ window = \"address:" + address.toLatin1() + "\" })");
    if (reply.trimmed() == "ok")
        return true;
    reply = request("dispatch closewindow address:" + address.toLatin1());
    return reply.trimmed() == "ok";
}

} // namespace hyprland
