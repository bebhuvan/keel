import QtQuick

// A slider drawn as a hardware fader: a hairline track and a small cap.
// Clicking or dragging anywhere along it picks a level in 5% steps.
Item {
    id: fader

    property real level: 0
    property color fill: theme.foreground
    readonly property real clamped: Math.max(0, Math.min(1, level))

    signal picked(real level)

    implicitHeight: 16

    Rectangle {
        y: 7
        width: parent.width
        height: 2
        color: theme.line
    }
    Rectangle {
        y: 7
        width: cap.x + cap.width / 2
        height: 2
        color: fader.fill
    }
    Rectangle {
        id: cap
        x: Math.round((fader.width - width) * fader.clamped)
        width: 10
        height: 16
        radius: Math.min(2, theme.radius)
        color: fader.fill
    }
    MouseArea {
        anchors.fill: parent
        anchors.margins: -6
        cursorShape: Qt.PointingHandCursor
        function pick(x) {
            const level = (x - 6 - cap.width / 2) / (fader.width - cap.width)
            fader.picked(Math.max(0, Math.min(1, Math.round(level * 20) / 20)))
        }
        onPressed: mouse => pick(mouse.x)
        onPositionChanged: mouse => {
            if (pressed)
                pick(mouse.x)
        }
    }
}
