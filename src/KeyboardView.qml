import QtQuick
import QtQuick.Effects

// The keyboard as it sits on the desk: every cap shows the colour its LED has
// right now, with the light spilling between the keys, and dips when pressed.
Item {
    id: board

    property bool paintable: false
    readonly property real unit: Math.max(8, Math.min(width / (backend.boardWidth + 1.0),
                                                      height / (backend.boardHeight + 1.0)))
    // Stays left to right in every language: it mirrors a physical object.
    LayoutMirroring.enabled: false

    Rectangle {
        id: body
        anchors.centerIn: parent
        width: board.unit * (backend.boardWidth + 1.0)
        height: board.unit * (backend.boardHeight + 1.0)
        radius: Math.max(theme.radius, board.unit * 0.3)
        border.color: "#34343c"
        gradient: Gradient {
            GradientStop { position: 0; color: "#2a2a31" }
            GradientStop { position: 1; color: "#151518" }
        }
    }

    Item {
        id: plate
        x: body.x + board.unit * 0.5
        y: body.y + board.unit * 0.5
        width: board.unit * backend.boardWidth
        height: board.unit * backend.boardHeight

        // The light itself, blurred, under the caps.
        Item {
            id: light
            anchors.fill: parent
            visible: false
            Repeater {
                model: backend.keys
                Rectangle {
                    required property var modelData
                    x: modelData.x * board.unit
                    y: modelData.y * board.unit
                    width: modelData.w * board.unit
                    height: board.unit
                    color: { backend.frame; return backend.colorAt(modelData.led) }
                }
            }
        }
        MultiEffect {
            anchors.fill: light
            source: light
            blurEnabled: true
            blur: 1.0
            blurMax: Math.round(board.unit * 0.9)
            brightness: 0.12
            saturation: 0.25
        }

        Repeater {
            model: backend.keys
            Item {
                id: key
                required property var modelData
                readonly property color glow: { backend.frame; return backend.colorAt(modelData.led) }
                readonly property bool down: { backend.downTick; return backend.isDown(modelData.usage) }
                readonly property bool lit: glow.r + glow.g + glow.b > 0.06
                x: modelData.x * board.unit
                y: modelData.y * board.unit
                width: modelData.w * board.unit
                height: board.unit

                Rectangle {
                    id: cap
                    anchors.fill: parent
                    anchors.margins: board.unit * 0.07
                    radius: board.unit * 0.16
                    color: "#121215"
                    border.color: key.lit ? Qt.rgba(key.glow.r, key.glow.g, key.glow.b, 0.75) : "#2b2b33"
                    scale: key.down ? 0.9 : 1
                    Behavior on scale { NumberAnimation { duration: 70 } }

                    // The top face of the cap, a little lighter and inset.
                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: board.unit * 0.07
                        anchors.rightMargin: board.unit * 0.07
                        anchors.topMargin: board.unit * 0.04
                        anchors.bottomMargin: board.unit * 0.11
                        radius: board.unit * 0.12
                        gradient: Gradient {
                            GradientStop { position: 0; color: key.down ? "#1c1c21" : "#27272e" }
                            GradientStop { position: 1; color: "#1b1b20" }
                        }
                        Text {
                            anchors.centerIn: parent
                            text: key.modelData.label
                            // Shine-through legends: they take the LED's colour.
                            color: key.lit ? Qt.lighter(key.glow, 1.35) : "#4c4c57"
                            font.pixelSize: Math.round(board.unit * (text.length > 2 ? 0.2 : 0.3))
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            enabled: board.paintable
            preventStealing: true
            cursorShape: board.paintable ? Qt.CrossCursor : Qt.ArrowCursor
            function paintAt(px, py) {
                const kx = px / board.unit, ky = py / board.unit
                for (const k of backend.keys)
                    if (kx >= k.x && kx < k.x + k.w && ky >= k.y && ky < k.y + 1) {
                        backend.paint(k.led)
                        return
                    }
            }
            onPressed: mouse => paintAt(mouse.x, mouse.y)
            onPositionChanged: mouse => paintAt(mouse.x, mouse.y)
        }
    }
}
