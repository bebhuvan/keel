import QtQuick

Window {
    id: win

    width: 760
    height: 780
    minimumWidth: 660
    minimumHeight: 520
    visible: true
    title: "Keel"
    color: theme.background

    readonly property var controls: backend.controls
    readonly property int gutter: 32
    readonly property int contentWidth: width - gutter * 2
    // The grid's label column; every page's content starts at this edge.
    readonly property int column: 120
    readonly property int rowHeight: 30

    // "overview", "details" or "storage".
    property string page: "overview"
    property bool helpVisible: false
    property bool showHeavy: false
    property bool searching: false
    property int selectedPid: 0
    property int storageIndex: 0
    // Snapshot of the busiest apps frozen while the pointer hovers the panel,
    // so a 2-second re-sort can never swap rows under a click on "quit".
    property var shownApps: []

    onShowHeavyChanged: backend.showHeavy = showHeavy
    Component.onCompleted: shownApps = backend.apps
    Connections {
        target: backend
        function onSampled() {
            if (!appsHover.hovered)
                win.shownApps = backend.apps
        }
    }
    // What the "are you sure?" question is about: "process" (a pid), "app"
    // (an index on the busiest list); empty when nothing is asked.
    property string confirmKind: ""
    // The exact [pid, start] pairs asked about, so a re-sort of the list in
    // the meantime can never change what gets ended.
    property var confirmTargets: []
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
    readonly property var pageLabels: ({ overview: "Overview", details: "Details", storage: "Storage" })
    readonly property var switchRows: [
        { id: "wifi", label: "Wi-Fi" },
        { id: "bluetooth", label: "Bluetooth" },
        { id: "dnd", label: "Do not disturb" },
        { id: "night", label: "Night light" },
        { id: "saver", label: "Battery saver" },
        { id: "awake", label: "Stay awake" }
    ]
    readonly property var modeRows: ["everyday", "focus", "travel", "night"]
    readonly property var modeLabels: ({
        everyday: "Everyday",
        focus: "Focus",
        travel: "Travel",
        night: "Night",
        custom: "Custom"
    })
    readonly property var modeHeadlines: ({
        everyday: "All calm.",
        focus: "Heads down.",
        travel: "Going the distance.",
        night: "Winding down.",
        custom: "All calm."
    })
    readonly property var modeNotes: ({
        everyday: "Everything on, nothing held back.",
        focus: "Notifications held, Bluetooth off, screen stays awake.",
        travel: "Battery saver on, Bluetooth off. Wi-Fi stays on.",
        night: "Warm screen, notifications held until morning.",
        custom: "Your own mix. Pick a mode to reset it."
    })
    readonly property var meterLabels: ({ workload: "Processor", memory: "Memory", heat: "Heat" })
    readonly property var displayRows: [
        { id: "both", label: "Both screens" },
        { id: "monitor", label: "Monitor only" }
    ]

    function clock() {
        return Qt.formatDateTime(new Date(), "ddd d MMM   HH:mm")
    }

    function percent(level) {
        return level < 0 ? "—" : Math.round(level * 100) + "%"
    }

    // App and section names arrive in lower case; a capital reads as a name.
    function capital(text) {
        const s = String(text)
        return s.charAt(0).toUpperCase() + s.slice(1)
    }

    // The footer's key hints, keys in ink and words in grey. Built only
    // from Keel's own words, so the markup is safe.
    function hints(pairs) {
        return pairs.map(p => "<font color='" + theme.foreground + "'>" + p[0] + "</font>&nbsp; " + p[1])
                    .join("&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;")
    }

    function batteryCaption(word) {
        if (word === "plugged in" || word === "charging")
            return "Battery, " + word
        return "Battery, " + word + " left"
    }

    function nextPage() {
        page = pages[(pages.indexOf(page) + 1) % pages.length]
    }

    function moveSelection(step) {
        if (page === "storage") {
            const count = storage.entries.length
            storageIndex = Math.max(0, Math.min(count - 1, storageIndex + step))
            storagePage.reveal(folderRows.y + storageIndex * win.rowHeight, win.rowHeight)
            return
        }
        const rows = backend.processes
        if (rows.length === 0)
            return
        let index = rows.findIndex(r => r.pid === win.selectedPid)
        index = index < 0 ? 0 : Math.max(0, Math.min(rows.length - 1, index + step))
        win.selectedPid = rows[index].pid
        detailsPage.reveal(processRows.y + index * win.rowHeight, win.rowHeight)
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
        if (!row || row.essential)
            return
        win.selectedPid = pid
        win.confirmKind = "process"
        win.confirmTargets = row.targets
        win.confirmName = row.name + " (" + pid + ")"
    }

    function askToEndApp(index) {
        const app = win.shownApps[index]
        if (!app || !app.canEnd)
            return
        win.confirmKind = "app"
        win.confirmTargets = app.targets
        win.confirmName = app.targets.length > 1 ? app.name + ", all " + app.targets.length + " of its processes"
                                                 : app.name
    }

    function answer(force) {
        backend.endProcesses(win.confirmTargets, force)
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
    Shortcut { sequence: "E"; enabled: win.keysFree && win.page === "overview"; context: Qt.ApplicationShortcut; onActivated: win.controls.cycleDisplay() }
    Shortcut { sequence: ","; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.controls.setBrightness(win.controls.brightness - 0.1) }
    Shortcut { sequence: "."; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.controls.setBrightness(win.controls.brightness + 0.1) }
    Shortcut { sequence: "-"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.controls.setVolume(win.controls.volume - 0.1) }
    Shortcut { sequences: ["=", "+"]; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.controls.setVolume(win.controls.volume + 0.1) }
    Shortcut { sequence: "M"; enabled: win.keysFree && win.page === "overview"; context: Qt.ApplicationShortcut; onActivated: win.controls.toggleMute() }
    Shortcut { sequence: "/"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.startSearch() }
    Shortcut { sequence: "C"; enabled: win.keysFree && win.page === "details"; context: Qt.ApplicationShortcut; onActivated: backend.sortByMemory = false }
    Shortcut { sequence: "M"; enabled: win.keysFree && win.page === "details"; context: Qt.ApplicationShortcut; onActivated: backend.sortByMemory = true }
    Shortcut { sequence: "Down"; enabled: !win.asking && win.page !== "overview" && (win.keysFree || win.searching); context: Qt.ApplicationShortcut; onActivated: win.moveSelection(1) }
    Shortcut { sequence: "Up"; enabled: !win.asking && win.page !== "overview" && (win.keysFree || win.searching); context: Qt.ApplicationShortcut; onActivated: win.moveSelection(-1) }
    Shortcut { sequence: "J"; enabled: win.keysFree && win.page !== "overview"; context: Qt.ApplicationShortcut; onActivated: win.moveSelection(1) }
    Shortcut { sequence: "K"; enabled: win.keysFree && win.page !== "overview"; context: Qt.ApplicationShortcut; onActivated: win.moveSelection(-1) }
    Shortcut { sequences: ["X", "Delete"]; enabled: win.keysFree && win.page === "details"; context: Qt.ApplicationShortcut; onActivated: win.askToEndProcess(win.selectedPid) }
    Shortcut { sequences: ["Return", "Enter", "Right", "L"]; enabled: win.keysFree && win.page === "storage"; context: Qt.ApplicationShortcut; onActivated: win.enterFolder(win.storageIndex) }
    Shortcut { sequences: ["Backspace", "Left", "H"]; enabled: win.keysFree && win.page === "storage"; context: Qt.ApplicationShortcut; onActivated: win.leaveFolder() }
    Shortcut { sequence: "O"; enabled: win.keysFree && win.page === "storage"; context: Qt.ApplicationShortcut; onActivated: storage.reveal(win.storageIndex) }
    Shortcut { sequence: "R"; enabled: win.keysFree && win.page === "storage"; context: Qt.ApplicationShortcut; onActivated: storage.rescan() }
    Shortcut { sequence: "V"; enabled: win.keysFree && win.page === "overview"; context: Qt.ApplicationShortcut; onActivated: win.showHeavy = !win.showHeavy }
    Shortcut { sequence: "P"; enabled: win.keysFree && win.controls.batteryCareAvailable; context: Qt.ApplicationShortcut; onActivated: win.controls.toggleBatteryCare() }
    Shortcut { sequence: "Y"; enabled: win.asking; context: Qt.ApplicationShortcut; onActivated: win.answer(false) }
    Shortcut { sequence: "F"; enabled: win.asking; context: Qt.ApplicationShortcut; onActivated: win.answer(true) }

    Timer { interval: 15000; running: true; repeat: true; onTriggered: clockText.text = win.clock() }

    // ── Top bar: mark, pages, clock ──────────────────────────────────────
    Item {
        id: topBar
        width: parent.width
        height: 54

        Row {
            x: win.gutter
            anchors.verticalCenter: parent.verticalCenter
            spacing: 9
            // The mark, as in the app icon: a level waterline over a hull
            // and its keel.
            Canvas {
                id: mark
                width: 16
                height: 15
                anchors.verticalCenter: parent.verticalCenter
                readonly property color ink: theme.accent
                readonly property color waterline: theme.foreground
                onInkChanged: requestPaint()
                onWaterlineChanged: requestPaint()
                onPaint: {
                    const g = getContext("2d")
                    g.reset()
                    g.scale(0.8, 0.8)
                    g.fillStyle = waterline
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
            PlainText { text: "Keel"; font.weight: Font.Medium; anchors.verticalCenter: parent.verticalCenter }
        }

        Row {
            x: win.gutter + win.column
            height: parent.height
            spacing: 26
            Repeater {
                model: win.pages
                Item {
                    required property string modelData
                    readonly property bool active: win.page === modelData
                    width: tabText.implicitWidth
                    height: topBar.height
                    PlainText {
                        id: tabText
                        anchors.verticalCenter: parent.verticalCenter
                        text: win.pageLabels[modelData]
                        color: active || tabArea.containsMouse ? theme.foreground : theme.secondary
                        font.weight: active ? Font.Medium : Font.Normal
                    }
                    MouseArea { id: tabArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: win.page = modelData }
                }
            }
        }

        Row {
            anchors { right: parent.right; rightMargin: win.gutter; verticalCenter: parent.verticalCenter }
            spacing: 18
            PlainText { id: clockText; text: win.clock(); color: theme.secondary; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
            // Light or dark for now: a half-filled circle.
            Item {
                width: 13
                height: 13
                anchors.verticalCenter: parent.verticalCenter
                readonly property color ink: themeArea.containsMouse ? theme.foreground : theme.secondary
                Rectangle { anchors.fill: parent; radius: width / 2; color: "transparent"; border.width: 1.5; border.color: parent.ink }
                Item {
                    width: parent.width / 2
                    height: parent.height
                    clip: true
                    Rectangle { width: 13; height: 13; radius: 6.5; color: parent.parent.ink }
                }
                MouseArea { id: themeArea; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: theme.flip() }
            }
        }
        Rectangle { x: win.gutter; anchors.bottom: parent.bottom; width: win.contentWidth; height: 1; color: theme.foreground }
    }

    // ── Overview ─────────────────────────────────────────────────────────
    Scroller {
        id: overview
        visible: win.page === "overview"
        anchors { left: parent.left; right: parent.right; top: topBar.bottom; bottom: footer.top }
        gutter: win.gutter

        // The headline, with the one number most people want beside it.
        Item {
            id: hero
            width: parent.width
            height: Math.max(heroText.implicitHeight, heroBattery.implicitHeight) + 62
            readonly property var battery: backend.meters[0] || ({ word: "", value: "", unit: "", alert: false })

            Column {
                y: 36
                width: win.column - 16
                spacing: 4
                PlainText { text: win.modeLabels[win.controls.mode] || ""; color: theme.secondary; font.pixelSize: 12 }
                PlainText { width: parent.width; text: win.controls.network; color: theme.secondary; font.pixelSize: 11; elide: Text.ElideRight }
            }
            Column {
                id: heroText
                x: win.column
                y: 30
                width: parent.width - win.column - heroBattery.width - 32
                PlainText {
                    width: parent.width
                    text: backend.calm ? win.modeHeadlines[win.controls.mode] : backend.headline
                    color: backend.calm ? theme.foreground : theme.warn
                    font.family: "Inter Display"
                    font.weight: Font.Medium
                    font.pixelSize: 44
                    font.letterSpacing: -1.3
                    lineHeight: 0.96
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                PlainText {
                    width: parent.width
                    topPadding: 12
                    text: backend.line
                    color: theme.secondary
                    wrapMode: Text.WordWrap
                }
            }
            Column {
                id: heroBattery
                anchors.right: parent.right
                y: 30
                visible: hero.battery.value !== ""
                Figure {
                    anchors.right: parent.right
                    value: hero.battery.value
                    unit: hero.battery.unit
                    size: 44
                    color: hero.battery.alert ? theme.warn : theme.foreground
                }
                PlainText {
                    anchors.right: parent.right
                    topPadding: 8
                    text: win.batteryCaption(hero.battery.word)
                    color: theme.secondary
                    font.pixelSize: 11
                }
            }
        }

        Section {
            id: vitals
            label: "Now"
            Row {
                spacing: 28
                Repeater {
                    model: [1, 2, 3]
                    Column {
                        required property int modelData
                        readonly property var m: backend.meters[modelData] || ({ label: "", level: 0, word: "", value: "", unit: "", alert: false })
                        width: (vitals.width - win.column - 56) / 3
                        spacing: 6
                        PlainText { text: win.meterLabels[m.label] || ""; color: theme.secondary; font.pixelSize: 12 }
                        Figure { value: m.value; unit: m.unit; word: m.word; color: m.alert ? theme.warn : theme.foreground }
                        Item { width: 1; height: 2 }
                        Gauge { width: parent.width; level: m.level; fill: m.alert ? theme.warn : theme.foreground }
                    }
                }
            }
        }

        Section {
            id: modes
            label: "Mode"
            spacing: 8
            Row {
                Repeater {
                    model: win.modeRows
                    Lamp {
                        required property string modelData
                        width: (modes.width - win.column) / 4
                        on: win.controls.mode === modelData
                        label: win.modeLabels[modelData]
                        onClicked: win.controls.applyMode(modelData)
                    }
                }
            }
            PlainText { text: win.modeNotes[win.controls.mode] || ""; color: theme.secondary; font.pixelSize: 12 }
        }

        Section {
            id: switches
            label: "Switches"
            Grid {
                columns: 3
                rowSpacing: 10
                Repeater {
                    model: win.switchRows
                    Lamp {
                        required property var modelData
                        width: (switches.width - win.column) / 3
                        on: win.controls.states[modelData.id] === true
                        label: modelData.label
                        onClicked: win.controls.toggle(modelData.id)
                    }
                }
            }
            PlainText {
                visible: win.controls.error !== ""
                width: parent.width
                text: win.controls.error
                color: theme.warn
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }

        Section {
            id: levels
            label: "Levels"
            Row {
                spacing: 28
                Repeater {
                    model: [
                        { label: "Brightness", level: win.controls.brightness, kind: "brightness" },
                        { label: win.controls.muted ? "Volume, muted" : "Volume", level: win.controls.volume, kind: "volume" }
                    ]
                    Column {
                        required property var modelData
                        width: (levels.width - win.column - 28) / 2
                        spacing: 9
                        Item {
                            width: parent.width
                            height: 16
                            PlainText { text: modelData.label; color: theme.secondary; font.pixelSize: 12 }
                            PlainText { anchors.right: parent.right; text: win.percent(modelData.level); font.pixelSize: 12 }
                        }
                        Fader {
                            width: parent.width
                            level: modelData.level
                            fill: modelData.kind === "volume" && win.controls.muted ? theme.muted : theme.foreground
                            onPicked: level => modelData.kind === "brightness" ? win.controls.setBrightness(level)
                                                                                 : win.controls.setVolume(level)
                        }
                    }
                }
            }
        }

        Section {
            id: screens
            label: "Screens"
            PlainText {
                visible: win.controls.display === "laptop"
                text: "Laptop only. Plug in a monitor to choose."
                color: theme.secondary
            }
            Row {
                visible: win.controls.display !== "laptop"
                spacing: 28
                Repeater {
                    model: win.displayRows
                    Lamp {
                        required property var modelData
                        on: win.controls.display === modelData.id
                        label: modelData.label
                        onClicked: win.controls.setDisplay(modelData.id)
                    }
                }
                PlainText { anchors.verticalCenter: parent.verticalCenter; text: win.controls.monitor; color: theme.muted; font.pixelSize: 12 }
            }
        }

        Section {
            visible: win.controls.batteryCareAvailable
            label: "Battery care"
            spacing: 6
            Lamp {
                on: win.controls.batteryCare
                label: win.controls.batteryCare ? "Stops charging at 80%" : "Charges to 100%"
                onClicked: win.controls.toggleBatteryCare()
            }
            PlainText {
                leftPadding: 17
                text: "Best when the laptop is mostly plugged in. Asks for your password."
                color: theme.secondary
                font.pixelSize: 12
            }
        }

        Section {
            visible: win.controls.devices !== ""
            label: "Devices"
            PlainText { width: parent.width; text: win.controls.devices; wrapMode: Text.WordWrap }
        }

        Section {
            id: busiest
            label: win.showHeavy ? "Heaviest" : "Busiest"
            spacing: 0
            HoverHandler { id: appsHover }
            side: [
                Lamp { width: win.column - 16; on: !win.showHeavy; label: "Now"; onClicked: win.showHeavy = false },
                Lamp { width: win.column - 16; on: win.showHeavy; label: "Since opened"; onClicked: win.showHeavy = true }
            ]

            Repeater {
                model: 3
                Item {
                    id: appRow
                    required property int index
                    readonly property var app: win.showHeavy ? backend.heavy[index] : win.shownApps[index]
                    readonly property real level: !app ? 0
                        : win.showHeavy ? app.seconds / Math.max(1, backend.heavy[0].seconds)
                        : Math.max(app.level, app.level > 0.001 ? 0.04 : 0)
                    readonly property bool hungry: !win.showHeavy && app !== undefined && app.level >= 0.5
                    visible: app !== undefined
                    width: busiest.width - win.column
                    height: 34
                    Rectangle { visible: appRow.index > 0; width: parent.width; height: 1; color: theme.line }
                    PlainText {
                        anchors { left: parent.left; right: appScale.left; rightMargin: 20; verticalCenter: parent.verticalCenter }
                        text: appRow.app ? win.capital(appRow.app.name) : ""
                        elide: Text.ElideRight
                    }
                    Gauge {
                        id: appScale
                        anchors { right: appDetail.left; rightMargin: 20; verticalCenter: parent.verticalCenter }
                        width: 90
                        ticks: false
                        level: appRow.level
                        fill: appRow.hungry ? theme.warn : theme.foreground
                    }
                    PlainText {
                        id: appDetail
                        anchors { right: win.showHeavy ? parent.right : appAction.left; rightMargin: win.showHeavy ? 0 : 20; verticalCenter: parent.verticalCenter }
                        width: 130
                        horizontalAlignment: Text.AlignRight
                        text: appRow.app ? appRow.app.detail : ""
                        color: appRow.hungry ? theme.warn : theme.secondary
                        font.pixelSize: 12
                        elide: Text.ElideLeft
                    }
                    // Apps with a window close politely, like their close
                    // button; background programs are ended, after asking.
                    PlainText {
                        id: appAction
                        visible: !win.showHeavy
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        width: 48
                        horizontalAlignment: Text.AlignRight
                        text: !appRow.app || win.showHeavy ? "" : appRow.app.canQuit ? "Quit" : appRow.app.canEnd ? "End" : "System"
                        color: appRow.app && !appRow.app.canEnd ? theme.muted : actionArea.containsMouse ? theme.foreground : theme.secondary
                        font.pixelSize: 12
                        font.underline: actionArea.containsMouse
                        MouseArea {
                            id: actionArea
                            anchors.fill: parent
                            anchors.margins: -6
                            enabled: appAction.visible && appRow.app !== undefined && appRow.app.canEnd
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: appRow.app.canQuit ? backend.closeWindows(appRow.app.windows) : win.askToEndApp(appRow.index)
                        }
                    }
                }
            }
            PlainText {
                visible: win.showHeavy
                topPadding: 8
                text: "Processor time since each app opened is what drains the battery."
                color: theme.secondary
                font.pixelSize: 12
            }
        }
    }

    // ── Details ──────────────────────────────────────────────────────────
    Scroller {
        id: detailsPage
        visible: win.page === "details"
        anchors { left: parent.left; right: parent.right; top: topBar.bottom; bottom: footer.top }
        gutter: win.gutter

        Section {
            id: processor
            rule: false
            label: "Processor"
            note: "Last 2 minutes"
            Row {
                spacing: 28
                readonly property real available: processor.width - win.column - 28
                Column {
                    width: parent.available * 0.64
                    spacing: 6
                    Trace { width: parent.width; height: 76; values: backend.cpuHistory }
                    Item {
                        width: parent.width
                        height: 14
                        PlainText { text: "2 min ago"; color: theme.muted; font.pixelSize: 11 }
                        PlainText { anchors.right: parent.right; text: "Now"; color: theme.muted; font.pixelSize: 11 }
                    }
                }
                Column {
                    id: coreColumn
                    width: parent.available * 0.36
                    spacing: 6
                    readonly property int count: Math.max(1, backend.cores.length)
                    readonly property int gap: count > 24 ? 2 : 3
                    Item {
                        width: parent.width
                        height: 76
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.line }
                        Row {
                            anchors.bottom: parent.bottom
                            spacing: coreColumn.gap
                            Repeater {
                                model: backend.cores.length
                                Item {
                                    required property int index
                                    readonly property real load: (backend.cores[index] || 0) / 100
                                    width: Math.max(2, (coreColumn.width - coreColumn.gap * (coreColumn.count - 1)) / coreColumn.count)
                                    height: 76
                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        width: parent.width
                                        height: Math.max(1, parent.height * parent.load)
                                        color: theme.foreground
                                    }
                                }
                            }
                        }
                    }
                    PlainText { text: "Each core now"; color: theme.muted; font.pixelSize: 11 }
                }
            }
        }

        Section {
            id: readings
            label: "Readings"
            Grid {
                columns: 3
                columnSpacing: 28
                rowSpacing: 24
                Repeater {
                    model: backend.sections.length
                    Column {
                        id: reading
                        required property int index
                        readonly property var section: backend.sections[index]
                        width: (readings.width - win.column - 56) / 3
                        PlainText { text: win.capital(reading.section.title); font.pixelSize: 12; font.weight: Font.Medium; bottomPadding: 6 }
                        Rectangle { width: parent.width; height: 1; color: theme.line }
                        Repeater {
                            model: reading.section.rows.length
                            Item {
                                required property int index
                                readonly property var row: reading.section.rows[index]
                                width: reading.width
                                height: 24
                                PlainText { id: rowLabel; anchors.verticalCenter: parent.verticalCenter; text: win.capital(parent.row.label); color: theme.secondary; font.pixelSize: 12 }
                                PlainText {
                                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                    width: parent.width - rowLabel.implicitWidth - 12
                                    horizontalAlignment: Text.AlignRight
                                    elide: Text.ElideLeft
                                    text: parent.row.value
                                    font.pixelSize: 12
                                }
                            }
                        }
                    }
                }
            }
        }

        Section {
            id: network
            label: "Network"
            Row {
                spacing: 28
                Repeater {
                    model: [
                        { label: "Down", peak: backend.downPeak, values: backend.downHistory },
                        { label: "Up", peak: backend.upPeak, values: backend.upHistory }
                    ]
                    Column {
                        required property var modelData
                        width: (network.width - win.column - 28) / 2
                        spacing: 8
                        Item {
                            width: parent.width
                            height: 16
                            PlainText { text: modelData.label; color: theme.secondary; font.pixelSize: 12 }
                            PlainText { anchors.right: parent.right; text: "Peak " + modelData.peak; color: theme.muted; font.pixelSize: 11 }
                        }
                        Trace { width: parent.width; height: 34; grid: false; values: modelData.values }
                    }
                }
            }
        }

        Section {
            label: "Processes"
            note: backend.sortByMemory ? "By memory" : "By processor"
            Item {
                width: parent.width
                height: 20
                PlainText {
                    id: searchPrompt
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Find"
                    color: win.searching ? theme.foreground : theme.secondary
                    font.weight: win.searching ? Font.Medium : Font.Normal
                    MouseArea { anchors.fill: parent; anchors.margins: -6; cursorShape: Qt.PointingHandCursor; onClicked: win.startSearch() }
                }
                TextInput {
                    id: searchInput
                    anchors { left: searchPrompt.right; leftMargin: 12; right: parent.right; verticalCenter: parent.verticalCenter }
                    text: backend.filter
                    color: theme.foreground
                    selectionColor: theme.accent
                    selectedTextColor: theme.onAccent
                    font.pixelSize: 13
                    clip: true
                    onTextEdited: {
                        backend.filter = text
                        const first = backend.processes[0]
                        win.selectedPid = first ? first.pid : 0
                    }
                    onActiveFocusChanged: if (activeFocus) win.searching = true
                    Keys.onReturnPressed: win.stopSearch(true)
                    Keys.onEnterPressed: win.stopSearch(true)
                    PlainText {
                        visible: searchInput.text === ""
                        text: "Part of a name, or a PID"
                        color: theme.muted
                    }
                }
            }
        }

        // The process table runs the full width, its PID in the label column.
        Item {
            width: parent.width
            height: 26
            readonly property color sortInk: theme.foreground
            PlainText { x: 10; anchors.bottom: parent.bottom; bottomPadding: 6; text: "PID"; color: theme.muted; font.pixelSize: 11 }
            PlainText { x: win.column; anchors.bottom: parent.bottom; bottomPadding: 6; text: "Name"; color: theme.muted; font.pixelSize: 11 }
            PlainText {
                anchors { right: parent.right; rightMargin: 150; bottom: parent.bottom }
                width: 70
                bottomPadding: 6
                horizontalAlignment: Text.AlignRight
                text: "Processor"
                color: backend.sortByMemory ? theme.muted : theme.foreground
                font.pixelSize: 11
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: backend.sortByMemory = false }
            }
            PlainText {
                anchors { right: parent.right; rightMargin: 60; bottom: parent.bottom }
                width: 80
                bottomPadding: 6
                horizontalAlignment: Text.AlignRight
                text: "Memory"
                color: backend.sortByMemory ? theme.foreground : theme.muted
                font.pixelSize: 11
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: backend.sortByMemory = true }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.line }
        }

        Column {
            id: processRows
            width: parent.width
            Repeater {
                model: backend.processes.length
                Item {
                    id: processRow
                    required property int index
                    readonly property var process: backend.processes[index] || ({ pid: 0, name: "", cpu: "", memory: "", essential: false })
                    readonly property bool selected: process.pid === win.selectedPid
                    width: processRows.width
                    height: win.rowHeight
                    Rectangle { anchors.fill: parent; color: processRow.selected || rowArea.containsMouse ? theme.bar : "transparent" }
                    Rectangle { visible: processRow.selected; width: 2; height: parent.height; color: theme.accent }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.line }
                    PlainText { x: 10; anchors.verticalCenter: parent.verticalCenter; text: processRow.process.pid; color: theme.secondary; font.pixelSize: 12 }
                    PlainText {
                        x: win.column
                        width: parent.width - win.column - 240
                        anchors.verticalCenter: parent.verticalCenter
                        text: processRow.process.name
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    PlainText {
                        anchors { right: parent.right; rightMargin: 150; verticalCenter: parent.verticalCenter }
                        text: processRow.process.cpu
                        color: backend.sortByMemory ? theme.secondary : theme.foreground
                        font.pixelSize: 12
                    }
                    PlainText {
                        anchors { right: parent.right; rightMargin: 60; verticalCenter: parent.verticalCenter }
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
                    PlainText {
                        anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                        visible: processRow.selected || rowArea.containsMouse
                        text: processRow.process.essential ? "System" : "End"
                        color: processRow.process.essential ? theme.muted : endArea.containsMouse ? theme.warn : theme.secondary
                        font.pixelSize: 12
                        font.underline: endArea.containsMouse && !processRow.process.essential
                        MouseArea {
                            id: endArea
                            anchors.fill: parent
                            anchors.margins: -6
                            enabled: !processRow.process.essential
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: win.askToEndProcess(processRow.process.pid)
                        }
                    }
                }
            }
        }
        PlainText {
            visible: backend.processes.length === 0 && backend.filter !== ""
            x: win.column
            topPadding: 14
            text: "Nothing called “" + backend.filter + "” is running."
            color: theme.secondary
        }
    }

    // ── Storage ──────────────────────────────────────────────────────────
    Scroller {
        id: storagePage
        visible: win.page === "storage"
        anchors { left: parent.left; right: parent.right; top: topBar.bottom; bottom: footer.top }
        gutter: win.gutter

        Item {
            id: disk
            width: parent.width
            height: diskBody.implicitHeight + 60
            readonly property bool full: storage.usedFraction > 0.95

            Column {
                y: 36
                width: win.column - 16
                spacing: 4
                PlainText { text: "Disk"; color: theme.secondary; font.pixelSize: 12 }
                PlainText { width: parent.width; text: storage.status; color: theme.muted; font.pixelSize: 11; wrapMode: Text.WordWrap }
                PlainText {
                    visible: !storage.scanning
                    topPadding: 6
                    text: "Rescan"
                    color: rescanArea.containsMouse ? theme.foreground : theme.secondary
                    font.pixelSize: 11
                    font.underline: rescanArea.containsMouse
                    MouseArea { id: rescanArea; anchors.fill: parent; anchors.margins: -4; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: storage.rescan() }
                }
            }
            Column {
                id: diskBody
                x: win.column
                y: 30
                width: parent.width - win.column
                spacing: 10
                Figure { value: storage.freeText; word: "free"; size: 44; color: disk.full ? theme.warn : theme.foreground }
                PlainText { text: storage.usedText; color: theme.secondary }
                Item { width: 1; height: 4 }
                Gauge { width: parent.width; level: storage.usedFraction; fill: disk.full ? theme.warn : theme.foreground }
                PlainText {
                    visible: storage.compressed
                    width: parent.width
                    topPadding: 4
                    text: "This disk compresses files, so the sizes below add up to more than it holds."
                    color: theme.secondary
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }
        }

        // The usual space hogs; each opens its folder.
        Section {
            id: hogs
            visible: storage.spots.length > 0
            label: "Usual hogs"
            Row {
                Repeater {
                    model: storage.spots.length
                    Item {
                        id: spot
                        required property int index
                        readonly property var info: storage.spots[index]
                        width: (hogs.width - win.column) / Math.max(1, storage.spots.length)
                        height: spotColumn.implicitHeight
                        Column {
                            id: spotColumn
                            spacing: 6
                            PlainText {
                                text: spot.info.label
                                color: spotArea.containsMouse ? theme.foreground : theme.secondary
                                font.pixelSize: 12
                                font.underline: spotArea.containsMouse
                            }
                            Figure { value: spot.info.size; size: 20 }
                        }
                        MouseArea { id: spotArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: storage.revealSpot(spot.index) }
                    }
                }
            }
        }

        Section {
            id: changes
            visible: storage.grown.length > 0 || storage.stale.length > 0
            label: "Changes"
            Row {
                spacing: 28
                Repeater {
                    model: [
                        { title: "Grew since " + (storage.grownSince || "the last scan"), empty: storage.grownSince === "" ? "Rescan later to see what grows." : "Nothing grew by more than 256 MB.", rows: storage.grown, stale: false },
                        { title: "Big, untouched 6+ months", empty: "No file over 1 GB has sat untouched that long.", rows: storage.stale, stale: true }
                    ]
                    Column {
                        id: insight
                        required property var modelData
                        width: (changes.width - win.column - 28) / 2
                        PlainText { text: insight.modelData.title; font.pixelSize: 12; font.weight: Font.Medium; bottomPadding: 6 }
                        Rectangle { width: parent.width; height: 1; color: theme.line }
                        Repeater {
                            model: Math.min(4, insight.modelData.rows.length)
                            Item {
                                required property int index
                                readonly property var row: insight.modelData.rows[index]
                                width: insight.width
                                height: 26
                                PlainText {
                                    width: parent.width - sizeText.implicitWidth - 12
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: parent.row.path
                                    color: insightArea.containsMouse ? theme.foreground : theme.secondary
                                    font.pixelSize: 12
                                    font.underline: insightArea.containsMouse
                                    elide: Text.ElideMiddle
                                }
                                PlainText {
                                    id: sizeText
                                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                    text: parent.row.size + (insight.modelData.stale ? "   " + parent.row.age : "")
                                    color: insight.modelData.stale ? theme.foreground : theme.warn
                                    font.pixelSize: 12
                                }
                                MouseArea {
                                    id: insightArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: insight.modelData.stale ? storage.revealStale(parent.index) : storage.revealGrown(parent.index)
                                }
                            }
                        }
                        PlainText {
                            visible: insight.modelData.rows.length === 0
                            width: parent.width
                            topPadding: 8
                            text: insight.modelData.empty
                            color: theme.secondary
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }

        Section {
            label: "Biggest in"
            spacing: 4
            PlainText { width: parent.width; text: storage.place; font.weight: Font.Medium; elide: Text.ElideMiddle }
            PlainText {
                text: storage.atTop ? "Enter goes into a folder, O opens it in your file manager."
                                    : "Backspace goes back out. Enter goes in, O opens it."
                color: theme.secondary
                font.pixelSize: 12
            }
        }

        // Folders run the full width, their size in the label column.
        Rectangle { width: parent.width; height: 1; color: theme.line }
        Column {
            id: folderRows
            width: parent.width
            Repeater {
                model: storage.entries.length
                Item {
                    id: entryRow
                    required property int index
                    readonly property var entry: storage.entries[index] || ({ name: "", size: "", fraction: 0, directory: false, other: false })
                    readonly property bool selected: index === win.storageIndex
                    width: folderRows.width
                    height: win.rowHeight
                    Rectangle { anchors.fill: parent; color: entryRow.selected || entryArea.containsMouse ? theme.bar : "transparent" }
                    Rectangle { visible: entryRow.selected; width: 2; height: parent.height; color: theme.accent }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.line }
                    PlainText {
                        x: 10
                        width: win.column - 34
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignRight
                        text: entryRow.entry.size
                        color: entryRow.entry.other ? theme.secondary : theme.foreground
                        font.pixelSize: 12
                    }
                    PlainText {
                        x: win.column
                        width: folderBar.x - win.column - 20
                        anchors.verticalCenter: parent.verticalCenter
                        text: entryRow.entry.name + (entryRow.entry.directory ? "/" : "")
                        color: entryRow.entry.other ? theme.secondary : theme.foreground
                        elide: Text.ElideMiddle
                    }
                    Gauge {
                        id: folderBar
                        anchors { right: parent.right; rightMargin: 70; verticalCenter: parent.verticalCenter }
                        width: Math.round(parent.width * 0.3)
                        ticks: false
                        level: Math.max(0.005, entryRow.entry.fraction)
                        fill: entryRow.entry.other ? theme.muted : theme.foreground
                    }
                    MouseArea {
                        id: entryArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: win.storageIndex = entryRow.index
                        onDoubleClicked: win.enterFolder(entryRow.index)
                    }
                    PlainText {
                        anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                        visible: !entryRow.entry.other && (entryRow.selected || entryArea.containsMouse)
                        text: "Open"
                        color: openArea.containsMouse ? theme.foreground : theme.secondary
                        font.pixelSize: 12
                        font.underline: openArea.containsMouse
                        MouseArea {
                            id: openArea
                            anchors.fill: parent
                            anchors.margins: -6
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: storage.reveal(entryRow.index)
                        }
                    }
                }
            }
        }
        PlainText {
            visible: storage.entries.length === 0
            x: win.column
            topPadding: 14
            text: storage.scanning ? "Measuring. The first scan takes a little while; after that it opens instantly."
                                   : "Nothing here is big enough to list."
            color: theme.secondary
        }
    }

    // ── Footer: the keys for this page ───────────────────────────────────
    Item {
        id: footer
        anchors.bottom: parent.bottom
        width: parent.width
        height: 38
        Rectangle { x: win.gutter; width: win.contentWidth; height: 1; color: theme.line }
        PlainText {
            x: win.gutter
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.StyledText
            text: win.hints(win.searching ? [["Type", "to find"], ["↑↓", "pick"], ["Enter", "done"], ["Esc", "clear"]]
                          : win.page === "details" ? [["Tab", "next page"], ["/", "find"], ["↑↓", "pick"], ["X", "end"]]
                          : win.page === "storage" ? [["Tab", "next page"], ["↑↓", "pick"], ["Enter", "go in"], ["⌫", "back"]]
                          : [["1–4", "modes"], ["Letters", "switches"], ["Tab", "next page"]])
            color: theme.secondary
            font.pixelSize: 11
        }
        PlainText {
            anchors { right: parent.right; rightMargin: win.gutter; verticalCenter: parent.verticalCenter }
            textFormat: Text.StyledText
            text: win.hints([["?", "keys"], ["Esc", "close"]])
            color: theme.secondary
            font.pixelSize: 11
        }
    }

    // Ending can't be taken back, so it asks first.
    Rectangle {
        anchors.fill: parent
        visible: win.asking
        color: Qt.rgba(0, 0, 0, theme.dark ? 0.6 : 0.28)
        MouseArea { anchors.fill: parent; onClicked: win.confirmKind = "" }
        Rectangle {
            anchors.centerIn: parent
            width: 460
            height: askColumn.implicitHeight + 56
            color: theme.background
            border.color: theme.line
            radius: Math.min(8, theme.radius)
            MouseArea { anchors.fill: parent }
            Column {
                id: askColumn
                x: 28
                y: 28
                width: parent.width - 56
                spacing: 8
                PlainText {
                    width: parent.width
                    elide: Text.ElideRight
                    text: "End " + win.confirmName + "?"
                    font.family: "Inter Display"
                    font.weight: Font.Medium
                    font.pixelSize: 20
                    font.letterSpacing: -0.3
                }
                PlainText { text: "Anything unsaved in it may be lost."; color: theme.secondary }
                Item { width: 1; height: 12 }
                Row {
                    spacing: 8
                    Repeater {
                        model: [{ label: "End it", key: "Y", force: false, cancel: false, primary: true },
                                { label: "Force", key: "F", force: true, cancel: false, primary: false },
                                { label: "Keep it", key: "Esc", force: false, cancel: true, primary: false }]
                        Rectangle {
                            id: choice
                            required property var modelData
                            width: choiceRow.implicitWidth + 28
                            height: 32
                            radius: Math.min(4, theme.radius)
                            color: modelData.primary ? theme.foreground : choiceArea.containsMouse ? theme.bar : "transparent"
                            border.width: modelData.primary ? 0 : 1
                            border.color: theme.line
                            Row {
                                id: choiceRow
                                anchors.centerIn: parent
                                spacing: 8
                                PlainText {
                                    text: choice.modelData.label
                                    color: choice.modelData.primary ? theme.background : choice.modelData.force ? theme.warn : theme.foreground
                                }
                                PlainText {
                                    anchors.baseline: parent.children[0].baseline
                                    text: choice.modelData.key
                                    color: choice.modelData.primary ? theme.muted : theme.muted
                                    font.pixelSize: 11
                                }
                            }
                            MouseArea {
                                id: choiceArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: choice.modelData.cancel ? win.confirmKind = "" : win.answer(choice.modelData.force)
                            }
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: win.helpVisible
        color: theme.background
        MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }
        Scroller {
            anchors.fill: parent
            gutter: win.gutter
            Item { width: 1; height: 30 }
            PlainText {
                text: "Keys"
                font.family: "Inter Display"
                font.weight: Font.Medium
                font.pixelSize: 30
                font.letterSpacing: -0.7
                bottomPadding: 18
            }
            Rectangle { width: parent.width; height: 1; color: theme.foreground }
            Repeater {
                model: [
                    ["Tab", "Overview, details, storage"],
                    ["1  2  3  4", "Everyday, focus, travel, night"],
                    ["W B D N S A", "Wi-Fi, Bluetooth, do not disturb, night light, battery saver, stay awake"],
                    ["E", "Screens: both, or monitor only"],
                    [",  .   -  =", "Brightness down and up, volume down and up"],
                    ["M", "Mute on the overview; sort by memory on details"],
                    ["C", "Sort processes by processor"],
                    ["/", "Find a process"],
                    ["↑ ↓   J K", "Pick a process or a folder"],
                    ["X   Delete", "End the picked process"],
                    ["Enter   ⌫", "Into a folder, back out"],
                    ["O   R", "Open in the file manager, rescan the disk"],
                    ["V", "Busiest now, or heaviest since opened"],
                    ["P", "Battery care: stop charging at 80%"],
                    ["T", "Light or dark, until Keel closes"],
                    ["Esc   Q", "Close Keel"]
                ]
                Item {
                    required property var modelData
                    width: parent.width
                    height: 32
                    PlainText { anchors.verticalCenter: parent.verticalCenter; text: modelData[0]; font.weight: Font.Medium }
                    PlainText {
                        x: win.column
                        width: parent.width - win.column
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData[1]
                        color: theme.secondary
                        elide: Text.ElideRight
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.line }
                }
            }
        }
    }
}
