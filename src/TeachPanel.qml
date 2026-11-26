import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The one-time walk that learns which light sits under which key. Nothing
// here takes keyboard focus: every key press belongs to the walk.
Rectangle {
    id: panel
    color: "#f20e0e10"

    component Choice: Button {
        focusPolicy: Qt.NoFocus
        Material.roundedScale: theme.radius
        Layout.fillWidth: true
    }

    // Swallow clicks so nothing underneath reacts.
    MouseArea { anchors.fill: parent }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 520)
        spacing: 14

        Label {
            text: backend.teachQuick ? qsTr("Quick sync") : qsTr("Checking every light")
            font.pixelSize: 24
            font.weight: Font.DemiBold
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            opacity: 0.75
            text: backend.teachQuick
                ? qsTr("Your keyboard is glowing dim blue, with one key bright white. Press the white key. After about a dozen, Keyglow works out where every other light is.")
                : qsTr("Your keyboard is glowing dim blue, with one key bright white. Press the white key. This goes through all 68, about a minute.")
        }

        ProgressBar {
            Layout.fillWidth: true
            Layout.topMargin: 6
            from: 0; to: backend.teachExpected
            value: backend.teachFound
        }
        Label {
            text: backend.teachQuick ? qsTr("Step %1 of about %2").arg(backend.teachFound + 1).arg(backend.teachExpected)
                                     : qsTr("%1 of %2 keys matched").arg(backend.teachFound).arg(backend.teachExpected)
            opacity: 0.6
        }
        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            wrapMode: Text.Wrap
            color: "#ffcc66"
            text: backend.teachNote
        }

        Choice {
            Layout.topMargin: 8
            highlighted: true
            Material.foreground: theme.accentForeground
            text: qsTr("No key is white")
            onClicked: backend.teachNothing()
        }
        Choice {
            text: qsTr("It's white, but pressing it does nothing (the Fn key)")
            onClicked: backend.teachSilent(true)
        }
        Choice {
            text: qsTr("Something is white, but it isn't under a key")
            onClicked: backend.teachSilent(false)
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 8
            Choice { flat: true; text: backend.teachQuick ? qsTr("Start over") : qsTr("Back one"); onClicked: backend.teachBack() }
            Choice { flat: true; visible: backend.teachQuick; text: qsTr("Check every light"); onClicked: backend.startTeaching() }
            Choice { flat: true; visible: !backend.teachQuick; text: qsTr("Finish with these"); enabled: backend.teachFound > 0; onClicked: backend.finishTeaching() }
            Choice { flat: true; text: qsTr("Cancel"); onClicked: backend.cancelTeaching() }
        }
    }
}
