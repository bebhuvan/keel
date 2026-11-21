import QtQuick

// The last two minutes as one inked line, newest at the right edge, with a
// dot on the latest value. Values are 0..1; a short history starts partway
// across rather than stretching. With `grid`, faint rules mark 0, half and
// full; without, only the baseline.
Item {
    id: trace

    property var values: []
    property int slots: 60
    property color ink: theme.foreground
    property bool grid: true

    onValuesChanged: canvas.requestPaint()
    onInkChanged: canvas.requestPaint()
    onGridChanged: canvas.requestPaint()

    Connections {
        target: theme
        function onChanged() {
            canvas.requestPaint()
        }
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const g = getContext("2d")
            g.reset()
            const w = width, h = height
            g.fillStyle = theme.line
            for (const f of trace.grid ? [0, 0.5, 1] : [0])
                g.fillRect(0, Math.round((h - 1) * (1 - f)), w, 1)

            const v = trace.values
            const n = Math.min(v.length, trace.slots)
            if (n < 2)
                return
            const right = w - 3
            const step = right / (trace.slots - 1)
            const x = i => right - (n - 1 - i) * step
            const y = i => 2 + (h - 4) * (1 - Math.max(0, Math.min(1, v[v.length - n + i])))

            g.beginPath()
            g.moveTo(x(0), y(0))
            for (let i = 1; i < n; i++)
                g.lineTo(x(i), y(i))
            g.lineWidth = 1.5
            g.lineJoin = "round"
            g.strokeStyle = trace.ink
            g.stroke()

            g.lineTo(x(n - 1), h)
            g.lineTo(x(0), h)
            g.closePath()
            g.globalAlpha = 0.06
            g.fillStyle = trace.ink
            g.fill()

            g.globalAlpha = 1
            g.beginPath()
            g.arc(x(n - 1), y(n - 1), 2.5, 0, 2 * Math.PI)
            g.fill()
        }
    }
}
