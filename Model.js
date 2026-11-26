.pragma library

// Pure helpers for the Keyglow bar panel: turning what the `keyglow` command
// prints into what the panel draws. No QML types here, so tests/plugin_model.js
// can run this file under node.

var swatches = ["#ff4d4d", "#ff8800", "#ffd60a", "#3ddc84", "#2fd4c4", "#2f8bff", "#7d5cff", "#ff5ca8", "#ffffff"]

// `keyglow status --json`. Anything else (a keyglow older than 1.1 answers in
// plain text, or the command is not installed) comes back as a problem to show.
function parseStatus(text) {
  var raw = String(text || "").trim()
  if (raw === "missing") return { ok: false, problem: "not-installed" }
  var data
  try { data = JSON.parse(raw) } catch (e) { data = null }
  if (!data || typeof data !== "object" || typeof data.preset !== "string")
    return { ok: false, problem: raw === "" ? "not-installed" : "too-old" }
  return {
    ok: true,
    problem: "",
    power: data.power === true,
    preset: data.preset,
    name: String(data.name || data.preset),
    brightness: percent(data.brightness, 100),
    speed: percent(data.speed, 50),
    color: /^#[0-9a-f]{6}$/i.test(String(data.color)) ? String(data.color).toLowerCase() : "#ffffff",
    usesColor: data.usesColor === true,
    usesSpeed: data.usesSpeed === true,
    device: String(data.device || "missing"),
    synced: data.synced === true,
    favorites: Array.isArray(data.favorites) ? data.favorites.map(String) : []
  }
}

// `keyglow list --json`.
function parseScenes(text) {
  var data
  try { data = JSON.parse(String(text || "")) } catch (e) { data = null }
  var empty = { categories: [], scenes: [] }
  if (!data || !Array.isArray(data.scenes)) return empty
  var scenes = []
  for (var i = 0; i < data.scenes.length; i++) {
    var s = data.scenes[i]
    if (!s || typeof s.id !== "string") continue
    scenes.push({
      id: s.id,
      name: String(s.name || s.id),
      blurb: String(s.blurb || ""),
      category: String(s.category || ""),
      reactive: s.reactive === true,
      usesColor: s.usesColor === true,
      palette: Array.isArray(s.palette) ? s.palette.map(String) : []
    })
  }
  var categories = []
  var listed = Array.isArray(data.categories) ? data.categories : []
  for (var j = 0; j < listed.length; j++) {
    if (listed[j] && typeof listed[j].id === "string")
      categories.push({ id: listed[j].id, name: String(listed[j].name || listed[j].id) })
  }
  return { categories: categories, scenes: scenes }
}

function percent(value, fallback) {
  var n = Number(value)
  if (!isFinite(n)) return fallback
  return Math.max(0, Math.min(100, Math.round(n)))
}

// The scenes under one tab. "favorites" follows the stars set in the app.
function visibleScenes(scenes, category, favorites) {
  var out = []
  for (var i = 0; i < scenes.length; i++) {
    var s = scenes[i]
    if (category === "all" || category === "" || s.category === category
        || (category === "favorites" && favorites.indexOf(s.id) >= 0))
      out.push(s)
  }
  return out
}

// Tabs worth showing: Favourites only once something is starred, and no tab
// that would open empty.
function visibleCategories(categories, scenes, favorites) {
  var out = []
  for (var i = 0; i < categories.length; i++) {
    var id = categories[i].id
    if (id === "all" || visibleScenes(scenes, id, favorites).length > 0) out.push(categories[i])
  }
  return out
}

function indexOfScene(scenes, id) {
  for (var i = 0; i < scenes.length; i++)
    if (scenes[i].id === id) return i
  return -1
}

// Arrow keys in a grid `columns` wide. Sideways stops at the ends of the list;
// up and down stop at the first and last row instead of wrapping.
function moveIndex(index, dx, dy, columns, count) {
  if (count <= 0) return -1
  if (index < 0) return 0
  var next = index + dx + dy * columns
  if (next < 0 || next >= count) {
    if (dy > 0 && Math.floor(index / columns) < Math.floor((count - 1) / columns)) return count - 1
    return index
  }
  return next
}

// Up to four colours to draw a scene's chip with. Scenes that take the user's
// colour show that; the Omarchy scene shows the desktop accent.
function chipColors(scene, userColor, accent) {
  if (scene.palette.length > 0) {
    var lit = []
    for (var i = 0; i < scene.palette.length; i++)
      if (brightness(scene.palette[i]) > 0.16) lit.push(scene.palette[i])
    var from = lit.length > 0 ? lit : scene.palette
    if (from.length <= 4) return from
    // Spread evenly, so a long palette still shows both of its ends.
    var out = []
    for (var k = 0; k < 4; k++) out.push(from[Math.round(k * (from.length - 1) / 3)])
    return out
  }
  if (scene.id === "theme") return [accent]
  if (scene.usesColor) return [userColor]
  return []
}

function brightness(hex) {
  var m = /^#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(String(hex))
  if (!m) return 0
  return Math.max(parseInt(m[1], 16), parseInt(m[2], 16), parseInt(m[3], 16)) / 255
}

// The small line under the title.
function statusLine(status) {
  if (!status.ok) return status.problem === "too-old" ? "Update needed" : "Not installed"
  if (status.device === "missing") return "Keyboard unplugged"
  if (status.device === "permission") return "No access to the keyboard"
  if (!status.power) return "Lights off"
  return status.name
}

// What to tell the user to do about it, or "" when all is well.
function helpLine(status) {
  if (!status.ok) {
    return status.problem === "too-old"
      ? "This panel needs Keyglow 1.1 or newer. Run bin/install again from the plugin folder."
      : "The keyglow command is not installed. Run bin/install from the plugin folder."
  }
  if (status.device === "permission")
    return "Unplug the keyboard and plug it back in, so the new access rule applies."
  if (status.device === "missing")
    return "No keyboard found. Plug in a CB Artemis by USB cable, or start OpenRGB's server (openrgb --server) for any other keyboard."
  if (!status.synced) return "Open Keyglow once to match the lights to your keys."
  return ""
}
