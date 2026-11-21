import QtQuick

// One row of the page grid: a hairline across, a short label in the left
// column and the content to its right. Every page is a stack of these, so
// everything starts on the same two edges.
Item {
    id: section

    // Width of the label column; content starts here on every page.
    readonly property int column: 120
    property string label: ""
    property string note: ""
    property bool rule: true
    property int spacing: 12
    default property alias content: body.data
    // Extra things under the label, such as a small switch between views.
    property alias side: sideColumn.data

    width: parent ? parent.width : 0
    implicitHeight: Math.max(body.implicitHeight, labels.implicitHeight) + 30

    Rectangle {
        visible: section.rule
        width: parent.width
        height: 1
        color: theme.line
    }
    Column {
        id: labels
        y: 16
        width: section.column - 16
        spacing: 4
        PlainText {
            width: parent.width
            text: section.label
            color: theme.secondary
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
        PlainText {
            width: parent.width
            visible: text !== ""
            text: section.note
            color: theme.muted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
        Column {
            id: sideColumn
            topPadding: 6
            spacing: 6
        }
    }
    Column {
        id: body
        x: section.column
        y: 15
        width: parent.width - section.column
        spacing: section.spacing
    }
}
