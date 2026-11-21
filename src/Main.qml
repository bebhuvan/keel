import QtQuick

Window {
    id: win

    width: 740
    height: 760
    minimumWidth: 640
    minimumHeight: 700
    visible: true
    title: "Keel"
    color: theme.background

    readonly property var controls: backend.controls
    readonly property int gutter: 24
    readonly property int contentWidth: width - gutter * 2

    // "overview", "details" or "storage".
    property string page: "overview"
    property bool helpVisible: false
    property bool searching: false
    property int selectedPid: 0
    property int storageIndex: 0
    // What the "are you sure?" question is about: "process" (a pid), "app"
    // (an index on the busiest list) or "trash"; empty when nothing is asked.
    property string confirmKind: ""
    property int confirmTarget: 0
    property string confirmName: ""
    readonly property bool asking: confirmKind !== ""
    // Single-letter keys act only when nothing else wants the keyboard.
    readonly property bool keysFree: !asking && !searching && !helpVisible

    onPageChanged: {
        backend.detailed = page === "details"
        if (page === "storage") {
            storage.open()
            storageIndex = 0
        }
    }

    readonly property var pages: ["overview", "details", "storage"]
    readonly property var switchRows: [
        { id: "wifi", label: "wi-fi", key: "w" },
        { id: "bluetooth", label: "bluetooth", key: "b" },
        { id: "dnd", label: "do not disturb", key: "d" },
        { id: "night", label: "night light", key: "n" },
        { id: "saver", label: "battery saver", key: "s" },
        { id: "awake", label: "stay awake", key: "a" }
    ]
    readonly property var modeRows: ["everyday", "focus", "travel", "night"]
    readonly property var modeHeadlines: ({
        everyday: "all calm.",
        focus: "heads down.",
        travel: "going the distance.",
        night: "winding down.",
        custom: "all calm."
    })
    readonly property var modeNotes: ({
        everyday: "everything on, nothing held back.",
        focus: "notifications held, bluetooth off, screen stays awake.",
        travel: "battery saver on, bluetooth off. wi-fi stays on.",
        night: "warm screen, notifications held until morning.",
        custom: "your own mix. pick a mode to reset it."
    })
    readonly property var displayRows: [
        { id: "both", label: "both screens" },
        { id: "monitor", label: "monitor only" },
        { id: "mirror", label: "mirror" }
    ]

    function clock() {
        return Qt.formatDateTime(new Date(), "ddd d MMM · HH:mm").toLowerCase()
    }

    function percent(level) {
        return level < 0 ? "—" : Math.round(level * 100) + "%"
    }

    function nextPage() {
        page = pages[(pages.indexOf(page) + 1) % pages.length]
    }

    function moveSelection(step) {
        if (page === "storage") {
            const count = storage.entries.length
            storageIndex = Math.max(0, Math.min(count - 1, storageIndex + step))
            storageList.positionViewAtIndex(storageIndex, ListView.Contain)
            return
        }
        const rows = backend.processes
        if (rows.length === 0)
            return
        let index = rows.findIndex(r => r.pid === win.selectedPid)
        index = index < 0 ? 0 : Math.max(0, Math.min(rows.length - 1, index + step))
        win.selectedPid = rows[index].pid
        processList.positionViewAtIndex(index, ListView.Contain)
    }

    function enterFolder(index) {
        const entry = storage.entries[index]
        if (!entry || !entry.directory)
            return
        storage.enter(index)
        storageIndex = 0
    }

    function leaveFolder() {
        storage.up()
        storageIndex = 0
    }

    function askToEndProcess(pid) {
        const row = backend.processes.find(r => r.pid === pid)
        if (!row)
            return
        win.selectedPid = pid
        win.confirmKind = "process"
        win.confirmTarget = pid
        win.confirmName = row.name + " (" + pid + ")"
    }

    function askToEndApp(index) {
        const app = backend.apps[index]
        if (!app)
            return
        win.confirmKind = "app"
        win.confirmTarget = index
        win.confirmName = app.name
    }

    function askToEmptyTrash(size) {
        win.confirmKind = "trash"
        win.confirmName = size
    }

    function answer(force) {
        if (win.confirmKind === "process")
            backend.endProcess(win.confirmTarget, force)
        else if (win.confirmKind === "app")
            backend.endApp(win.confirmTarget, force)
        else if (win.confirmKind === "trash" && !force)
            storage.emptyTrash()
        win.confirmKind = ""
    }

    function startSearch() {
        page = "details"
        win.searching = true
        searchInput.forceActiveFocus()
    }

    function stopSearch(keep) {
        win.searching = false
        if (!keep)
            backend.filter = ""
        searchInput.focus = false
    }

    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut
        onActivated: {
            if (win.asking) win.confirmKind = ""
            else if (win.searching || backend.filter !== "") win.stopSearch(false)
            else if (win.helpVisible) win.helpVisible = false
            else Qt.quit()
        } }
    Shortcut { sequence: "Q"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "?"; enabled: !win.asking && !win.searching; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Tab"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.nextPage() }
    Shortcut { sequence: "T"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: theme.flip() }
    Shortcut { sequence: "1"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.controls.applyMode("everyday") }
    Shortcut { sequence: "2"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.controls.applyMode("focus") }
    Shortcut { sequence: "3"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.controls.applyMode("travel") }
    Shortcut { sequence: "4"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.controls.applyMode("night") }
    Shortcut { sequence: "W"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.controls.toggle("wifi") }
    Shortcut { sequence: "B"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.controls.toggle("bluetooth") }
    Shortcut { sequence: "D"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.controls.toggle("dnd") }
    Shortcut { sequence: "N"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.controls.toggle("night") }
    Shortcut { sequence: "S"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.controls.toggle("saver") }
    Shortcut { sequence: "A"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.controls.toggle("awake") }
    Shortcut { sequence: "E"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.controls.cycleDisplay() }
    Shortcut { sequence: ","; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.controls.setBrightness(win.controls.brightness - 0.1) }
    Shortcut { sequence: "."; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.controls.setBrightness(win.controls.brightness + 0.1) }
    Shortcut { sequence: "-"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.controls.setVolume(win.controls.volume - 0.1) }
    Shortcut { sequences: ["=", "+"]; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.controls.setVolume(win.controls.volume + 0.1) }
    Shortcut { sequence: "M"; enabled: win.keysFree && win.page === "overview"; context: Qt.ApplicationShortcut; onActivated: win.controls.toggleMute() }
    Shortcut { sequence: "/"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.startSearch() }
    Shortcut { sequence: "C"; enabled: win.keysFree && win.page === "details"; context: Qt.ApplicationShortcut; onActivated: backend.sortByMemory = false }
    Shortcut { sequence: "M"; enabled: win.keysFree && win.page === "details"; context: Qt.ApplicationShortcut; onActivated: backend.sortByMemory = true }
    Shortcut { sequences: ["Down", "J"]; enabled: !win.asking && win.page !== "overview" && (win.keysFree || win.searching); context: Qt.ApplicationShortcut; onActivated: win.moveSelection(1) }
    Shortcut { sequences: ["Up", "K"]; enabled: !win.asking && win.page !== "overview" && (win.keysFree || win.searching); context: Qt.ApplicationShortcut; onActivated: win.moveSelection(-1) }
    Shortcut { sequences: ["X", "Delete"]; enabled: win.keysFree && win.page === "details"; context: Qt.ApplicationShortcut; onActivated: win.askToEndProcess(win.selectedPid) }
    Shortcut { sequences: ["Return", "Enter", "Right", "L"]; enabled: win.keysFree && win.page === "storage"; context: Qt.ApplicationShortcut; onActivated: win.enterFolder(win.storageIndex) }
    Shortcut { sequences: ["Backspace", "Left", "H"]; enabled: win.keysFree && win.page === "storage"; context: Qt.ApplicationShortcut; onActivated: win.leaveFolder() }
    Shortcut { sequence: "O"; enabled: win.keysFree && win.page === "storage"; context: Qt.ApplicationShortcut; onActivated: storage.reveal(win.storageIndex) }
    Shortcut { sequence: "R"; enabled: win.keysFree && win.page === "storage"; context: Qt.ApplicationShortcut; onActivated: storage.rescan() }
    Shortcut { sequence: "Y"; enabled: win.asking; context: Qt.ApplicationShortcut; onActivated: win.answer(false) }
    Shortcut { sequence: "F"; enabled: win.asking && win.confirmKind !== "trash"; context: Qt.ApplicationShortcut; onActivated: win.answer(true) }

    Timer { interval: 15000; running: true; repeat: true; onTriggered: clockText.text = win.clock() }

    Rectangle {
        anchors.fill: parent
        color: theme.background
        border.color: theme.accent
        border.width: 1

        // ── Top bar: mark, pages, clock ──────────────────────────────────
        Rectangle {
            id: topBar
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 }
            height: 42
            color: theme.bar

            Row {
                anchors { left: parent.left; leftMargin: 18; verticalCenter: parent.verticalCenter }
                spacing: 10
                // The mark, as in the app icon: a level waterline over a
                // hull and its keel.
                Canvas {
                    id: mark
                    width: 20
                    height: 18
                    anchors.verticalCenter: parent.verticalCenter
                    readonly property color ink: theme.accent
                    readonly property color line: theme.foreground
                    onInkChanged: requestPaint()
                    onLineChanged: requestPaint()
                    onPaint: {
                        const g = getContext("2d")
                        g.reset()
                        g.fillStyle = line
                        g.fillRect(0, 2, 20, 2)
                        g.fillStyle = ink
                        g.beginPath()
                        g.moveTo(2, 7); g.lineTo(18, 7); g.lineTo(14.5, 12); g.lineTo(5.5, 12)
                        g.closePath(); g.fill()
                        g.beginPath()
                        g.moveTo(9, 12); g.lineTo(11, 12); g.lineTo(10, 18)
                        g.closePath(); g.fill()
                    }
                }
                Text { text: "keel"; color: theme.foreground; font.pixelSize: 13; font.weight: Font.Medium; anchors.verticalCenter: parent.verticalCenter }
            }

            Row {
                anchors.centerIn: parent
                spacing: 22
                Repeater {
                    model: win.pages
                    Item {
                        required property string modelData
                        readonly property bool active: win.page === modelData
                        width: tabText.implicitWidth
                        height: topBar.height
                        Text {
                            id: tabText
                            anchors.centerIn: parent
                            text: modelData
                            color: active ? theme.foreground : tabArea.containsMouse ? theme.secondary : theme.muted
                            font.pixelSize: 12
                        }
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 2
                            color: active ? theme.accent : "transparent"
                        }
                        MouseArea { id: tabArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: win.page = modelData }
                    }
                }
            }

            Row {
                anchors { right: parent.right; rightMargin: 18; verticalCenter: parent.verticalCenter }
                spacing: 16
                Text { id: clockText; text: win.clock(); color: theme.muted; font.pixelSize: 12 }
                Text {
                    text: theme.dark ? "◑" : "◐"
                    color: themeArea.containsMouse ? theme.accent : theme.secondary
                    font.pixelSize: 14
                    MouseArea { id: themeArea; anchors.fill: parent; anchors.margins: -4; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: theme.flip() }
                }
            }
            Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: theme.line }
        }

        // ── Overview ─────────────────────────────────────────────────────
        Column {
            id: overview
            visible: win.page === "overview"
            x: win.gutter
            y: topBar.height + 22
            width: win.contentWidth
            spacing: 0

            Item {
                width: parent.width
                height: headlineColumn.implicitHeight
                Column {
                    id: headlineColumn
                    width: parent.width - hero.width - 16
                    Text {
                        text: "mode: " + win.controls.mode
                              + (win.controls.network !== "" ? "  ·  on " + win.controls.network.toLowerCase() : "")
                        color: theme.muted
                        font.pixelSize: 11
                        font.letterSpacing: 0.5
                    }
                    Text {
                        topPadding: 8
                        bottomPadding: 6
                        width: parent.width
                        text: backend.calm ? win.modeHeadlines[win.controls.mode] : backend.headline
                        color: backend.calm ? theme.foreground : theme.warn
                        font.pixelSize: 36
                        font.weight: Font.Medium
                        font.letterSpacing: -0.5
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: backend.line
                        color: theme.secondary
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                    }
                }
                // The one number most people want: how long the battery lasts.
                Column {
                    id: hero
                    anchors { right: parent.right; bottom: parent.bottom; bottomMargin: 2 }
                    readonly property var battery: backend.meters[0] || ({ word: "", detail: "" })
                    Text {
                        anchors.right: parent.right
                        text: hero.battery.word === "plugged in" || hero.battery.word === "charging" ? hero.battery.detail : hero.battery.word
                        color: theme.accent
                        font.pixelSize: 28
                        font.weight: Font.Light
                    }
                    Text {
                        anchors.right: parent.right
                        text: hero.battery.word === "plugged in" ? "charged" : hero.battery.word === "charging" ? "charging" : "left"
                        color: theme.muted
                        font.pixelSize: 11
                    }
                }
            }

            Item { width: 1; height: 24 }

            Row {
                spacing: 20
                Repeater {
                    model: 4
                    Column {
                        required property int index
                        readonly property var m: backend.meters[index] || ({ label: "", level: 0, word: "", detail: "" })
                        width: (win.contentWidth - 20 * 3) / 4
                        spacing: 5
                        Text { text: m.label; color: theme.muted; font.pixelSize: 11; font.letterSpacing: 0.5 }
                        Meter { width: parent.width; height: 10; level: m.level; fill: index === 3 && m.level > 0.8 ? theme.warn : theme.accent }
                        Row {
                            spacing: 8
                            Text { text: m.word; color: theme.foreground; font.pixelSize: 14 }
                            Text { text: m.detail; color: theme.muted; font.pixelSize: 11; anchors.baseline: parent.children[0].baseline }
                        }
                    }
                }
            }

            Item { width: 1; height: 22 }

            Rectangle {
                width: parent.width
                height: 50
                color: "transparent"
                border.color: theme.line
                radius: Math.min(6, theme.radius)
                clip: true
                Row {
                    anchors.fill: parent
                    anchors.margins: 1
                    Repeater {
                        model: win.modeRows
                        Rectangle {
                            required property string modelData
                            required property int index
                            readonly property bool active: win.controls.mode === modelData
                            width: parent.width / 4
                            height: parent.height
                            color: active ? theme.accent : modeArea.containsMouse ? theme.bar : "transparent"
                            Row {
                                anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
                                spacing: 10
                                Text { text: index + 1; color: active ? theme.onAccent : theme.muted; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                                Text { text: modelData; color: active ? theme.onAccent : theme.foreground; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                            }
                            Rectangle { visible: index > 0; width: 1; height: parent.height; color: theme.line }
                            MouseArea {
                                id: modeArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: win.controls.applyMode(modelData)
                            }
                        }
                    }
                }
            }
            Text {
                topPadding: 8
                bottomPadding: 14
                text: win.modeNotes[win.controls.mode] || ""
                color: theme.muted
                font.pixelSize: 12
            }

            Panel {
                width: parent.width
                Grid {
                    width: parent.width
                    columns: 3
                    rowSpacing: 8
                    Repeater {
                        model: win.switchRows
                        Item {
                            required property var modelData
                            readonly property bool on: win.controls.states[modelData.id] === true
                            width: (win.contentWidth - 28) / 3
                            height: 20
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - 8
                                elide: Text.ElideRight
                                text: "<font color='" + (on ? theme.good : theme.muted) + "'>" + (on ? "●" : "○")
                                      + "</font>&nbsp; " + modelData.label + " <font color='" + theme.muted + "'>"
                                      + modelData.key + "</font>"
                                textFormat: Text.StyledText
                                color: on ? theme.foreground : theme.muted
                                font.pixelSize: 13
                            }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: win.controls.toggle(modelData.id) }
                        }
                    }
                }

                Rectangle { width: parent.width; height: 1; color: theme.line }

                Row {
                    spacing: 20
                    Repeater {
                        model: [
                            { label: "brightness", level: win.controls.brightness, kind: "brightness" },
                            { label: win.controls.muted ? "volume · muted" : "volume", level: win.controls.volume, kind: "volume" }
                        ]
                        Item {
                            required property var modelData
                            width: (win.contentWidth - 28 - 20) / 2
                            height: 22
                            Text { id: levelLabel; width: 110; anchors.verticalCenter: parent.verticalCenter; text: modelData.label; color: theme.secondary; font.pixelSize: 13 }
                            Meter {
                                anchors { left: levelLabel.right; verticalCenter: parent.verticalCenter }
                                width: parent.width - 110 - 48
                                height: 14
                                interactive: true
                                level: modelData.level
                                fill: modelData.kind === "volume" && win.controls.muted ? theme.muted : theme.accent
                                onPicked: level => modelData.kind === "brightness" ? win.controls.setBrightness(level)
                                                                                     : win.controls.setVolume(level)
                            }
                            Text {
                                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                text: win.percent(modelData.level)
                                color: theme.foreground
                                font.pixelSize: 12
                            }
                        }
                    }
                }

                Item {
                    width: parent.width
                    height: 24
                    Text { id: screensLabel; width: 110; anchors.verticalCenter: parent.verticalCenter; text: "screens"; color: theme.secondary; font.pixelSize: 13 }
                    Text {
                        visible: win.controls.display === "laptop"
                        anchors { left: screensLabel.right; verticalCenter: parent.verticalCenter }
                        text: "laptop only. plug in a monitor to choose."
                        color: theme.muted
                        font.pixelSize: 12
                    }
                    Row {
                        visible: win.controls.display !== "laptop"
                        anchors { left: screensLabel.right; verticalCenter: parent.verticalCenter }
                        spacing: 6
                        Repeater {
                            model: win.displayRows
                            Rectangle {
                                required property var modelData
                                readonly property bool active: win.controls.display === modelData.id
                                width: choiceText.implicitWidth + 18
                                height: 22
                                radius: Math.min(4, theme.radius)
                                color: active ? theme.accent : choiceArea.containsMouse ? theme.background : "transparent"
                                border.color: active ? theme.accent : theme.line
                                Text {
                                    id: choiceText
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: active ? theme.onAccent : theme.foreground
                                    font.pixelSize: 12
                                }
                                MouseArea { id: choiceArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: win.controls.setDisplay(modelData.id) }
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            leftPadding: 6
                            text: win.controls.monitor + "  e"
                            color: theme.muted
                            font.pixelSize: 11
                        }
                    }
                }

                Text {
                    visible: win.controls.error !== ""
                    text: win.controls.error
                    color: theme.warn
                    font.pixelSize: 12
                }
            }

            Item { width: 1; height: 14 }

            Panel {
                width: parent.width
                title: "busiest right now"
                aside: "quit closes politely · end asks first"
                Repeater {
                    model: 3
                    Item {
                        required property int index
                        readonly property var app: backend.apps[index]
                        visible: app !== undefined
                        width: win.contentWidth - 28
                        height: 22
                        Text {
                            id: appName
                            width: 150
                            anchors.verticalCenter: parent.verticalCenter
                            text: app ? app.name : ""
                            color: theme.foreground
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                        Meter {
                            anchors { left: appName.right; leftMargin: 8; verticalCenter: parent.verticalCenter }
                            width: 140
                            height: 10
                            level: app ? Math.max(app.level, app.level > 0.001 ? 0.1 : 0) : 0
                        }
                        Text {
                            anchors { right: actionText.left; rightMargin: 16; verticalCenter: parent.verticalCenter }
                            text: app ? app.detail : ""
                            color: theme.muted
                            font.pixelSize: 12
                        }
                        // Apps with a window close politely, like their close
                        // button; background programs are ended, after asking.
                        Text {
                            id: actionText
                            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                            width: 36
                            horizontalAlignment: Text.AlignRight
                            text: app ? (app.canQuit ? "quit" : "end") : ""
                            color: actionArea.containsMouse ? theme.accent : theme.secondary
                            font.pixelSize: 12
                            font.underline: actionArea.containsMouse
                            MouseArea {
                                id: actionArea
                                anchors.fill: parent
                                anchors.margins: -4
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: app.canQuit ? backend.quitApp(index) : win.askToEndApp(index)
                            }
                        }
                    }
                }
            }
        }

        // ── Details ──────────────────────────────────────────────────────
        Item {
            id: details
            visible: win.page === "details"
            anchors { left: parent.left; right: parent.right; top: topBar.bottom; bottom: bottomBar.top; margins: win.gutter; topMargin: 20 }

            Column {
                id: detailTop
                width: parent.width
                spacing: 12

                Panel {
                    width: parent.width
                    title: "processor · last 2 minutes"
                    aside: "each core now"
                    Row {
                        spacing: 16
                        Graph { width: (details.width - 28 - 16) * 0.62; height: 40; values: backend.cpuHistory }
                        Row {
                            id: coreRow
                            spacing: 2
                            readonly property real available: (details.width - 28 - 16) * 0.38
                            readonly property int count: Math.max(1, backend.cores.length)
                            Repeater {
                                model: backend.cores.length
                                Rectangle {
                                    required property int index
                                    width: Math.max(2, (coreRow.available - 2 * (coreRow.count - 1)) / coreRow.count)
                                    height: 40
                                    color: theme.line
                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        width: parent.width
                                        height: Math.max(1, parent.height * (backend.cores[index] || 0) / 100)
                                        color: theme.accent
                                    }
                                }
                            }
                        }
                    }
                }

                Grid {
                    columns: 3
                    columnSpacing: 12
                    rowSpacing: 12
                    Repeater {
                        model: backend.sections.length
                        Rectangle {
                            required property int index
                            readonly property var section: backend.sections[index]
                            width: (details.width - 24) / 3
                            height: 136
                            color: theme.bar
                            radius: Math.min(6, theme.radius)
                            Column {
                                x: 12; y: 10
                                width: parent.width - 24
                                spacing: 3
                                Text { text: section.title; color: theme.accent; font.pixelSize: 11; font.letterSpacing: 1; bottomPadding: 3 }
                                Repeater {
                                    model: section.rows.length
                                    Item {
                                        required property int index
                                        readonly property var row: parent.parent.section.rows[index]
                                        width: parent.width
                                        height: 17
                                        Text { id: rowLabel; text: row.label; color: theme.muted; font.pixelSize: 12 }
                                        Text {
                                            anchors.right: parent.right
                                            width: parent.width - rowLabel.implicitWidth - 10
                                            horizontalAlignment: Text.AlignRight
                                            elide: Text.ElideLeft
                                            text: row.value
                                            color: theme.foreground
                                            font.pixelSize: 12
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Row {
                    spacing: 12
                    Repeater {
                        model: [
                            { label: "network down", peak: backend.downPeak, values: backend.downHistory },
                            { label: "network up", peak: backend.upPeak, values: backend.upHistory }
                        ]
                        Panel {
                            required property var modelData
                            width: (details.width - 12) / 2
                            title: modelData.label
                            aside: "peak " + modelData.peak
                            Graph { width: parent.width; height: 22; values: modelData.values }
                        }
                    }
                }

                Item {
                    width: parent.width
                    height: 22
                    Text {
                        id: searchPrompt
                        anchors.verticalCenter: parent.verticalCenter
                        text: win.searching || backend.filter !== "" ? "find /" : "processes"
                        color: win.searching ? theme.accent : theme.muted
                        font.pixelSize: win.searching || backend.filter !== "" ? 12 : 11
                        font.letterSpacing: 1
                    }
                    TextInput {
                        id: searchInput
                        visible: win.searching || backend.filter !== ""
                        anchors { left: searchPrompt.right; leftMargin: 8; right: sortHint.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
                        text: backend.filter
                        color: theme.foreground
                        selectionColor: theme.accent
                        selectedTextColor: theme.onAccent
                        font.pixelSize: 12
                        clip: true
                        onTextEdited: {
                            backend.filter = text
                            const first = backend.processes[0]
                            win.selectedPid = first ? first.pid : 0
                        }
                        Keys.onReturnPressed: win.stopSearch(true)
                        Keys.onEnterPressed: win.stopSearch(true)
                        Text {
                            visible: searchInput.text === ""
                            text: "type part of a name or a pid"
                            color: theme.muted
                            font.pixelSize: 12
                        }
                    }
                    Text {
                        id: sortHint
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        text: "<font color='" + (backend.sortByMemory ? theme.muted : theme.accent) + "'>c cpu</font>  <font color='"
                              + (backend.sortByMemory ? theme.accent : theme.muted) + "'>m memory</font>  ·  / find  ·  x end"
                        textFormat: Text.StyledText
                        color: theme.muted
                        font.pixelSize: 11
                    }
                }
            }

            ListView {
                id: processList
                anchors { left: parent.left; right: parent.right; top: detailTop.bottom; bottom: parent.bottom; topMargin: 2 }
                clip: true
                model: backend.processes.length
                boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    id: processRow
                    required property int index
                    readonly property var process: backend.processes[index] || ({ pid: 0, name: "", cpu: "", memory: "" })
                    readonly property bool selected: process.pid === win.selectedPid
                    width: processList.width
                    height: 22
                    color: selected || rowArea.containsMouse ? theme.bar : "transparent"
                    Rectangle { visible: processRow.selected; width: 2; height: parent.height; color: theme.accent }
                    Text { x: 10; width: 70; anchors.verticalCenter: parent.verticalCenter; text: processRow.process.pid; color: theme.muted; font.pixelSize: 12 }
                    Text {
                        x: 80; width: parent.width - 300; anchors.verticalCenter: parent.verticalCenter
                        text: processRow.process.name; color: theme.foreground; font.pixelSize: 12; elide: Text.ElideRight
                    }
                    Text {
                        anchors { right: parent.right; rightMargin: 150; verticalCenter: parent.verticalCenter }
                        text: processRow.process.cpu
                        color: backend.sortByMemory ? theme.secondary : theme.foreground
                        font.pixelSize: 12
                    }
                    Text {
                        anchors { right: parent.right; rightMargin: 56; verticalCenter: parent.verticalCenter }
                        text: processRow.process.memory
                        color: backend.sortByMemory ? theme.foreground : theme.secondary
                        font.pixelSize: 12
                    }
                    MouseArea {
                        id: rowArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: win.selectedPid = processRow.process.pid
                        onDoubleClicked: win.askToEndProcess(processRow.process.pid)
                    }
                    Text {
                        anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                        visible: processRow.selected || rowArea.containsMouse
                        text: "end"
                        color: endArea.containsMouse ? theme.warn : theme.secondary
                        font.pixelSize: 12
                        font.underline: endArea.containsMouse
                        MouseArea {
                            id: endArea
                            anchors.fill: parent
                            anchors.margins: -4
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: win.askToEndProcess(processRow.process.pid)
                        }
                    }
                }
                Text {
                    visible: backend.processes.length === 0 && backend.filter !== ""
                    topPadding: 12
                    leftPadding: 10
                    text: "nothing called “" + backend.filter + "” is running."
                    color: theme.muted
                    font.pixelSize: 12
                }
            }
        }

        // ── Storage ──────────────────────────────────────────────────────
        Item {
            id: storagePage
            visible: win.page === "storage"
            anchors { left: parent.left; right: parent.right; top: topBar.bottom; bottom: bottomBar.top; margins: win.gutter; topMargin: 22 }

            Column {
                id: storageTop
                width: parent.width
                spacing: 0

                Item {
                    width: parent.width
                    height: 34
                    Text { anchors.bottom: parent.bottom; text: storage.usedText; color: theme.foreground; font.pixelSize: 24; font.weight: Font.Medium; font.letterSpacing: -0.5 }
                    Text { anchors { right: parent.right; bottom: parent.bottom; bottomMargin: 3 } text: storage.freeText; color: theme.accent; font.pixelSize: 15 }
                }
                Item { width: 1; height: 10 }
                Rectangle {
                    width: parent.width
                    height: 8
                    radius: Math.min(4, theme.radius)
                    color: theme.line
                    Rectangle {
                        width: parent.width * storage.usedFraction
                        height: parent.height
                        radius: parent.radius
                        color: storage.usedFraction > 0.95 ? theme.warn : theme.accent
                    }
                }
                Text {
                    topPadding: 8
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: (storage.status !== "" ? storage.status + (storage.scanning ? "" : "  ·  r rescan") : "")
                          + (storage.compressed ? "  ·  this disk compresses files, so sizes below add up to more than it holds." : "")
                    color: theme.muted
                    font.pixelSize: 11
                }

                Item { width: 1; height: 16 }

                Panel {
                    width: parent.width
                    title: "easy wins"
                    aside: "the usual places space piles up"
                    Repeater {
                        model: storage.spots.length
                        Item {
                            required property int index
                            readonly property var spot: storage.spots[index]
                            width: storagePage.width - 28
                            height: 22
                            Text { id: spotLabel; width: 120; anchors.verticalCenter: parent.verticalCenter; text: spot.label; color: theme.foreground; font.pixelSize: 13 }
                            Text { id: spotSize; x: 120; width: 90; anchors.verticalCenter: parent.verticalCenter; text: spot.size; color: theme.accent; font.pixelSize: 13 }
                            Text {
                                x: 214
                                width: parent.width - 214 - spotActions.width - 12
                                anchors.verticalCenter: parent.verticalCenter
                                text: spot.note
                                color: theme.muted
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                            Row {
                                id: spotActions
                                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                spacing: 14
                                Repeater {
                                    model: spot.action === "empty" ? ["open", "empty"] : ["open"]
                                    Text {
                                        required property string modelData
                                        text: modelData
                                        color: spotArea.containsMouse ? (modelData === "empty" ? theme.warn : theme.accent) : theme.secondary
                                        font.pixelSize: 12
                                        font.underline: spotArea.containsMouse
                                        MouseArea {
                                            id: spotArea
                                            anchors.fill: parent
                                            anchors.margins: -4
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: modelData === "empty" ? win.askToEmptyTrash(spot.size) : storage.revealSpot(index)
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Text {
                        visible: storage.spots.length === 0
                        text: storage.scanning ? "measuring…" : "nothing measured yet. press r to scan."
                        color: theme.muted
                        font.pixelSize: 12
                    }
                }

                Item { width: 1; height: 16 }

                Item {
                    width: parent.width
                    height: 18
                    Text {
                        text: "biggest in " + storage.place
                        color: theme.muted
                        font.pixelSize: 11
                        font.letterSpacing: 1
                    }
                    Text {
                        anchors.right: parent.right
                        text: (storage.atTop ? "" : "⌫ back  ·  ") + "enter go in  ·  o open"
                        color: theme.muted
                        font.pixelSize: 11
                    }
                }
            }

            ListView {
                id: storageList
                anchors { left: parent.left; right: parent.right; top: storageTop.bottom; bottom: parent.bottom; topMargin: 6 }
                clip: true
                model: storage.entries.length
                boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    id: entryRow
                    required property int index
                    readonly property var entry: storage.entries[index] || ({ name: "", size: "", fraction: 0, directory: false, other: false })
                    readonly property bool selected: index === win.storageIndex
                    width: storageList.width
                    height: 24
                    color: selected || entryArea.containsMouse ? theme.bar : "transparent"
                    Rectangle { visible: entryRow.selected; width: 2; height: parent.height; color: theme.accent }
                    Text {
                        x: 12
                        width: parent.width * 0.42
                        anchors.verticalCenter: parent.verticalCenter
                        text: entryRow.entry.name + (entryRow.entry.directory ? "/" : "")
                        color: entryRow.entry.other ? theme.muted : theme.foreground
                        font.pixelSize: 13
                        elide: Text.ElideMiddle
                    }
                    Rectangle {
                        x: parent.width * 0.45
                        width: parent.width * 0.32
                        height: 6
                        anchors.verticalCenter: parent.verticalCenter
                        radius: Math.min(3, theme.radius)
                        color: theme.line
                        Rectangle {
                            width: Math.max(2, parent.width * entryRow.entry.fraction)
                            height: parent.height
                            radius: parent.radius
                            color: entryRow.entry.other ? theme.muted : theme.accent
                        }
                    }
                    Text {
                        anchors { right: parent.right; rightMargin: 56; verticalCenter: parent.verticalCenter }
                        text: entryRow.entry.size
                        color: theme.foreground
                        font.pixelSize: 12
                    }
                    MouseArea {
                        id: entryArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: win.storageIndex = entryRow.index
                        onDoubleClicked: win.enterFolder(entryRow.index)
                    }
                    Text {
                        anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                        visible: !entryRow.entry.other && (entryRow.selected || entryArea.containsMouse)
                        text: "open"
                        color: openArea.containsMouse ? theme.accent : theme.secondary
                        font.pixelSize: 12
                        font.underline: openArea.containsMouse
                        MouseArea {
                            id: openArea
                            anchors.fill: parent
                            anchors.margins: -4
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: storage.reveal(entryRow.index)
                        }
                    }
                }
                Text {
                    visible: storage.entries.length === 0
                    topPadding: 12
                    leftPadding: 12
                    text: storage.scanning ? "measuring. the first scan takes a little while; after that it opens instantly."
                                           : "nothing here is big enough to list."
                    color: theme.muted
                    font.pixelSize: 12
                }
            }
        }

        Rectangle {
            id: bottomBar
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 1 }
            height: 30
            color: theme.bar
            Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top } height: 1; color: theme.line }
            Text {
                anchors { left: parent.left; leftMargin: 18; verticalCenter: parent.verticalCenter }
                text: win.searching ? "type to find · ↑↓ pick · enter done · esc clear"
                    : win.page === "details" ? "tab next page · / find · ↑↓ pick · x end"
                    : win.page === "storage" ? "tab next page · ↑↓ pick · enter go in · ⌫ back · r rescan"
                    : "1–4 modes · letters flip switches · tab next page"
                color: theme.muted
                font.pixelSize: 11
            }
            Text {
                anchors { right: parent.right; rightMargin: 18; verticalCenter: parent.verticalCenter }
                text: "? keys · esc close"
                color: theme.muted
                font.pixelSize: 11
            }
        }

        // Ending or deleting can't be taken back, so it asks first.
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            visible: win.asking
            color: Qt.rgba(0, 0, 0, theme.dark ? 0.55 : 0.25)
            MouseArea { anchors.fill: parent; onClicked: win.confirmKind = "" }
            Rectangle {
                anchors.centerIn: parent
                width: 440
                height: 150
                color: theme.background
                border.color: theme.warn
                radius: Math.min(6, theme.radius)
                MouseArea { anchors.fill: parent }
                Column {
                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 26 }
                    spacing: 8
                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: win.confirmKind === "trash" ? "empty the trash?" : "end " + win.confirmName + "?"
                        color: theme.foreground
                        font.pixelSize: 16
                    }
                    Text {
                        text: win.confirmKind === "trash" ? "this deletes " + win.confirmName + " for good. it can't be undone."
                                                          : "anything unsaved in it may be lost."
                        color: theme.secondary
                        font.pixelSize: 12
                    }
                    Item { width: 1; height: 6 }
                    Row {
                        spacing: 18
                        Repeater {
                            model: win.confirmKind === "trash"
                                   ? [{ label: "y  empty it", force: false, cancel: false }, { label: "esc  keep it", force: false, cancel: true }]
                                   : [{ label: "y  end it", force: false, cancel: false }, { label: "f  force", force: true, cancel: false },
                                      { label: "esc  keep it", force: false, cancel: true }]
                            Text {
                                required property var modelData
                                text: modelData.label
                                color: choiceArea.containsMouse ? theme.accent : modelData.force ? theme.warn : theme.secondary
                                font.pixelSize: 12
                                MouseArea {
                                    id: choiceArea
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: modelData.cancel ? win.confirmKind = "" : win.answer(modelData.force)
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            visible: win.helpVisible
            color: theme.background
            MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }
            Column {
                anchors.centerIn: parent
                spacing: 5
                Text { text: "keys"; color: theme.accent; font.pixelSize: 13; font.letterSpacing: 1; bottomPadding: 8 }
                Repeater {
                    model: [
                        ["tab", "overview → details → storage"],
                        ["1 2 3 4", "everyday · focus · travel · night"],
                        ["w b d n s a", "wi-fi · bluetooth · do not disturb · night light · battery saver · stay awake"],
                        ["e", "screens: both · monitor only · mirror"],
                        [", .   - =", "brightness · volume"],
                        ["m", "mute (overview) · sort by memory (details)"],
                        ["/", "find a process"],
                        ["↑ ↓  j k", "pick a process or folder"],
                        ["x  del", "end the picked process"],
                        ["c", "sort processes by cpu"],
                        ["enter  ⌫", "into a folder · back out (storage)"],
                        ["o  r", "open in files · rescan (storage)"],
                        ["t", "light / dark for now"],
                        ["esc  q", "close keel"]
                    ]
                    Row {
                        required property var modelData
                        Text { width: 130; text: modelData[0]; color: theme.foreground; font.pixelSize: 13 }
                        Text { text: modelData[1]; color: theme.secondary; font.pixelSize: 13 }
                    }
                }
            }
        }
    }
}
