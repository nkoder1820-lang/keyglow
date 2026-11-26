import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1100
    height: 700
    // A tiling window manager hands out whatever size it likes, so the
    // layout has to hold together well below the size it opens at.
    minimumWidth: 520
    minimumHeight: 420
    readonly property bool narrow: width < 820
    visible: true
    title: "Keyglow"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    readonly property bool keysFree: !backend.teaching && backend.onboarded
    property string category: "all"
    readonly property var shown: backend.presets.filter(p => category === "all" || (category === "favorites" ? p.favorite : p.category === category))
    readonly property var swatches: ["#ffffff", "#ff2d2d", "#ff8a00", "#ffd60a", "#3ddc5a",
                                     "#00d5c8", "#2f8bff", "#7d5cff", "#e052ff", "#ff4f9a"]

    // While teaching, every key belongs to the walk, so no shortcut is live.
    Shortcut { sequence: "?"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Escape"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = false }
    Shortcut { sequence: "Q"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: backend.toggle() }
    Shortcut { sequence: "Right"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: backend.step(1) }
    Shortcut { sequence: "Left"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: backend.step(-1) }
    Shortcut { sequence: "Up"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: backend.brightness += 10 }
    Shortcut { sequence: "Down"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: backend.brightness -= 10 }
    Shortcut { sequence: "]"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: backend.speed += 10 }
    Shortcut { sequence: "["; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: backend.speed -= 10 }
    Shortcut { sequence: "F"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: backend.fillCustom() }
    Shortcut { sequence: "P"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: backend.paintFromCurrent() }
    Shortcut { sequence: "S"; enabled: win.keysFree; context: Qt.ApplicationShortcut; onActivated: backend.toggleFavorite(backend.preset) }
    Shortcut { sequence: "T"; enabled: win.keysFree && backend.canSync; context: Qt.ApplicationShortcut; onActivated: backend.startQuickSync() }

    component Caption: Label {
        opacity: 0.55
        font.pixelSize: 11
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 1.2
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        // The room takes on the colour of the keyboard.
        Rectangle {
            x: parent.width * 0.36 - width / 2
            y: -height * 0.42
            width: parent.width * 1.0
            height: parent.height * 1.05
            radius: width / 2
            opacity: backend.power ? 0.2 : 0
            Behavior on opacity { NumberAnimation { duration: 400 } }
            gradient: Gradient {
                GradientStop { position: 0.35; color: backend.ambient }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 22
            enabled: win.keysFree

            // Left: the keyboard and the scenes.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                // Without this the row takes its widest child as a minimum and
                // pushes the side panel out of a narrow window.
                Layout.minimumWidth: 220
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Label { text: "Keyglow"; font.pixelSize: 22; font.weight: Font.DemiBold }
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: backend.deviceStatus === "ready" ? "#3ddc5a" : "#ff6b6b"
                    }
                    Label {
                        Layout.fillWidth: true
                        opacity: 0.6
                        elide: Text.ElideRight
                        text: backend.deviceStatus === "ready" ? qsTr("%1 connected").arg(backend.deviceName)
                            : backend.deviceStatus === "permission" ? qsTr("%1 found, but access is blocked").arg(backend.deviceName)
                            : qsTr("No keyboard found")
                    }
                    Switch {
                        text: win.narrow ? "" : backend.power ? qsTr("Lights on") : qsTr("Lights off")
                        checked: backend.power
                        focusPolicy: Qt.NoFocus
                        onToggled: backend.power = checked
                    }
                }

                // A missing keyboard or permission is said out loud, never hidden.
                Rectangle {
                    Layout.fillWidth: true
                    visible: backend.deviceStatus !== "ready"
                    implicitHeight: warning.implicitHeight + 20
                    radius: theme.radius
                    color: "#3a1d1d"
                    border.color: "#ff6b6b"
                    Label {
                        id: warning
                        anchors.fill: parent
                        anchors.margins: 10
                        wrapMode: Text.Wrap
                        color: "#ffb4b4"
                        text: backend.deviceStatus === "permission"
                            ? qsTr("The keyboard is plugged in, but Keyglow isn't allowed to talk to it. Reinstall Keyglow (it ships the access rule), then replug the keyboard.")
                            : qsTr("No keyboard to light was found. A CB Artemis must be plugged in with its cable (lighting can't be set over Bluetooth or the 2.4G dongle). Any other keyboard is reached through OpenRGB: install it and start its server with \"openrgb --server\".")
                    }
                }

                KeyboardView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    // The keyboard is the point of the window: it keeps its
                    // size and the scenes below scroll if space runs short.
                    Layout.preferredHeight: 270
                    Layout.minimumHeight: 190
                    Layout.maximumHeight: 340
                    paintable: backend.preset === "custom"
                }

                // Until taught, key positions are a guess; say so and offer the fix.
                Rectangle {
                    Layout.fillWidth: true
                    visible: !backend.taught
                    implicitHeight: teachRow.implicitHeight + 16
                    radius: theme.radius
                    color: "#1d2233"
                    border.color: "#3b4a7a"
                    RowLayout {
                        id: teachRow
                        anchors.fill: parent
                        anchors.margins: 8
                        anchors.leftMargin: 12
                        spacing: 12
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: qsTr("Keyglow is guessing which light is under which key. Sync it once: press about a dozen keys as they light up, and every scene lands exactly on your keys.")
                        }
                        Button {
                            text: qsTr("Quick sync")
                            highlighted: true
                            focusPolicy: Qt.NoFocus
                            Material.foreground: theme.accentForeground
                            Material.roundedScale: theme.radius
                            onClicked: backend.startQuickSync()
                        }
                    }
                }

                // Good news after a sync, shown for a few seconds.
                Rectangle {
                    Layout.fillWidth: true
                    visible: backend.notice.length > 0
                    implicitHeight: noticeLabel.implicitHeight + 18
                    radius: theme.radius
                    color: "#17301f"
                    border.color: "#3ddc5a"
                    Label {
                        id: noticeLabel
                        anchors.fill: parent
                        anchors.margins: 9
                        anchors.leftMargin: 12
                        wrapMode: Text.Wrap
                        color: "#b9f5c6"
                        text: backend.notice
                    }
                }

                // What is playing, in words.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Label {
                        text: backend.power ? (backend.current.name || "") : qsTr("Lights off")
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.fillWidth: true
                        text: backend.power ? (backend.current.blurb || "") : qsTr("Press Space to bring them back")
                        opacity: 0.55
                        elide: Text.ElideRight
                    }
                    Label {
                        text: backend.current.favorite ? "★" : "☆"
                        font.pixelSize: 20
                        color: backend.current.favorite ? "#ffd60a" : "#8a8a96"
                        TapHandler { onTapped: backend.toggleFavorite(backend.preset) }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                    }
                }

                // Kinds of scene.
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: backend.categories
                        Rectangle {
                            required property var modelData
                            readonly property bool on: win.category === modelData.id
                            width: chip.implicitWidth + 24
                            height: 30
                            radius: theme.radius > 0 ? 15 : 0
                            color: on ? theme.accent : chipHover.hovered ? "#26262d" : "#1a1a1f"
                            border.color: on ? theme.accent : "#2c2c33"
                            Label {
                                id: chip
                                anchors.centerIn: parent
                                text: modelData.name
                                font.pixelSize: 13
                                color: parent.on ? theme.accentForeground : "#d8d8e0"
                            }
                            HoverHandler { id: chipHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: win.category = modelData.id }
                        }
                    }
                }
                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 130
                    Layout.preferredHeight: scenes.implicitHeight
                    contentHeight: scenes.implicitHeight
                    clip: true
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    Label {
                        visible: win.shown.length === 0
                        width: parent.width
                        topPadding: 24
                        horizontalAlignment: Text.AlignHCenter
                        opacity: 0.5
                        text: qsTr("No favourites yet. Hover a scene and click its star, or press S.")
                    }
                    Flow {
                        id: scenes
                        width: parent.width - 10
                        spacing: 8
                        // As many columns as fit, stretched to fill the row.
                        readonly property int perRow: Math.max(2, Math.floor((width + spacing) / 180))
                        readonly property real cardWidth: Math.floor((width - spacing * (perRow - 1)) / perRow)
                        Repeater {
                            model: win.shown
                            PresetCard {
                                required property var modelData
                                preset: modelData
                                width: scenes.cardWidth
                                selected: backend.preset === modelData.id
                                opacity: backend.power || selected ? 1 : 0.7
                                onPicked: backend.preset = modelData.id
                            }
                        }
                    }
                }
            }

            // Right: how the chosen scene looks.
            Rectangle {
                Layout.preferredWidth: win.narrow ? 228 : 268
                Layout.minimumWidth: Layout.preferredWidth
                Layout.maximumWidth: Layout.preferredWidth
                Layout.fillHeight: true
                radius: theme.radius
                color: "#15151a"
                border.color: "#26262d"

                // Scrolls when the window is too short to show every control.
                Flickable {
                    id: sideScroll
                    anchors.fill: parent
                    anchors.margins: 16
                    contentHeight: sideColumn.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    ColumnLayout {
                        id: sideColumn
                        width: sideScroll.width
                        height: Math.max(implicitHeight, sideScroll.height)
                        spacing: 10

                        Caption { text: qsTr("Colour") }
                        Label {
                            Layout.fillWidth: true
                            visible: !backend.usesColor
                            wrapMode: Text.Wrap
                            opacity: 0.5
                            font.pixelSize: 12
                            text: qsTr("This scene brings its own colours.")
                        }
                        Grid {
                            id: swatchGrid
                            Layout.fillWidth: true
                            columns: 5
                            spacing: 8
                            enabled: backend.usesColor
                            opacity: enabled ? 1 : 0.3
                            LayoutMirroring.enabled: false
                            Repeater {
                                model: win.swatches
                                Rectangle {
                                    required property string modelData
                                    width: Math.floor((swatchGrid.width - swatchGrid.spacing * 4) / 5); height: 32
                                    radius: Math.min(4, theme.radius)
                                    color: modelData
                                    border.width: backend.color === modelData ? 3 : 1
                                    border.color: backend.color === modelData ? theme.accent : "#3a3a42"
                                    TapHandler { onTapped: backend.color = parent.modelData }
                                }
                            }
                        }
                        // Any hue: drag along the spectrum.
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 20
                            radius: Math.min(4, theme.radius)
                            enabled: backend.usesColor
                            opacity: enabled ? 1 : 0.3
                            LayoutMirroring.enabled: false
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.000; color: "#ff0000" }
                                GradientStop { position: 0.167; color: "#ffff00" }
                                GradientStop { position: 0.333; color: "#00ff00" }
                                GradientStop { position: 0.500; color: "#00ffff" }
                                GradientStop { position: 0.667; color: "#0000ff" }
                                GradientStop { position: 0.833; color: "#ff00ff" }
                                GradientStop { position: 1.000; color: "#ff0000" }
                            }
                            MouseArea {
                                anchors.fill: parent
                                preventStealing: true
                                function pick(x) {
                                    backend.color = Qt.hsva(Math.max(0, Math.min(0.999, x / width)), 1, 1, 1).toString()
                                }
                                onPressed: mouse => pick(mouse.x)
                                onPositionChanged: mouse => pick(mouse.x)
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 8
                            Caption { text: qsTr("Brightness"); Layout.fillWidth: true }
                            Label { text: qsTr("%1%").arg(backend.brightness); opacity: 0.7 }
                        }
                        Slider {
                            Layout.fillWidth: true
                            focusPolicy: Qt.NoFocus
                            from: 0; to: 100; stepSize: 1
                            value: backend.brightness
                            onMoved: backend.brightness = value
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            opacity: backend.usesSpeed ? 1 : 0.35
                            Caption { text: qsTr("Speed"); Layout.fillWidth: true }
                            Label { text: qsTr("%1%").arg(backend.speed); opacity: 0.7 }
                        }
                        Slider {
                            Layout.fillWidth: true
                            focusPolicy: Qt.NoFocus
                            enabled: backend.usesSpeed
                            from: 0; to: 100; stepSize: 1
                            value: backend.speed
                            onMoved: backend.speed = value
                        }

                        Caption { text: qsTr("White balance"); Layout.topMargin: 8 }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            opacity: 0.6
                            font.pixelSize: 12
                            text: qsTr("If white looks pink, lower these. If it looks green, raise them.")
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Red"); opacity: 0.7; Layout.preferredWidth: 34 }
                            Slider {
                                Layout.fillWidth: true
                                focusPolicy: Qt.NoFocus
                                from: 5; to: 100; stepSize: 1
                                value: backend.whiteRed
                                onMoved: backend.whiteRed = value
                            }
                            Label { text: qsTr("%1%").arg(backend.whiteRed); opacity: 0.7 }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Blue"); opacity: 0.7; Layout.preferredWidth: 34 }
                            Slider {
                                Layout.fillWidth: true
                                focusPolicy: Qt.NoFocus
                                from: 5; to: 100; stepSize: 1
                                value: backend.whiteBlue
                                onMoved: backend.whiteBlue = value
                            }
                            Label { text: qsTr("%1%").arg(backend.whiteBlue); opacity: 0.7 }
                        }

                        Caption { text: qsTr("Paint"); Layout.topMargin: 8 }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            opacity: 0.5
                            font.pixelSize: 12
                            text: backend.preset === "custom"
                                ? qsTr("Click or drag across the keys above to colour them.")
                                : qsTr("Like this look? Freeze it and repaint single keys.")
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Button {
                                Layout.fillWidth: true
                                flat: true
                                focusPolicy: Qt.NoFocus
                                Material.roundedScale: theme.radius
                                text: qsTr("Paint this look")
                                enabled: backend.preset !== "custom" && backend.preset !== "onboard" && backend.power
                                onClicked: backend.paintFromCurrent()
                            }
                            Button {
                                Layout.fillWidth: true
                                flat: true
                                focusPolicy: Qt.NoFocus
                                Material.roundedScale: theme.radius
                                text: qsTr("Fill all")
                                onClicked: backend.fillCustom()
                            }
                        }

                        Item { Layout.fillHeight: true }

                        Switch {
                            Layout.fillWidth: true
                            focusPolicy: Qt.NoFocus
                            text: qsTr("Light up at login")
                            checked: backend.autostart
                            onToggled: backend.autostart = checked
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            // A layout's minimum width is its children's put together,
                            // and one wide row would widen the whole column past its frame.
                            Layout.minimumWidth: 0
                            Button {
                                flat: true
                                focusPolicy: Qt.NoFocus
                                Material.roundedScale: theme.radius
                                visible: backend.canSync
                                text: backend.taught ? qsTr("Sync keys again") : qsTr("Sync keys")
                                onClicked: backend.startQuickSync()
                            }
                            Item { Layout.fillWidth: true }
                            Label { visible: !win.narrow; opacity: 0.45; text: qsTr("? shortcuts") }
                        }
                    }
                }
            }
        }

        Onboarding {
            anchors.fill: parent
            visible: !backend.onboarded
        }

        TeachPanel {
            anchors.fill: parent
            visible: backend.teaching
        }

        Rectangle {
            anchors.fill: parent
            visible: win.helpVisible
            color: "#eb0e0e10"
            TapHandler { onTapped: win.helpVisible = false }
            GridLayout {
                anchors.centerIn: parent
                columns: 2
                columnSpacing: 28
                rowSpacing: 10
                Repeater {
                    model: [
                        ["Space", qsTr("Lights off and back on")],
                        ["← →", qsTr("Previous and next scene")],
                        ["↑ ↓", qsTr("Brightness")],
                        ["[ ]", qsTr("Speed")],
                        ["S", qsTr("Star or unstar the current scene")],
                        ["P", qsTr("Paint over the current look")],
                        ["F", qsTr("Fill every key with the colour")],
                        ["T", qsTr("Sync the lights to your keys")],
                        ["?", qsTr("Show or hide this")],
                        ["Q", qsTr("Quit (the lights stay on)")]
                    ]
                    Label {
                        required property var modelData
                        required property int index
                        Layout.columnSpan: 2
                        text: modelData[0] + "      " + modelData[1]
                        font.pixelSize: 16
                    }
                }
            }
        }
    }
}
