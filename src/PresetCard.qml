import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// One scene: a miniature of the keyboard lit that way, its name, and what it
// does. Hovering plays the miniature; the star keeps the scene in Favourites.
Rectangle {
    id: card

    property var preset
    property bool selected: false
    property int tick: 0
    signal picked()

    width: 190
    height: thumb.height + 62
    radius: theme.radius
    color: selected ? "#22222b" : hover.hovered ? "#1f1f26" : "#17171c"
    border.width: selected ? 2 : 1
    border.color: selected ? theme.accent : hover.hovered ? "#3a3a45" : "#26262d"
    scale: hover.hovered && !selected ? 1.02 : 1
    Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
    Behavior on color { ColorAnimation { duration: 110 } }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: card.picked() }
    Timer {
        interval: 90
        repeat: true
        running: hover.hovered
        onTriggered: card.tick++
    }

    Rectangle {
        id: thumb
        x: 8; y: 8
        width: parent.width - 16
        height: Math.round(width * 5 / 16) + 8
        radius: Math.min(6, theme.radius)
        color: "#0c0c0f"
        Image {
            anchors.fill: parent
            anchors.margins: 4
            fillMode: Image.PreserveAspectFit
            smooth: true
            cache: false
            // Stays left to right: it is a picture of the keyboard.
            mirror: false
            source: "image://scene/" + card.preset.id + "/" + backend.color.substring(1) + "/" + card.tick
                    + "/" + backend.layoutVersion + "/" + theme.accent.substring(1)
        }
    }

    Label {
        id: name
        anchors.left: parent.left; anchors.right: star.left
        anchors.top: thumb.bottom
        anchors.leftMargin: 10
        anchors.topMargin: 7
        text: card.preset.name + (card.preset.reactive ? "  ⚡" : "")
        font.pixelSize: 14
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }
    Label {
        id: star
        anchors.right: parent.right
        anchors.verticalCenter: name.verticalCenter
        anchors.rightMargin: 8
        text: card.preset.favorite ? "★" : "☆"
        font.pixelSize: 16
        color: card.preset.favorite ? "#ffd60a" : "#8a8a96"
        opacity: card.preset.favorite || hover.hovered ? 1 : 0
        // Its own tap target, so starring doesn't also pick the scene.
        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: backend.toggleFavorite(card.preset.id)
        }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
    }
    Label {
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: name.bottom
        anchors.leftMargin: 10; anchors.rightMargin: 10
        anchors.topMargin: 1
        text: card.preset.blurb
        font.pixelSize: 11
        opacity: 0.55
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        lineHeight: 0.95
        elide: Text.ElideRight
    }
}
