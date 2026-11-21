import QtQuick

// An indicator lamp and its label: a filled dot when on, an empty ring when
// off. Switches, modes and choices all use it, so "on" looks the same
// everywhere. Clicking it does whatever `clicked` is wired to.
Item {
    id: lamp

    property bool on: false
    property string label: ""

    signal clicked()

    implicitWidth: 17 + text.implicitWidth
    implicitHeight: 20

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: 8
        height: 8
        radius: 4
        color: lamp.on ? theme.foreground : "transparent"
        border.width: lamp.on ? 0 : 1.5
        border.color: area.containsMouse ? theme.secondary : theme.muted
    }
    PlainText {
        id: text
        x: 17
        width: lamp.width - 17
        anchors.verticalCenter: parent.verticalCenter
        text: lamp.label
        color: lamp.on || area.containsMouse ? theme.foreground : theme.secondary
        elide: Text.ElideRight
    }
    MouseArea {
        id: area
        anchors.fill: parent
        anchors.margins: -4
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: lamp.clicked()
    }
}
