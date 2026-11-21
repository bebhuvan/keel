import QtQuick

// A level drawn like an instrument dial laid flat: a thin track, the filled
// part in ink (or the attention colour), and tick marks every tenth with
// longer ones at the ends and the middle.
Item {
    id: gauge

    property real level: 0
    property color fill: theme.foreground
    property bool ticks: true

    implicitHeight: ticks ? 9 : 2

    Rectangle {
        width: parent.width
        height: 2
        color: theme.line
    }
    Rectangle {
        width: Math.round(parent.width * Math.max(0, Math.min(1, gauge.level)))
        height: 2
        color: gauge.fill
    }
    Repeater {
        model: gauge.ticks ? 11 : 0
        Rectangle {
            required property int index
            x: Math.round((gauge.width - 1) * index / 10)
            y: 4
            width: 1
            height: index % 5 === 0 ? 5 : 3
            color: theme.muted
        }
    }
}
