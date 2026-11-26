import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Commons as Commons
import qs.Ui
import "Model.js" as Model

// Keyglow in the Omarchy bar: a keyboard icon that opens the scene picker.
// Everything goes through the `keyglow` command, which writes
// ~/.config/keyglow/config.json for the background streamer to follow, so the
// panel, the app window and key bindings all stay in step.
Panel {
  id: root
  moduleName: "io.github.nkoder1820-lang.keyglow"
  ipcTarget: "io.github.nkoder1820-lang.keyglow"

  readonly property color foreground: bar ? bar.foreground : Commons.Color.foreground
  readonly property color accent: Commons.Color.accent
  readonly property color dim: Qt.darker(foreground, 1.5)
  readonly property string fontFamily: bar ? bar.fontFamily : Style.font.family
  readonly property string keyboardGlyph: "\u{f030c}"
  readonly property int columns: 3

  // Before the first answer the panel assumes nothing is wrong, so the bar
  // icon does not flash a warning at login.
  property var status: ({ ok: true, problem: "", power: true, preset: "", name: "", brightness: 100, speed: 50,
                          color: "#ffffff", usesColor: false, usesSpeed: true, device: "ready", synced: true,
                          favorites: [] })
  property var scenes: []
  property var categories: []
  property string category: "all"
  property int cursor: -1
  property bool cursorActive: false

  readonly property var shownCategories: Model.visibleCategories(categories, scenes, status.favorites)
  readonly property var shownScenes: Model.visibleScenes(scenes, category, status.favorites)
  readonly property bool usable: status.ok && status.device === "ready"
  readonly property string helpLine: Model.helpLine(status)

  function refresh() {
    if (!statusProc.running) statusProc.running = true
  }

  function loadScenes() {
    if (!listProc.running) listProc.running = true
  }

  // Shows the change at once and lets the command catch up; the answer to the
  // next status read replaces the guess.
  function send(args, guess) {
    if (!status.ok) return
    if (guess) status = Object.assign({}, status, guess)
    Quickshell.execDetached(["keyglow"].concat(args))
    settle.restart()
  }

  function togglePower() { send([status.power ? "off" : "on"], { power: !status.power }) }

  function pickScene(scene) {
    if (!scene) return
    send(["preset", scene.id], { preset: scene.id, name: scene.name, power: true, usesColor: scene.usesColor })
  }

  function nudgeBrightness(by) {
    var next = Math.max(0, Math.min(100, status.brightness + by))
    send(["brightness", String(next)], { brightness: next })
  }

  function stepCategory(by) {
    var ids = shownCategories.map(function(c) { return c.id })
    if (ids.length === 0) return
    var at = Math.max(0, ids.indexOf(category))
    category = ids[(at + by + ids.length) % ids.length]
    cursor = -1
  }

  function openApp() {
    Quickshell.execDetached(["keyglow"])
    close()
  }

  onOpenedChanged: {
    if (!opened) return
    cursorActive = false
    refresh()
    loadScenes()
  }

  Component.onCompleted: { refresh(); loadScenes() }

  // A tab can empty out (the last star removed in the app) while it is open.
  onShownCategoriesChanged: {
    for (var i = 0; i < shownCategories.length; i++)
      if (shownCategories[i].id === category) return
    category = "all"
  }

  Process {
    id: statusProc
    // `command -v` first, so a missing keyglow is an answer and not a failed start.
    command: ["sh", "-c", "command -v keyglow >/dev/null 2>&1 || { echo missing; exit 0; }; keyglow status --json"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        // A slider being dragged knows better than a read that started before it moved.
        if (!settle.running) root.status = Model.parseStatus(text)
      }
    }
  }

  Process {
    id: listProc
    command: ["sh", "-c", "command -v keyglow >/dev/null 2>&1 && keyglow list --json"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        var parsed = Model.parseScenes(text)
        if (parsed.scenes.length === 0) return
        root.scenes = parsed.scenes
        root.categories = parsed.categories
      }
    }
  }

  // The app window, a key binding or another bar can change the lights too.
  FileView {
    path: Quickshell.env("HOME") + "/.config/keyglow/config.json"
    watchChanges: true
    printErrors: false
    onFileChanged: { reload(); root.refresh() }
  }

  Timer { id: settle; interval: 350; onTriggered: root.refresh() }

  // Plugging the keyboard in or out changes no file, so look while the panel is up.
  Timer { interval: 2500; running: root.opened; repeat: true; onTriggered: root.refresh() }

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  BarIconButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: root.keyboardGlyph
    dimmed: !root.usable || !root.status.power
    tooltipText: "Keyglow: " + Model.statusLine(root.status)
    onPressed: function(b) {
      if (b === Qt.RightButton) root.togglePower()
      else if (b === Qt.MiddleButton) root.send(["next"], null)
      else root.toggle()
    }
    onWheelMoved: function(delta) { root.nudgeBrightness(delta > 0 ? 5 : -5) }
  }

  KeyboardPanel {
    id: panel
    anchorItem: button
    owner: root
    bar: root.bar
    open: root.opened
    focusTarget: keyCatcher
    contentWidth: panel.fittedContentWidth(Style.space(400))
    contentHeight: panel.fittedContentHeight(column.implicitHeight)

    PanelKeyCatcher {
      id: keyCatcher
      anchors.fill: parent
      onMoveRequested: function(dx, dy) {
        if (!root.cursorActive || root.cursor < 0) {
          root.cursorActive = true
          root.cursor = Math.max(0, Model.indexOfScene(root.shownScenes, root.status.preset))
        } else {
          root.cursor = Model.moveIndex(root.cursor, dx, dy, root.columns, root.shownScenes.length)
        }
        grid.reveal(root.cursor)
      }
      onActivateRequested: {
        if (root.cursorActive && root.cursor >= 0) root.pickScene(root.shownScenes[root.cursor])
        else root.togglePower()
      }
      onCloseRequested: root.close()
      onTabRequested: function(direction) { root.switchPanel(direction) }
      onTextKey: function(t) {
        if (t === "p") root.togglePower()
        else if (t === "+" || t === "=") root.nudgeBrightness(10)
        else if (t === "-") root.nudgeBrightness(-10)
        else if (t === "]") root.stepCategory(1)
        else if (t === "[") root.stepCategory(-1)
        else if (t === "o") root.openApp()
      }

      Column {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: Style.space(12)

        PanelHero {
          foreground: root.foreground
          fontFamily: root.fontFamily
          title: "Keyglow"
          meta: Model.statusLine(root.status).toUpperCase()
          iconOpacity: root.usable && root.status.power ? 1.0 : 0.45
          iconComponent: Component {
            Text {
              textFormat: Text.PlainText
              text: root.keyboardGlyph
              color: root.foreground
              font.family: root.fontFamily
              font.pixelSize: Style.font.display
            }
          }
          trailingControl: Component {
            ToggleSwitch {
              visible: root.status.ok
              checked: root.status.power
              foreground: root.foreground
              accent: root.accent
              onToggled: root.togglePower()
            }
          }
        }

        Text {
          visible: root.helpLine !== ""
          width: parent.width
          textFormat: Text.PlainText
          text: root.helpLine
          wrapMode: Text.WordWrap
          color: root.status.ok && root.status.device === "ready" ? root.dim : (root.bar ? root.bar.urgent : Commons.Color.urgent)
          font.family: root.fontFamily
          font.pixelSize: Style.font.bodySmall
        }

        // ---------- Scenes ----------
        PanelSeparator { visible: root.scenes.length > 0; foreground: root.foreground }

        Flow {
          visible: root.scenes.length > 0
          width: parent.width
          spacing: Style.space(4)

          Repeater {
            model: root.shownCategories
            Button {
              required property var modelData
              text: modelData.name
              fontSize: Style.font.caption
              fontFamily: root.fontFamily
              foreground: root.foreground
              accent: root.accent
              horizontalPadding: Style.space(7)
              verticalPadding: Style.space(3)
              active: root.category === modelData.id
              onClicked: { root.category = modelData.id; root.cursor = -1 }
            }
          }
        }

        Flickable {
          id: grid
          visible: root.scenes.length > 0
          width: parent.width
          height: Math.min(contentHeight, tileHeight * 5 + gap * 4)
          contentHeight: tiles.implicitHeight
          clip: true
          boundsBehavior: Flickable.StopAtBounds

          readonly property int gap: Style.space(4)
          readonly property int tileWidth: Math.floor((width - gap * (root.columns - 1)) / root.columns)
          readonly property int tileHeight: Style.space(40)

          // Keeps the keyboard cursor's row on screen.
          function reveal(index) {
            if (index < 0) return
            var top = Math.floor(index / root.columns) * (tileHeight + gap)
            if (top < contentY) contentY = top
            else if (top + tileHeight > contentY + height) contentY = top + tileHeight - height
          }

          Grid {
            id: tiles
            columns: root.columns
            spacing: grid.gap

            Repeater {
              model: root.shownScenes
              CursorSurface {
                id: tile
                required property var modelData
                required property int index
                readonly property var chip: Model.chipColors(modelData, root.status.color, String(root.accent))

                width: grid.tileWidth
                height: grid.tileHeight
                foreground: root.foreground
                accent: root.accent
                bordered: true
                current: root.status.preset === modelData.id
                hasCursor: root.cursorActive && root.cursor === index

                Row {
                  id: chips
                  anchors.left: parent.left
                  anchors.right: parent.right
                  anchors.top: parent.top
                  anchors.margins: Style.space(6)
                  height: Style.space(6)
                  spacing: Style.space(2)
                  opacity: root.status.power ? 1.0 : 0.5

                  Repeater {
                    model: tile.chip
                    Rectangle {
                      required property var modelData
                      width: (chips.width - chips.spacing * (tile.chip.length - 1)) / tile.chip.length
                      height: chips.height
                      radius: Math.min(Style.cornerRadius, height / 2)
                      color: modelData
                    }
                  }

                  // The rainbow scenes and the keyboard's own effect have no fixed colours.
                  Rectangle {
                    visible: tile.chip.length === 0
                    width: chips.width
                    height: chips.height
                    radius: Math.min(Style.cornerRadius, height / 2)
                    gradient: Gradient {
                      orientation: Gradient.Horizontal
                      GradientStop { position: 0.0; color: tile.modelData.id === "onboard" ? Commons.Util.alpha(root.foreground, 0.25) : "#ff4d4d" }
                      GradientStop { position: 0.33; color: tile.modelData.id === "onboard" ? Commons.Util.alpha(root.foreground, 0.25) : "#ffd60a" }
                      GradientStop { position: 0.66; color: tile.modelData.id === "onboard" ? Commons.Util.alpha(root.foreground, 0.25) : "#3ddc84" }
                      GradientStop { position: 1.0; color: tile.modelData.id === "onboard" ? Commons.Util.alpha(root.foreground, 0.25) : "#2f8bff" }
                    }
                  }
                }

                Text {
                  anchors.left: parent.left
                  anchors.right: parent.right
                  anchors.bottom: parent.bottom
                  anchors.margins: Style.space(6)
                  textFormat: Text.PlainText
                  text: tile.modelData.name
                  elide: Text.ElideRight
                  color: root.foreground
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.bodySmall
                  font.bold: tile.current
                }

                MouseArea {
                  anchors.fill: parent
                  hoverEnabled: true
                  cursorShape: Qt.PointingHandCursor
                  onEntered: { root.cursorActive = true; root.cursor = tile.index }
                  onClicked: root.pickScene(tile.modelData)
                }
              }
            }
          }
        }

        // ---------- Brightness, speed, colour ----------
        PanelSeparator { visible: root.status.ok; foreground: root.foreground }

        SliderRow {
          visible: root.status.ok
          label: "Brightness"
          value: root.status.brightness
          onChosen: function(v) { root.send(["brightness", String(v)], { brightness: v }) }
        }

        SliderRow {
          visible: root.status.ok && root.status.usesSpeed
          label: "Speed"
          value: root.status.speed
          onChosen: function(v) { root.send(["speed", String(v)], { speed: v }) }
        }

        Row {
          visible: root.status.ok && root.status.usesColor
          width: parent.width
          spacing: Style.space(8)

          Text {
            width: Style.space(78)
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: "Colour"
            color: root.dim
            font.family: root.fontFamily
            font.pixelSize: Style.font.bodySmall
          }

          Repeater {
            model: [String(root.accent)].concat(Model.swatches)
            Rectangle {
              required property var modelData
              readonly property bool chosen: Qt.colorEqual(modelData, root.status.color)
              width: Style.space(20)
              height: width
              radius: Style.cornerRadius > 0 ? width / 2 : 0
              color: modelData
              border.width: chosen ? Math.max(2, Style.space(2)) : 1
              border.color: chosen ? root.foreground : Commons.Util.alpha(root.foreground, 0.25)

              MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                  var hex = String(Qt.color(parent.modelData)).substring(0, 7).toLowerCase()
                  root.send(["color", hex], { color: hex })
                }
              }
            }
          }
        }

        PanelSeparator { foreground: root.foreground }

        Item {
          width: parent.width
          implicitHeight: openButton.implicitHeight

          Text {
            anchors.left: parent.left
            anchors.right: openButton.left
            anchors.rightMargin: Style.space(8)
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: "Arrows pick · P power · [ ] tabs"
            elide: Text.ElideRight
            color: root.dim
            font.family: root.fontFamily
            font.pixelSize: Style.font.caption
          }

          Button {
            id: openButton
            anchors.right: parent.right
            visible: root.status.ok
            text: "Open Keyglow"
            fontSize: Style.font.bodySmall
            fontFamily: root.fontFamily
            foreground: root.foreground
            accent: root.accent
            bordered: true
            onClicked: root.openApp()
          }
        }
      }
    }
  }

  component SliderRow: Row {
    id: sliderRow
    property string label: ""
    property int value: 0
    signal chosen(int value)

    width: parent ? parent.width : 0
    spacing: Style.space(8)

    Text {
      width: Style.space(78)
      anchors.verticalCenter: parent.verticalCenter
      textFormat: Text.PlainText
      text: sliderRow.label
      color: root.dim
      font.family: root.fontFamily
      font.pixelSize: Style.font.bodySmall
    }

    PanelSlider {
      width: parent.width - Style.space(78) - Style.space(36) - parent.spacing * 2
      anchors.verticalCenter: parent.verticalCenter
      bar: root.bar
      minimum: 0
      maximum: 100
      step: 5
      integer: true
      value: sliderRow.value
      onMoved: function(v) { sliderRow.chosen(Math.round(v)) }
    }

    Text {
      width: Style.space(36)
      anchors.verticalCenter: parent.verticalCenter
      horizontalAlignment: Text.AlignRight
      textFormat: Text.PlainText
      text: sliderRow.value + "%"
      color: root.foreground
      font.family: root.fontFamily
      font.pixelSize: Style.font.bodySmall
    }
  }
}
