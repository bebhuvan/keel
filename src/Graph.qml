import QtQuick

// The last two minutes as thin bars, newest on the right. Values are 0..1;
// a short history leaves the left side empty rather than stretching.
Item {
    id: graph

    property var values: []
    property int slots: 60

    Row {
        anchors.fill: parent
        spacing: 1
        Repeater {
            model: graph.slots
            Item {
                required property int index
                readonly property int offset: index - (graph.slots - graph.values.length)
                readonly property real value: offset >= 0 ? graph.values[offset] : 0
                width: (graph.width - (graph.slots - 1)) / graph.slots
                height: graph.height
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: Math.max(parent.value > 0 ? 1 : 0, parent.height * parent.value)
                    color: theme.accent
                }
            }
        }
    }
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: theme.line
    }
}
