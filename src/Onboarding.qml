import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The first run: say hello, match the lights to the keys, pick a look, and
// set it to come on at login. Four short steps, each skippable.
Rectangle {
    id: intro
    color: "#0e0e10"

    property int step: 0
    readonly property var featured: ["terracotta", "reactor", "aurora", "synthwave", "tokyonight", "heat", "matrix", "ember"]
    readonly property bool ready: backend.deviceStatus === "ready"

    function next() {
        if (step === 3)
            backend.finishOnboarding()
        else
            step++
    }
    // Lights at login is what most people expect, so the last step starts with
    // it on, in plain sight, where it can be switched off.
    onStepChanged: if (step === 3) backend.autostart = true

    component Primary: Button {
        focusPolicy: Qt.NoFocus
        highlighted: true
        Material.foreground: theme.accentForeground
        Material.roundedScale: theme.radius
        font.pixelSize: 15
        leftPadding: 22; rightPadding: 22
    }
    component Quiet: Button {
        focusPolicy: Qt.NoFocus
        flat: true
        Material.roundedScale: theme.radius
    }
    component Title: Label {
        font.pixelSize: 30
        font.weight: Font.DemiBold
        Layout.alignment: Qt.AlignHCenter
    }
    component Lead: Label {
        Layout.fillWidth: true
        Layout.maximumWidth: 560
        Layout.alignment: Qt.AlignHCenter
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        opacity: 0.7
        font.pixelSize: 15
    }

    MouseArea { anchors.fill: parent } // nothing underneath reacts

    // The glow of the current scene, behind everything.
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: -height * 0.45
        width: parent.width * 1.1
        height: parent.height * 1.1
        radius: width / 2
        opacity: 0.22
        gradient: Gradient {
            GradientStop { position: 0.35; color: backend.ambient }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    ColumnLayout {
        // Kept to a comfortable size, so on a large screen the steps and the
        // button that moves on stay together instead of drifting to the edges.
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 980)
        height: Math.min(parent.height - 64, 760)
        spacing: 16

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: intro.step

            // 1. Hello.
            ColumnLayout {
                spacing: 14
                Item { Layout.fillHeight: true }
                Title { text: qsTr("Welcome to Keyglow") }
                Lead { text: qsTr("Light for your keyboard: fifty scenes, from a quiet candle to keys that flare as you type. Here it is, live.") }
                KeyboardView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 250
                    Layout.maximumHeight: 300
                    Layout.topMargin: 6
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 8
                    Rectangle { width: 8; height: 8; radius: 4; color: intro.ready ? "#3ddc5a" : "#ff6b6b" }
                    Label {
                        opacity: 0.7
                        text: intro.ready ? qsTr("Your keyboard is connected and should be glowing right now.")
                            : backend.deviceStatus === "permission"
                                ? qsTr("The keyboard is plugged in, but access is blocked. Reinstall Keyglow, then replug it.")
                                : qsTr("No keyboard found yet. Plug a CB Artemis in with its cable, or start OpenRGB's server for any other keyboard.")
                    }
                }
                Item { Layout.fillHeight: true }
            }

            // 2. Match the lights to the keys.
            ColumnLayout {
                spacing: 14
                Item { Layout.fillHeight: true }
                Title { text: backend.taught ? qsTr("Lights and keys are in sync") : qsTr("First, a quick sync") }
                Lead {
                    text: backend.taught
                        ? qsTr("Keyglow knows exactly which light sits under which key. Every scene will land where it should.")
                        : qsTr("The keyboard doesn't say which light is under which key, so Keyglow asks. One key turns white, you press it, about a dozen times. It takes half a minute and only happens once.")
                }
                KeyboardView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 210
                    Layout.maximumHeight: 260
                    opacity: backend.taught ? 1 : 0.55
                }
                Primary {
                    Layout.alignment: Qt.AlignHCenter
                    visible: !backend.taught
                    enabled: intro.ready
                    text: qsTr("Start the sync")
                    onClicked: backend.startQuickSync()
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    visible: backend.taught
                    color: "#3ddc5a"
                    font.pixelSize: 16
                    text: qsTr("✓  All 68 keys matched")
                }
                Item { Layout.fillHeight: true }
            }

            // 3. Pick a look.
            ColumnLayout {
                spacing: 14
                Title { text: qsTr("Pick a look to start with") }
                Lead { text: qsTr("Click one and watch your keyboard change. There are dozens more inside, and you can star the ones you like.") }
                KeyboardView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 170
                    Layout.maximumHeight: 200
                }
                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentHeight: picks.implicitHeight
                    clip: true
                    Flow {
                        id: picks
                        width: parent.width
                        spacing: 10
                        readonly property int perRow: Math.max(2, Math.min(4, Math.floor((width + spacing) / 200)))
                        readonly property real cardWidth: Math.floor((width - spacing * (perRow - 1)) / perRow)
                        Repeater {
                            model: backend.presets.filter(p => intro.featured.indexOf(p.id) >= 0)
                            PresetCard {
                                required property var modelData
                                preset: modelData
                                width: picks.cardWidth
                                selected: backend.preset === modelData.id
                                onPicked: backend.preset = modelData.id
                            }
                        }
                    }
                }
            }

            // 4. Finishing touches.
            ColumnLayout {
                spacing: 14
                Item { Layout.fillHeight: true }
                Title { text: qsTr("You're set") }
                Lead { text: qsTr("The lights keep running after you close this window. A few things worth knowing:") }
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.fillWidth: true
                    Layout.maximumWidth: 560
                    implicitHeight: tips.implicitHeight + 32
                    radius: theme.radius
                    color: "#17171c"
                    border.color: "#26262d"
                    ColumnLayout {
                        id: tips
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12
                        Switch {
                            Layout.fillWidth: true
                            focusPolicy: Qt.NoFocus
                            text: qsTr("Light the keyboard up when I log in")
                            checked: backend.autostart
                            onToggled: backend.autostart = checked
                        }
                        Repeater {
                            model: [
                                ["Space", qsTr("turns the lights off and back on")],
                                ["← →", qsTr("flip through scenes")],
                                ["↑ ↓", qsTr("change the brightness")],
                                ["?", qsTr("shows every shortcut")],
                                ["keyglow next", qsTr("in a terminal or a key binding does the same from anywhere")]
                            ]
                            RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 12
                                Rectangle {
                                    implicitWidth: keyLabel.implicitWidth + 16
                                    implicitHeight: keyLabel.implicitHeight + 8
                                    radius: Math.min(4, theme.radius)
                                    color: "#26262d"
                                    Label { id: keyLabel; anchors.centerIn: parent; text: modelData[0]; font.family: "monospace"; font.pixelSize: 13 }
                                }
                                Label { Layout.fillWidth: true; text: modelData[1]; opacity: 0.75; wrapMode: Text.Wrap }
                            }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }

        // Where you are, and the way forward.
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Quiet {
                text: qsTr("Back")
                visible: intro.step > 0
                onClicked: intro.step--
            }
            Item { Layout.fillWidth: true }
            Row {
                spacing: 8
                Repeater {
                    model: 4
                    Rectangle {
                        required property int index
                        width: index === intro.step ? 22 : 8
                        height: 8
                        radius: 4
                        color: index === intro.step ? theme.accent : "#3a3a45"
                        Behavior on width { NumberAnimation { duration: 140 } }
                    }
                }
            }
            Item { Layout.fillWidth: true }
            Quiet {
                visible: intro.step === 1 && !backend.taught
                text: qsTr("Skip for now")
                onClicked: intro.next()
            }
            Primary {
                visible: !(intro.step === 1 && !backend.taught)
                text: intro.step === 0 ? qsTr("Get started") : intro.step === 3 ? qsTr("Open Keyglow") : qsTr("Continue")
                onClicked: intro.next()
            }
        }
    }
}
