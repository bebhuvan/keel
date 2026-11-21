import QtQuick

// A page that scrolls when the window is too short for it, with a thin
// mark at the right edge showing where you are. Its content is one column
// between the page gutters.
Flickable {
    id: page

    property int gutter: 32
    default property alias content: column.data

    contentWidth: width
    contentHeight: column.implicitHeight + 40
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    // Scrolls just enough to show the band from y to y + h.
    function reveal(y, h) {
        if (y < contentY)
            contentY = Math.max(0, y)
        else if (y + h > contentY + height)
            contentY = Math.min(contentHeight - height, y + h - height)
    }

    Column {
        id: column
        x: page.gutter
        width: page.width - page.gutter * 2
    }

    Rectangle {
        parent: page
        visible: page.visibleArea.heightRatio < 1
        x: page.width - 6
        y: page.visibleArea.yPosition * page.height
        width: 2
        height: page.visibleArea.heightRatio * page.height
        radius: 1
        color: theme.line
    }
}
