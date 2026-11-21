import QtQuick

// A row of ten blocks, ▰▰▰▱▱, in the accent with the empty part in the line
// color. Drawn as rectangles rather than text so it fits any width exactly,
// whatever font the system falls back to for block characters. When
// `interactive`, clicking or dragging along it picks a level.
Item {
    id: meter

    property real level: 0
    property bool interactive: false
    property color fill: theme.accent
    readonly property int blocks: 10
    readonly property int filled: Math.max(0, Math.min(blocks, Math.round(level * blocks)))

    signal picked(real level)

    implicitHeight: 6

    Row {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        spacing: 3
        Repeater {
            model: meter.blocks
            Rectangle {
                required property int index
                width: (meter.width - 3 * (meter.blocks - 1)) / meter.blocks
                height: 6
                radius: Math.min(1, theme.radius)
                color: index < meter.filled ? meter.fill : theme.line
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        anchors.margins: -6
        enabled: meter.interactive
        cursorShape: Qt.PointingHandCursor
        function pick(x) {
            meter.picked(Math.max(0, Math.min(1, Math.ceil((x - 6) / meter.width * meter.blocks) / meter.blocks)))
        }
        onPressed: mouse => pick(mouse.x)
        onPositionChanged: mouse => { if (pressed) pick(mouse.x) }
    }
}
