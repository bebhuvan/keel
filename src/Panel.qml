import QtQuick

// A quiet raised surface that groups related things. Its children stack in
// a column inside the padding; corners follow the window rounding.
Rectangle {
    id: panel

    property string title: ""
    property string aside: ""
    property bool titleClickable: false
    default property alias content: column.data

    signal titleClicked()

    implicitHeight: column.implicitHeight + 28
    color: theme.bar
    radius: Math.min(6, theme.radius)

    Column {
        id: column
        x: 14
        y: 14
        width: parent.width - 28
        spacing: 8

        Item {
            visible: panel.title !== ""
            width: parent.width
            height: 14
            MouseArea {
                anchors.fill: parent
                enabled: panel.titleClickable
                cursorShape: Qt.PointingHandCursor
                onClicked: panel.titleClicked()
            }
            PlainText { text: panel.title; color: theme.muted; font.pixelSize: 11; font.letterSpacing: 1 }
            PlainText { anchors.right: parent.right; text: panel.aside; color: theme.muted; font.pixelSize: 11 }
        }
    }
}
