// Runs the bar panel's Model.js under node: node tests/plugin_model.js
const fs = require("fs"), path = require("path"), vm = require("vm"), assert = require("assert")

const source = fs.readFileSync(path.join(__dirname, "..", "Model.js"), "utf8").replace(/^\.pragma library$/m, "")
const M = {}
vm.runInNewContext(source + "\nObject.assign(out, {parseStatus, parseScenes, visibleScenes, visibleCategories, indexOfScene, moveIndex, chipColors, statusLine, helpLine, swatches})", { out: M })
const plain = (v) => JSON.parse(JSON.stringify(v))

// Status: a good answer, an old keyglow, and no keyglow at all.
const good = M.parseStatus('{"brightness":140,"color":"#2F8BFF","device":"ready","favorites":["nord"],"name":"Ripple","power":true,"preset":"ripple","speed":50,"synced":true,"usesColor":true,"usesSpeed":true}')
assert.strictEqual(good.ok, true)
assert.strictEqual(good.brightness, 100)
assert.strictEqual(good.color, "#2f8bff")
assert.deepStrictEqual(plain(good.favorites), ["nord"])
assert.strictEqual(M.statusLine(good), "Ripple")
assert.strictEqual(M.helpLine(good), "")
assert.strictEqual(M.parseStatus("on ripple 100% #2f8bff\n").problem, "too-old")
assert.strictEqual(M.parseStatus("missing\n").problem, "not-installed")
assert.strictEqual(M.parseStatus("").problem, "not-installed")
assert.strictEqual(M.statusLine(M.parseStatus("missing")), "Not installed")
assert.strictEqual(M.statusLine(Object.assign({}, good, { power: false })), "Lights off")
assert.strictEqual(M.statusLine(Object.assign({}, good, { device: "missing" })), "Keyboard unplugged")
assert.ok(M.helpLine(Object.assign({}, good, { device: "permission" })).includes("plug it back in"))
assert.ok(M.helpLine(Object.assign({}, good, { synced: false })).includes("Open Keyglow"))

// Scenes: junk is dropped, tabs filter, Favourites hides until something is starred.
const list = M.parseScenes(JSON.stringify({
  categories: [{ id: "all", name: "All" }, { id: "favorites", name: "Favourites" }, { id: "themes", name: "Themes" }, { id: "work", name: "Work" }],
  scenes: [
    { id: "nord", name: "Nord", category: "themes", palette: ["#000000", "#5e81ac", "#88c0d0"] },
    { id: "theme", name: "Omarchy", category: "themes", palette: [] },
    { id: "solid", name: "Solid", category: "basics", usesColor: true, palette: [] },
    { id: "rainbow", name: "Rainbow", category: "basics", palette: [] },
    null, { name: "no id" }
  ]
}))
assert.strictEqual(list.scenes.length, 4)
assert.deepStrictEqual(plain(M.parseScenes("not json")), { categories: [], scenes: [] })
assert.deepStrictEqual(plain(M.visibleScenes(list.scenes, "themes", []).map((s) => s.id)), ["nord", "theme"])
assert.deepStrictEqual(plain(M.visibleScenes(list.scenes, "favorites", ["solid"]).map((s) => s.id)), ["solid"])
assert.strictEqual(M.visibleScenes(list.scenes, "all", []).length, 4)
assert.deepStrictEqual(plain(M.visibleCategories(list.categories, list.scenes, []).map((c) => c.id)), ["all", "themes"])
assert.deepStrictEqual(plain(M.visibleCategories(list.categories, list.scenes, ["nord"]).map((c) => c.id)), ["all", "favorites", "themes"])
assert.strictEqual(M.indexOfScene(list.scenes, "solid"), 2)
assert.strictEqual(M.indexOfScene(list.scenes, "nope"), -1)

// Chips: dark filler colours are skipped, long palettes keep both ends.
assert.deepStrictEqual(plain(M.chipColors(list.scenes[0], "#ff0000", "#00ff00")), ["#5e81ac", "#88c0d0"])
assert.deepStrictEqual(plain(M.chipColors(list.scenes[1], "#ff0000", "#00ff00")), ["#00ff00"])
assert.deepStrictEqual(plain(M.chipColors(list.scenes[2], "#ff0000", "#00ff00")), ["#ff0000"])
assert.deepStrictEqual(plain(M.chipColors(list.scenes[3], "#ff0000", "#00ff00")), [])
const long = { id: "x", usesColor: false, palette: ["#110000", "#aa0000", "#bb0000", "#cc0000", "#dd0000", "#ee0000", "#ff0000"] }
assert.deepStrictEqual(plain(M.chipColors(long, "", "")), ["#aa0000", "#cc0000", "#dd0000", "#ff0000"])

// Arrow keys in a 3-wide grid of 8 (rows: 0-2, 3-5, 6-7).
assert.strictEqual(M.moveIndex(-1, 1, 0, 3, 8), 0)
assert.strictEqual(M.moveIndex(0, -1, 0, 3, 8), 0)
assert.strictEqual(M.moveIndex(1, 0, 1, 3, 8), 4)
assert.strictEqual(M.moveIndex(5, 0, 1, 3, 8), 7) // no key below: land on the last one
assert.strictEqual(M.moveIndex(7, 0, 1, 3, 8), 7)
assert.strictEqual(M.moveIndex(7, 1, 0, 3, 8), 7)
assert.strictEqual(M.moveIndex(1, 0, -1, 3, 8), 1)
assert.strictEqual(M.moveIndex(0, 0, 0, 3, 0), -1)

// The real command, when it has been built.
const built = path.join(__dirname, "..", "build", "keyglow")
if (fs.existsSync(built)) {
  const run = (args) => require("child_process").execFileSync(built, args, { encoding: "utf8" })
  const real = M.parseScenes(run(["list", "--json"]))
  assert.ok(real.scenes.length >= 50)
  assert.ok(real.categories.length >= 6)
  assert.strictEqual(M.parseStatus(run(["status", "--json"])).ok, true)
}
console.log("plugin model: ok")
