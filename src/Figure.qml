import QtQuick

// A number set large with its unit small beside it, on one baseline:
// "81 °C", "21.3 / 31 GB". Display sizes use Inter's Display cut, drawn
// for large sizes.
Row {
    id: figure

    property string value: ""
    property string unit: ""
    property string word: ""
    property int size: 26
    property color color: theme.foreground

    spacing: 0

    PlainText {
        id: number
        text: figure.value
        color: figure.color
        font.family: "Inter Display"
        font.weight: Font.Medium
        font.pixelSize: figure.size
        font.letterSpacing: -figure.size * 0.025
    }
    PlainText {
        anchors.baseline: number.baseline
        visible: text !== ""
        leftPadding: figure.unit.startsWith("%") ? 1 : 4
        text: figure.unit
        color: figure.color
        font.pixelSize: Math.round(figure.size * 0.5)
    }
    PlainText {
        anchors.baseline: number.baseline
        visible: text !== ""
        leftPadding: 8
        text: figure.word
        color: theme.secondary
        font.pixelSize: 12
    }
}
