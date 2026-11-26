#include "lighting.h"

#include <QHash>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <climits>
#include <cmath>

using Lighting::Leds;
using Lighting::Rows;

static QString configDir() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/keyglow";
}

static bool writeJson(const QString &path, const QJsonObject &o) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    // Written beside the target and renamed, so the streamer never reads half a file.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    return file.commit();
}

static QJsonObject readJson(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

// ---------------------------------------------------------------- Settings

Settings::Settings() : custom(Leds, QColor("#ffffff")) {}

QString Settings::defaultPath() { return configDir() + "/config.json"; }

Settings Settings::load(const QString &path) {
    Settings s;
    const QJsonObject o = readJson(path);
    // Scenes that were renamed keep working for settings saved before.
    static const QHash<QString, QString> renamed = {
        {"claude", "terracotta"}, {"arc", "reactor"}, {"nightcity", "neoncity"},
        {"vibranium", "kinetic"}, {"webslinger", "swing"}, {"gotham", "nightwatch"}};
    auto current = [](const QString &id) { return renamed.value(id, id); };
    for (const Preset &p : Engine::presets())
        if (p.id == current(o.value("preset").toString()))
            s.preset = p.id;
    s.power = o.value("power").toBool(true);
    s.onboarded = o.value("onboarded").toBool(false);
    for (const QJsonValue &v : o.value("favorites").toArray()) {
        const QString id = current(v.toString());
        if (Engine::preset(id).id == id && !s.favorites.contains(id))
            s.favorites.append(id);
    }
    if (o.value("whiteRed").toInt() > 0)
        s.whiteRed = qBound(5, o.value("whiteRed").toInt(), 100);
    if (o.value("whiteBlue").toInt() > 0)
        s.whiteBlue = qBound(5, o.value("whiteBlue").toInt(), 100);
    if (QColor c(o.value("color").toString()); c.isValid())
        s.color = c;
    s.brightness = qBound(0, o.value("brightness").toInt(100), 100);
    s.speed = qBound(0, o.value("speed").toInt(50), 100);
    s.identify = qBound(-1, o.value("identify").toInt(-1), Lighting::MaxLeds - 1);
    const QJsonArray custom = o.value("custom").toArray();
    // Painted colours are kept per LED, for however many the keyboard has.
    if (custom.size() > s.custom.size())
        s.custom.resize(qMin(int(custom.size()), Lighting::MaxLeds), QColor("#ffffff"));
    for (int i = 0; i < s.custom.size() && i < custom.size(); ++i)
        if (QColor c(custom.at(i).toString()); c.isValid())
            s.custom[i] = c;
    return s;
}

bool Settings::save(const QString &path) const {
    QJsonArray list;
    for (const QColor &c : custom)
        list.append(c.name());
    return writeJson(path, {{"power", power}, {"onboarded", onboarded},
                            {"favorites", QJsonArray::fromStringList(favorites)}, {"preset", preset}, {"color", color.name()},
                            {"brightness", brightness}, {"speed", speed}, {"identify", identify},
                            {"whiteRed", whiteRed}, {"whiteBlue", whiteBlue}, {"custom", list}});
}

bool Settings::operator==(const Settings &o) const {
    return power == o.power && onboarded == o.onboarded && favorites == o.favorites && preset == o.preset
        && color == o.color && brightness == o.brightness && speed == o.speed && custom == o.custom
        && identify == o.identify && whiteRed == o.whiteRed && whiteBlue == o.whiteBlue;
}

// ---------------------------------------------------------------- Layout

namespace {
struct Slot { double w; int usage; };
// The 68-key, 65% arrangement the Artemis uses, row by row. The usages are
// the starting guess; teaching replaces them with what the keys really send.
const QVector<QVector<Slot>> kTemplate = {
    {{1, 0x29}, {1, 0x1E}, {1, 0x1F}, {1, 0x20}, {1, 0x21}, {1, 0x22}, {1, 0x23}, {1, 0x24}, {1, 0x25},
     {1, 0x26}, {1, 0x27}, {1, 0x2D}, {1, 0x2E}, {2, 0x2A}, {1, 0x35}},
    {{1.5, 0x2B}, {1, 0x14}, {1, 0x1A}, {1, 0x08}, {1, 0x15}, {1, 0x17}, {1, 0x1C}, {1, 0x18}, {1, 0x0C},
     {1, 0x12}, {1, 0x13}, {1, 0x2F}, {1, 0x30}, {1.5, 0x31}, {1, 0x4C}},
    {{1.75, 0x39}, {1, 0x04}, {1, 0x16}, {1, 0x07}, {1, 0x09}, {1, 0x0A}, {1, 0x0B}, {1, 0x0D}, {1, 0x0E},
     {1, 0x0F}, {1, 0x33}, {1, 0x34}, {2.25, 0x28}, {1, 0x4B}},
    {{2.25, 0xE1}, {1, 0x1D}, {1, 0x1B}, {1, 0x06}, {1, 0x19}, {1, 0x05}, {1, 0x11}, {1, 0x10}, {1, 0x36},
     {1, 0x37}, {1, 0x38}, {1.75, 0xE5}, {1, 0x52}, {1, 0x4E}},
    {{1.25, 0xE0}, {1.25, 0xE3}, {1.25, 0xE2}, {6.25, 0x2C}, {1, 0xE6}, {1, 0}, {1, 0xE4}, {1, 0x50},
     {1, 0x51}, {1, 0x4F}},
};
// Matrix columns the bottom row most likely uses, for the untaught guess.
const int kBottomColumns[] = {0, 1, 2, 5, 9, 10, 11, 12, 13, 14};
}

QString KeyboardLayout::labelFor(int usage) {
    static const QMap<int, QString> names = {
        {0x28, "Enter"}, {0x29, "Esc"}, {0x2A, "Bksp"}, {0x2B, "Tab"}, {0x2C, ""}, {0x2D, "-"}, {0x2E, "="},
        {0x2F, "["}, {0x30, "]"}, {0x31, "\\"}, {0x33, ";"}, {0x34, "'"}, {0x35, "`"}, {0x36, ","},
        {0x37, "."}, {0x38, "/"}, {0x39, "Caps"}, {0x46, "PrtSc"}, {0x47, "ScrLk"}, {0x48, "Pause"},
        {0x49, "Ins"}, {0x4A, "Home"}, {0x4B, "PgUp"}, {0x4C, "Del"}, {0x4D, "End"}, {0x4E, "PgDn"},
        {0x4F, "→"}, {0x50, "←"}, {0x51, "↓"}, {0x52, "↑"}, {0x65, "Menu"}, {0xE0, "Ctrl"},
        {0xE1, "Shift"}, {0xE2, "Alt"}, {0xE3, "Super"}, {0xE4, "Ctrl"}, {0xE5, "Shift"}, {0xE6, "Alt"},
        {0xE7, "Super"}};
    if (usage == 0)
        return "Fn";
    if (usage >= 0x04 && usage <= 0x1D)
        return QString(QChar('A' + usage - 0x04));
    if (usage >= 0x1E && usage <= 0x26)
        return QString(QChar('1' + usage - 0x1E));
    if (usage == 0x27)
        return "0";
    if (usage >= 0x3A && usage <= 0x45)
        return "F" + QString::number(usage - 0x39);
    return names.value(usage, "?");
}

KeyboardLayout::KeyboardLayout() {
    QVector<QPair<int, int>> guess;
    for (int r = 0; r < kTemplate.size(); ++r)
        for (int j = 0; j < kTemplate[r].size(); ++j)
            guess.append({(r == 4 ? kBottomColumns[j] : j) * Rows + r, kTemplate[r][j].usage});
    arrange(guess);
}

QString KeyboardLayout::defaultPath() { return configDir() + "/layout.json"; }

KeyboardLayout KeyboardLayout::fromPairs(const QVector<QPair<int, int>> &ledUsage) {
    KeyboardLayout layout;
    if (!ledUsage.isEmpty()) {
        layout.arrange(ledUsage);
        layout.m_taught = true;
    }
    return layout;
}

KeyboardLayout KeyboardLayout::placed(const QVector<Key> &keys, const QVector<Key> &lights, int leds) {
    KeyboardLayout layout;
    layout.m_leds = qBound(1, leds, Lighting::MaxLeds);
    layout.m_keys.clear();
    layout.m_extras.clear();
    layout.m_width = layout.m_height = 1;
    for (const Key &k : keys)
        if (k.led >= 0 && k.led < layout.m_leds) {
            layout.m_keys.append(k);
            layout.m_width = qMax(layout.m_width, k.x + k.w);
            layout.m_height = qMax(layout.m_height, k.y + 1);
        }
    for (const Key &k : lights)
        if (k.led >= 0 && k.led < layout.m_leds) {
            layout.m_lights.append(k);
            layout.m_extras.append(k.led);
        }
    layout.m_taught = layout.m_placed = true;
    return layout;
}

KeyboardLayout KeyboardLayout::load(const QString &path) {
    const QJsonObject file = readJson(path);
    if (file.value("placed").toBool()) {
        QVector<Key> keys, lights;
        for (const QJsonValue &v : file.value("keys").toArray()) {
            const QJsonObject o = v.toObject();
            Key k;
            k.led = o.value("led").toInt(-1);
            k.usage = o.value("usage").toInt();
            k.x = o.value("x").toDouble();
            k.y = o.value("y").toDouble();
            k.w = qMax(0.25, o.value("w").toDouble(1));
            k.label = o.value("label").toString();
            (k.usage < 0 ? lights : keys).append(k);
        }
        if (!keys.isEmpty())
            return placed(keys, lights, file.value("leds").toInt());
        return {};
    }
    QVector<QPair<int, int>> pairs;
    for (const QJsonValue &v : file.value("keys").toArray()) {
        const int led = v.toObject().value("led").toInt(-1);
        if (led >= 0 && led < Leds)
            pairs.append({led, v.toObject().value("usage").toInt()});
    }
    return fromPairs(pairs);
}

bool KeyboardLayout::save(const QString &path) const {
    QJsonArray keys;
    if (m_placed) {
        auto entry = [](const Key &k, int usage) {
            return QJsonObject{{"led", k.led}, {"usage", usage}, {"x", k.x}, {"y", k.y}, {"w", k.w}, {"label", k.label}};
        };
        for (const Key &k : m_keys)
            keys.append(entry(k, k.usage));
        for (const Key &k : m_lights)
            keys.append(entry(k, -1));
        return writeJson(path, {{"placed", true}, {"leds", m_leds}, {"keys", keys}});
    }
    for (const Key &k : m_keys)
        keys.append(QJsonObject{{"led", k.led}, {"usage", k.usage}});
    for (int led : m_extras)
        keys.append(QJsonObject{{"led", led}, {"usage", -1}});
    return writeJson(path, {{"keys", keys}});
}

// Places keys from their matrix positions: the matrix row is the physical
// row, and within a row the matrix column gives left-to-right order. When the
// rows have the Artemis's key counts the real key widths are used; anything
// else falls back to a plain grid so an unexpected board is still usable.
void KeyboardLayout::arrange(QVector<QPair<int, int>> ledUsage) {
    std::sort(ledUsage.begin(), ledUsage.end());
    QMap<int, QVector<QPair<int, int>>> rows; // matrix row -> (column, usage)
    m_extras.clear();
    for (const auto &p : ledUsage) {
        if (p.second < 0)
            m_extras.append(p.first);
        else
            rows[p.first % Rows].append({p.first / Rows, p.second});
    }

    bool matches = rows.size() == kTemplate.size();
    int r = 0;
    for (auto it = rows.begin(); matches && it != rows.end(); ++it, ++r)
        matches = it->size() == kTemplate[r].size();

    m_keys.clear();
    m_width = 1;
    r = 0;
    for (auto it = rows.begin(); it != rows.end(); ++it, ++r) {
        double x = 0;
        for (int j = 0; j < it->size(); ++j) {
            Key k;
            k.led = (*it)[j].first * Rows + it.key();
            k.usage = (*it)[j].second;
            k.w = matches ? kTemplate[r][j].w : 1;
            k.x = matches ? x : (*it)[j].first;
            k.y = r;
            k.label = labelFor(k.usage);
            x += k.w;
            m_width = qMax(m_width, k.x + k.w);
            m_keys.append(k);
        }
    }
    m_height = qMax(1, int(rows.size()));
}

int KeyboardLayout::ledForUsage(int usage) const {
    for (const Key &k : m_keys)
        if (k.usage == usage && usage > 0)
            return k.led;
    return -1;
}

// ---------------------------------------------------------------- QuickSync

namespace {
// How each row may be wired: runs of keys in consecutive columns, each run
// starting `lo` to `hi` columns after the previous run ends.
struct Run { int keys, lo, hi; };
const QVector<QVector<Run>> kRuns = {
    {{13, 0, 0}, {1, 0, 1}, {1, 0, 2}},                       // Esc..=, Backspace, corner key
    {{13, 0, 0}, {1, 0, 1}, {1, 0, 2}},                       // Tab..], backslash, corner key
    {{12, 0, 0}, {1, 0, 2}, {1, 0, 3}},                       // Caps..', Enter, corner key
    {{1, 0, 0}, {10, 0, 1}, {1, 0, 2}, {1, 0, 2}, {1, 0, 2}}, // Shift, Z../, Shift, Up, corner key
    {{3, 0, 0}, {1, 0, 4}, {3, 0, 4}, {3, 0, 3}},             // Ctrl Super Alt, Space, Alt Fn Ctrl, arrows
};
const int kLastColumn = 17;

void expand(const QVector<Run> &runs, int index, int column, QVector<int> row, QVector<QVector<int>> &out) {
    if (index == runs.size()) {
        out.append(row);
        return;
    }
    for (int gap = runs[index].lo; gap <= runs[index].hi; ++gap) {
        QVector<int> next = row;
        for (int k = 0; k < runs[index].keys; ++k)
            next.append(column + gap + k);
        if (next.last() <= kLastColumn)
            expand(runs, index + 1, next.last() + 1, next, out);
    }
}
// The right-hand column's keys differ between Artemis batches, so what they
// send is learned rather than assumed.
bool isCorner(int row, int slot) { return row < 4 && slot == kTemplate[row].size() - 1; }
}

QuickSync::QuickSync() {
    for (auto &rows : m_rows)
        for (const auto &runs : kRuns) {
            QVector<Row> wirings;
            expand(runs, 0, 0, {}, wirings);
            rows.append(wirings);
        }
}

bool QuickSync::failed() const { return !m_alive[0] && !m_alive[1]; }

int QuickSync::offset() const { return m_alive[0] ? 0 : 1; }

void QuickSync::keep(int offset, int row, const std::function<bool(const Row &)> &fits) {
    if (!m_alive[offset])
        return;
    if (row < 0 || row >= kTemplate.size()) {
        m_alive[offset] = false;
        return;
    }
    QVector<Row> &wirings = m_rows[offset][row];
    wirings.erase(std::remove_if(wirings.begin(), wirings.end(), [&](const Row &r) { return !fits(r); }),
                  wirings.end());
    if (wirings.isEmpty())
        m_alive[offset] = false;
}

void QuickSync::place(int led, int row, int slot) {
    for (int o = 0; o < 2; ++o) {
        if (led % Rows - o != row)
            m_alive[o] = false;
        else
            keep(o, row, [&](const Row &r) { return r[slot] == led / Rows; });
    }
}

void QuickSync::pressed(int led, int usage) {
    ++m_answers;
    m_answered.insert(led);
    for (int r = 0; r < kTemplate.size(); ++r)
        for (int j = 0; j < kTemplate[r].size(); ++j)
            if (kTemplate[r][j].usage == usage && usage > 0 && !isCorner(r, j)) {
                place(led, r, j);
                return;
            }
    // Not a key with a fixed place, so it is one of the corner keys, in
    // whichever row this light belongs to.
    m_extraUsage.insert(led, usage);
    for (int o = 0; o < 2; ++o) {
        const int row = led % Rows - o;
        if (row > 3)
            m_alive[o] = false;
        else
            keep(o, row, [&](const Row &r) { return r.last() == led / Rows; });
    }
}

void QuickSync::silentKey(int led) {
    ++m_answers;
    m_answered.insert(led);
    place(led, 4, 5); // Fn
}

void QuickSync::nothing(int led) {
    ++m_answers;
    m_answered.insert(led);
    for (int o = 0; o < 2; ++o) {
        const int row = led % Rows - o;
        if (row >= 0 && row < kTemplate.size())
            keep(o, row, [&](const Row &r) { return !r.contains(led / Rows); });
    }
}

int QuickSync::nextLed() const {
    if (failed())
        return -1;
    // The first light settles which matrix rows the board uses.
    if (m_alive[0] && m_alive[1])
        return m_answered.contains(0) ? 1 : 0;
    const int o = offset();
    const auto &rows = m_rows[o];

    // Still several wirings for a row: ask about the column that splits them
    // most evenly, preferring one where a key is expected over a blank.
    for (int r = 0; r < rows.size(); ++r) {
        if (rows[r].size() < 2)
            continue;
        int best = -1, bestWorst = INT_MAX, bestBlank = INT_MAX;
        for (int column = 0; column <= kLastColumn; ++column) {
            if (m_answered.contains(column * Rows + r + o))
                continue;
            QHash<int, int> groups;
            for (const Row &w : rows[r])
                ++groups[w.indexOf(column)];
            if (groups.size() < 2)
                continue;
            const int worst = *std::max_element(groups.begin(), groups.end());
            const int blank = groups.value(-1);
            if (worst < bestWorst || (worst == bestWorst && blank < bestBlank)) {
                best = column;
                bestWorst = worst;
                bestBlank = blank;
            }
        }
        if (best >= 0)
            return best * Rows + r + o;
    }

    // One wiring left. Learn what the corner keys send, then spot-check Space,
    // Enter and the right arrow: far-apart keys that would expose a wrong map.
    QVector<int> checks;
    for (int r = 0; r < 4; ++r)
        checks.append(rows[r].first().last() * Rows + r + o);
    checks << rows[4].first()[3] * Rows + 4 + o << rows[2].first()[12] * Rows + 2 + o
           << rows[4].first()[9] * Rows + 4 + o;
    for (int led : checks)
        if (!m_answered.contains(led))
            return led;
    return -1;
}

QVector<QPair<int, int>> QuickSync::result() const {
    QVector<QPair<int, int>> pairs;
    if (failed())
        return pairs;
    const int o = offset();
    for (int r = 0; r < kTemplate.size(); ++r)
        for (int j = 0; j < kTemplate[r].size(); ++j) {
            const int led = m_rows[o][r].first()[j] * Rows + r + o;
            pairs.append({led, isCorner(r, j) ? m_extraUsage.value(led, kTemplate[r][j].usage)
                                              : kTemplate[r][j].usage});
        }
    return pairs;
}

// ---------------------------------------------------------------- Engine

const QVector<Preset> &Engine::presets() {
    using K = Kind;
    auto P = [](const char *id, const char *category, K kind, std::initializer_list<const char *> colors,
                double a = 0, double b = 0) {
        Preset p{id, category, kind, {}, a, b};
        for (const char *c : colors)
            p.palette.append(QColor(c));
        p.usesColor = kind == K::Solid || kind == K::Breathe || kind == K::Custom || kind == K::Heartbeat
                      || kind == K::Spotlight || (kind == K::Ripple && p.palette.isEmpty())
                      || (kind == K::Gradient && p.palette.isEmpty()) || kind == K::Zones;
        p.usesSpeed = !(kind == K::Solid || kind == K::Custom || kind == K::Onboard || kind == K::Zones);
        p.reactive = kind == K::Ripple || kind == K::Heat || kind == K::Fireworks || kind == K::Spotlight;
        return p;
    };
    static const QVector<Preset> list = {
        // Signature: looks borrowed from things you know.
        P("terracotta", "signature", K::Spokes, {"#1f120c", "#8a3f25", "#d97757", "#f6d5c0"}),
        P("reactor", "signature", K::Core, {"#b3121d", "#f2b632", "#bff4ff"}),
        P("saber", "signature", K::Clash, {"#1e6bff", "#ff1a1a", "#ffffff"}),
        P("matrix", "signature", K::Rain, {"#001205", "#00d848", "#c8ffd8"}),
        P("grid", "signature", K::Grid, {"#000814", "#00e5ff", "#ff9a1f"}),
        P("scanner", "signature", K::Scanner, {"#140000", "#ff1500"}),
        P("neoncity", "signature", K::Glitch, {"#fcee0a", "#00f0ff", "#ff003c"}),
        P("synthwave", "signature", K::Gradient, {"#ff2a6d", "#d300c5", "#7a04eb", "#05d9e8"}, 0.25, 0.9),
        P("kinetic", "signature", K::Ripple, {"#0b0014", "#a855f7"}),
        P("gamma", "signature", K::Plasma, {"#0a1f00", "#39ff14", "#6a0dad", "#39ff14"}, 0.5),
        P("swing", "signature", K::Flow, {"#d81e1e", "#0b3fd8", "#d81e1e", "#f2f2f2"}, 0.7, 0.2),
        P("nightwatch", "signature", K::Twinkle, {"#05060a", "#0d1017", "#ffd400"}, 0.1),
        // Editor themes, drifting slowly.
        P("theme", "themes", K::Accent, {}),
        P("tokyonight", "themes", K::Flow, {"#7aa2f7", "#bb9af7", "#7dcfff", "#9ece6a", "#f7768e"}, 0.9, 0.3),
        P("catppuccin", "themes", K::Flow, {"#cba6f7", "#f5c2e7", "#89b4fa", "#94e2d5", "#fab387"}, 0.9, 0.3),
        P("gruvbox", "themes", K::Flow, {"#fb4934", "#fabd2f", "#b8bb26", "#83a598", "#d3869b"}, 0.9, 0.3),
        P("nord", "themes", K::Flow, {"#5e81ac", "#81a1c1", "#88c0d0", "#8fbcbb", "#b48ead"}, 0.9, 0.3),
        P("dracula", "themes", K::Flow, {"#bd93f9", "#ff79c6", "#8be9fd", "#50fa7b", "#ffb86c"}, 0.9, 0.3),
        P("rosepine", "themes", K::Flow, {"#eb6f92", "#f6c177", "#ebbcba", "#31748f", "#c4a7e7"}, 0.9, 0.3),
        P("everforest", "themes", K::Flow, {"#a7c080", "#83c092", "#7fbbb3", "#dbbc7f", "#e67e80"}, 0.9, 0.3),
        P("kanagawa", "themes", K::Flow, {"#7e9cd8", "#957fb8", "#e6c384", "#ff5d62", "#7aa89f"}, 0.9, 0.3),
        P("solarized", "themes", K::Flow, {"#268bd2", "#2aa198", "#859900", "#b58900", "#cb4b16", "#d33682"}, 0.9, 0.3),
        // Nature.
        P("aurora", "nature", K::Plasma, {"#00ff9d", "#00c8ff", "#7a3cff", "#00ff9d"}, 0.65),
        P("sunset", "nature", K::Gradient, {"#ffb347", "#ff5e62", "#c2389d", "#5b2a9d"}, 0.85, 0.1),
        P("ocean", "nature", K::Crest, {"#001a66", "#0090ff", "#d8f6ff"}),
        P("ember", "nature", K::Fire, {"#140000", "#8a0a00", "#ff5a00", "#ffd24a"}, 1.3),
        P("candle", "nature", K::Fire, {"#3a1400", "#c2540a", "#ff8f2a", "#ffc46b"}, 0),
        P("starlight", "nature", K::Twinkle, {"#050a2a", "#131e63", "#ffffff"}, 0.22),
        P("fireflies", "nature", K::Twinkle, {"#021a0b", "#0a3d1a", "#e8ff5a"}, 0.12),
        P("sakura", "nature", K::Rain, {"#2a0f1c", "#ff8fc0", "#ffe3ef"}),
        P("lava", "nature", K::Plasma, {"#1a0000", "#c21500", "#ff7b00", "#ffd000"}, 0.3),
        P("glacier", "nature", K::Plasma, {"#0a2a4a", "#3aa0ff", "#bfefff", "#ffffff"}, 0.4),
        P("nebula", "nature", K::Plasma, {"#14002e", "#5a1fb5", "#d13bd6", "#2ac6ff"}, 0.5),
        P("storm", "nature", K::Storm, {"#05070f", "#16233f", "#dfe9ff"}),
        P("forest", "nature", K::Gradient, {"#0b3d1a", "#2e8b3d", "#9bd35a", "#e3f27a"}, 0.8, 0.4),
        // Reactive: they answer your typing.
        P("ripple", "reactive", K::Ripple, {}),
        P("heat", "reactive", K::Heat, {"#07102e", "#7a0c2e", "#ff5a00", "#ffd24a", "#ffffff"}),
        P("frost", "reactive", K::Heat, {"#000510", "#0a3a8a", "#35b6ff", "#e8fbff"}),
        P("fireworks", "reactive", K::Fireworks, {"#030308", "#ff3b6b", "#ffd60a", "#35b6ff"}),
        P("spotlight", "reactive", K::Spotlight, {}),
        P("raindrops", "reactive", K::Ripple, {"#001233", "#7fe3ff"}),
        // Work: colour that tells you where keys are.
        P("focus", "work", K::Zones, {"#ff3b3b", "#38d6ff", "#e8ecff", "#9d7bff", "#ffb020"}, 0),
        P("gamer", "work", K::Zones, {}, 1),
        P("vim", "work", K::Zones, {"#3ddc84", "#ffb020", "#38d6ff", "#ff79c6", "#ff3b3b"}, 2),
        P("coder", "work", K::Zones, {"#38d6ff", "#c792ea", "#3ddc84", "#ff5370", "#ffcb6b"}, 3),
        // Basics.
        P("solid", "basics", K::Solid, {}),
        P("breathe", "basics", K::Breathe, {}),
        P("heartbeat", "basics", K::Heartbeat, {}),
        P("duotone", "basics", K::Gradient, {}, 0.9, 0.2),
        P("rainbow", "basics", K::Rainbow, {"#ff0040", "#ffd000", "#00e060", "#00a0ff", "#a040ff"}),
        P("cycle", "basics", K::Cycle, {"#ff0040", "#ffd000", "#00e060", "#00a0ff", "#a040ff"}),
        P("custom", "basics", K::Custom, {}),
        P("onboard", "basics", K::Onboard, {"#2a2a30", "#55555f"}),
    };
    return list;
}

const Preset &Engine::preset(const QString &id) {
    for (const Preset &p : presets())
        if (p.id == id)
            return p;
    static const Preset fallback = [] {
        for (const Preset &p : presets())
            if (p.id == "aurora")
                return p;
        return presets().first();
    }();
    return fallback;
}

void Engine::setLayout(const KeyboardLayout &layout) {
    m_layout = layout;
    m_cx = layout.width() / 2;
    m_cy = layout.height() / 2;
    m_nodes.clear();
    QVector<bool> seen(layout.leds(), false);
    const double w = qMax(1.0, layout.width()), h = qMax(1.0, layout.height());
    if (layout.isPlaced()) {
        // The driver said where everything is: nothing to guess.
        for (const Key &k : layout.keys()) {
            const double cx = k.x + k.w / 2, cy = k.y + 0.5;
            m_nodes.append({k.led, k.usage, cx, cy, cx / w, h > 1 ? (cy - 0.5) / (h - 1) : 0.5});
        }
        for (const Key &k : layout.lights()) {
            const double cx = qBound(0.0, k.x + k.w / 2, w), cy = qBound(0.5, k.y + 0.5, h - 0.5);
            m_nodes.append({k.led, -1, cx, cy, cx / w, h > 1 ? (cy - 0.5) / (h - 1) : 0.5});
        }
        return;
    }
    for (const Key &k : layout.keys()) {
        const double cx = k.x + k.w / 2, cy = k.y + 0.5;
        m_nodes.append({k.led, k.usage, cx, cy, cx / w, h > 1 ? (cy - 0.5) / (h - 1) : 0.5});
        seen[k.led] = true;
    }
    // Before teaching, the guess may be wrong, so the rest of the matrix is lit
    // too, placed by matrix position. After it, LEDs that aren't there stay off.
    for (int i = 0; i < Leds; ++i)
            if (!seen[i] && (!layout.taught() || layout.extras().contains(i))) {
                const double cx = qMin(w, i / Rows + 0.5), cy = qMin(h, i % Rows + 0.5);
                m_nodes.append({i, -1, cx, cy, cx / w, h > 1 ? qBound(0.0, (cy - 0.5) / (h - 1), 1.0) : 0.5});
            }
}

void Engine::press(int usage, qint64 ms) {
    for (const Node &n : m_nodes)
        if (n.usage == usage && usage > 0) {
            m_hits.append({n.x, n.y, n.led, ms});
            break;
        }
    while (m_hits.size() > 48)
        m_hits.removeFirst();
}

namespace {
QColor mix(const QColor &a, const QColor &b, double t) {
    t = qBound(0.0, t, 1.0);
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}
QColor scale(const QColor &c, double k) {
    k = qBound(0.0, k, 1.0);
    return QColor::fromRgbF(c.redF() * k, c.greenF() * k, c.blueF() * k);
}
QColor add(const QColor &a, const QColor &b) {
    return QColor(qMin(255, a.red() + b.red()), qMin(255, a.green() + b.green()), qMin(255, a.blue() + b.blue()));
}
double fract(double v) { return v - std::floor(v); }
// Repeatable pseudo-random 0..1, so the window and the streamer agree.
double hash(qint64 a, qint64 b) {
    quint64 h = quint64(a) * 0x9E3779B97F4A7C15ULL ^ (quint64(b) + 0xD1B54A32D192ED03ULL) * 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 31; h *= 0x94D049BB133111EBULL; h ^= h >> 29;
    return double(h % 100000) / 100000.0;
}
bool isLetter(int u) { return u >= 0x04 && u <= 0x1D; }
bool isNumber(int u) { return u >= 0x1E && u <= 0x27; }
bool isArrow(int u) { return u >= 0x4F && u <= 0x52; }
bool isModifier(int u) { return u == 0 || u >= 0xE0 || u == 0x39 || u == 0x2B; }
}

namespace {
QColor rampOf(const QVector<QColor> &stops, double t) {
    if (stops.size() < 2)
        return stops.value(0, QColor(Qt::black));
    t = qBound(0.0, t, 1.0) * (stops.size() - 1);
    const int i = qMin(int(stops.size()) - 2, int(t));
    return mix(stops[i], stops[i + 1], t - i);
}
// Same, but the last colour runs back into the first, so it can scroll forever.
QColor loopOf(const QVector<QColor> &stops, double t) {
    const double at = fract(t) * stops.size();
    const int i = int(at) % stops.size();
    return mix(stops[i], stops[(i + 1) % stops.size()], at - int(at));
}
double triangle(double v) { return 1 - std::abs(2 * fract(v / 2) - 1); }
}

QColor Engine::shade(const Settings &s, const Node &n, double t, qint64 ms) const {
    const Preset &p = preset(s.preset);
    const QVector<QColor> &pal = p.palette;
    const double u = n.u, v = n.v;
    const double dx = n.x - m_cx, dy = n.y - m_cy, far = std::hypot(dx, dy);
    const int k = n.usage;

    switch (p.kind) {
    case Kind::Solid:
        return s.color;
    case Kind::Breathe:
        return scale(s.color, 0.06 + 0.94 * (0.5 - 0.5 * std::cos(t * 1.6)));
    case Kind::Custom:
        return s.custom.value(n.led, QColor(Qt::white));
    case Kind::Onboard:
        return QColor(Qt::black);
    case Kind::Rainbow:
        return QColor::fromHsvF(fract(u * 0.8 + v * 0.25 - t * 0.25), 1, 1);
    case Kind::Cycle:
        return QColor::fromHsvF(fract(t * 0.08), 1, 1);
    case Kind::Accent: {
        QColor other = m_accent.toHsv();
        other.setHsvF(fract(qMax(0.0f, other.hsvHueF()) + 0.12), qMin(1.0, other.hsvSaturationF() + 0.15), 1.0);
        return mix(m_accent, other, 0.5 + 0.5 * std::sin(u * 3.2 + v * 1.4 - t * 0.8));
    }
    case Kind::Flow:
        return loopOf(pal, u * p.a + v * p.b - t * 0.1);
    case Kind::Gradient: {
        // Back and forth along the palette, so there is never a hard seam.
        const double at = triangle(u * p.a + v * p.b + 0.1 * std::sin(t * 0.5 + v * 2.2) + t * 0.03);
        if (!pal.isEmpty())
            return rampOf(pal, at);
        QColor twin = s.color.toHsv();
        twin.setHsvF(fract(qMax(0.0f, twin.hsvHueF()) + 0.5), twin.hsvSaturationF(), twin.valueF());
        return mix(s.color, twin, at);
    }
    case Kind::Plasma: {
        const double band = 0.5 + 0.25 * std::sin(u * 3.1 + t * 0.6) + 0.25 * std::sin(u * 5.3 - t * 0.4 + v * 2.0);
        const double glow = 1 - p.a + p.a * (0.5 + 0.5 * std::sin(v * 2.6 + u * 1.7 + t * 0.9));
        return scale(rampOf(pal, band), glow);
    }
    case Kind::Fire: {
        // Hotter toward the bottom row (when a > 0), with a flicker that travels sideways.
        const double flicker = 0.55 + 0.45 * std::sin(u * 21 + t * 5.1) * std::sin(u * 7.3 - t * 3.3 + v * 3);
        const double height = p.a > 0 ? std::pow(v, p.a) : 0.75;
        return rampOf(pal, height * (0.45 + 0.55 * flicker) + 0.08);
    }
    case Kind::Crest: {
        const double wave = std::sin(u * 6.5 - t * 1.4 + v * 1.3);
        const QColor water = mix(pal[0], pal[1], 0.5 + 0.5 * wave);
        return wave > 0.9 ? mix(water, pal[2], (wave - 0.9) * 10) : water;
    }
    case Kind::Rain: {
        // One drop per column, each with its own pace and starting point.
        const int column = int(n.x);
        const double pace = 0.35 + 0.5 * hash(column, 7);
        const double head = fract(t * pace * 0.45 + hash(column, 11)) * (m_layout.height() + 5) - 1;
        const double behind = head - n.y;
        if (behind < 0 || behind > 4)
            return pal[0];
        return behind < 0.6 ? pal[2] : mix(pal[1], pal[0], behind / 4);
    }
    case Kind::Twinkle: {
        const double shifted = t / 1.9 + hash(n.led, 3);
        const QColor sky = mix(pal[0], pal[1], v);
        if (hash(n.led, qint64(std::floor(shifted))) > p.a)
            return sky;
        return mix(sky, pal[2], std::sin(M_PI * fract(shifted)));
    }
    case Kind::Core: {
        // Armour plating in two colours, and a reactor pulsing at the heart.
        const QColor plate = std::sin(u * 9.0 + v * 3.0) > 0.55 ? pal[1] : pal[0];
        const double pulse = 0.75 + 0.25 * std::sin(t * 3.0);
        const double core = std::exp(-far * far / (2.6 * pulse));
        const double ring = 0.45 * std::exp(-std::pow(far - 3.1, 2) / 0.35) * pulse;
        return mix(scale(plate, 0.85), pal[2], qMin(1.0, core + ring));
    }
    case Kind::Spokes: {
        // A many-pointed spark, slowly turning and breathing out from the middle.
        const double angle = std::atan2(dy * 1.7, dx);
        const double ray = std::pow(0.5 + 0.5 * std::cos(angle * 6 + t * 0.35), 2.2);
        const double reach = 6.0 + 2.5 * std::sin(t * 0.8);
        const double near = qMax(0.0, 1 - far / reach);
        return rampOf(pal, 0.22 + 0.78 * qMin(1.0, ray * near * 1.3 + near * near * 0.55)
                               + 0.05 * std::sin(t * 1.3 + u * 5));
    }
    case Kind::Clash: {
        // Two blades meeting; the meeting point shoves back and forth and sparks.
        const double meet = 0.5 + 0.18 * std::sin(t * 1.3) + 0.04 * std::sin(t * 7.1);
        const double d = u - meet;
        const QColor blade = scale(d < 0 ? pal[0] : pal[1], 0.3 + 0.7 * std::exp(-std::abs(d) * 3.0));
        const double spark = std::exp(-d * d / 0.0025) * (0.7 + 0.3 * std::sin(t * 23 + v * 9));
        return mix(blade, pal[2], spark);
    }
    case Kind::Scanner: {
        const double at = triangle(t * 0.45);
        const bool rightward = fract(t * 0.45 / 2) < 0.5;
        const double d = u - at, behind = rightward ? -d : d;
        double glow = std::exp(-d * d / 0.004);
        if (behind > 0)
            glow = qMax(glow, 0.7 * std::exp(-behind * 7));
        return mix(pal[0], pal[1], glow);
    }
    case Kind::Grid: {
        // A light cycle per row, alternating direction and colour, leaving a trail.
        const int row = int(n.y);
        const double head = fract(t * 0.22 * (1 + 0.25 * row) + hash(row, 5));
        double behind = row % 2 ? fract(u - head) : fract(head - u);
        const QColor trail = pal[1 + row % 2];
        if (behind < 0.04)
            return QColor(Qt::white);
        return mix(scale(pal[1], 0.07), trail, behind < 0.4 ? 1 - behind / 0.4 : 0);
    }
    case Kind::Glitch: {
        const qint64 slot = qint64(std::floor(t * 5));
        const int row = int(n.y);
        if (hash(row * 31 + 7, slot) < 0.14)
            return hash(n.led, slot) < 0.4 ? QColor(Qt::white) : pal[(row + slot) % pal.size()];
        return scale(loopOf(pal, u * 0.9 + v * 0.3 - t * 0.15), 0.85);
    }
    case Kind::Storm: {
        const QColor cloud = mix(pal[0], pal[1], 0.5 + 0.5 * std::sin(u * 4 + v * 2 + t * 0.5));
        const double beat = t * 1.1;
        const qint64 slot = qint64(std::floor(beat));
        if (hash(slot, 1) > 0.3)
            return cloud;
        const double f = fract(beat), where = hash(slot, 2);
        const double flash = std::exp(-f * 9) + (f > 0.18 ? 0.6 * std::exp(-(f - 0.18) * 14) : 0);
        return mix(cloud, pal[2], qMin(1.0, flash * (0.3 + 0.7 * std::exp(-std::pow(u - where, 2) / 0.02))));
    }
    case Kind::Heartbeat: {
        const double f = fract(t * 0.9);
        const double beat = std::exp(-f * 9) + 0.7 * std::exp(-std::pow((f - 0.22) * 12, 2));
        return scale(s.color, 0.06 + qMin(1.0, beat) * (1 - 0.04 * far));
    }
    case Kind::Ripple: {
        const QColor ring = pal.isEmpty() ? s.color : pal[1];
        QColor out = pal.isEmpty() ? scale(s.color, 0.07) : pal[0];
        for (const Hit &h : m_hits) {
            const double age = (ms - h.ms) / 1000.0;
            if (age < 0 || age > 1.6)
                continue;
            const double d = std::hypot(n.x - h.x, n.y - h.y) - age * 11.0;
            out = add(out, scale(ring, std::exp(-d * d / 0.9) * (1 - age / 1.6)));
        }
        return out;
    }
    case Kind::Fireworks: {
        QColor out = pal[0];
        for (const Hit &h : m_hits) {
            const double age = (ms - h.ms) / 1000.0;
            if (age < 0 || age > 1.4)
                continue;
            const double d = std::hypot(n.x - h.x, n.y - h.y) - age * 9.0;
            const QColor burst = QColor::fromHsvF(hash(h.ms, h.led), 0.9, 1);
            out = add(out, scale(burst, std::exp(-d * d / 0.7) * (1 - age / 1.4)));
        }
        return out;
    }
    case Kind::Spotlight: {
        double lit = 0;
        for (const Hit &h : m_hits)
            if (h.led == n.led && ms >= h.ms)
                lit = qMax(lit, std::exp(-(ms - h.ms) / 1000.0 / 0.5));
        return scale(s.color, 0.03 + 0.97 * lit);
    }
    case Kind::Heat: {
        double heat = 0;
        for (const Hit &h : m_hits) {
            const double age = (ms - h.ms) / 1000.0;
            if (age < 0 || age > 4)
                continue;
            const double d = std::hypot(n.x - h.x, n.y - h.y);
            heat = qMax(heat, std::exp(-age / 1.1) * (h.led == n.led ? 1.0 : d < 1.6 ? 0.35 : 0.0));
        }
        return rampOf(pal, heat);
    }
    case Kind::Zones: {
        const QColor soft("#e8ecff");
        if (p.a == 0) { // by kind of key
            if (k == 0x29) return pal[0];
            if (isArrow(k)) return pal[4];
            if (isNumber(k)) return pal[1];
            if (k == 0x28 || k == 0x2C || k == 0x2A) return pal[3];
            if (isModifier(k)) return s.color;
            if (isLetter(k)) return pal[2];
            if (k >= 0x49 && k <= 0x4E) return QColor("#3ddc84");
            return QColor("#8088a8");
        }
        if (p.a == 1) { // movement keys
            if (k == 0x1A || k == 0x04 || k == 0x16 || k == 0x07 || isArrow(k)) return s.color;
            if (k == 0x29) return QColor("#ff3b3b");
            if (k == 0x2C || k == 0xE1 || k == 0xE0 || k == 0x14 || k == 0x08 || k == 0x15 || k == 0x09
                || (k >= 0x1E && k <= 0x22))
                return scale(s.color, 0.4);
            return scale(s.color, 0.05);
        }
        if (p.a == 2) { // vim: motions, insert keys, word jumps, operators
            if (k == 0x0B || k == 0x0D || k == 0x0E || k == 0x0F) return pal[0];
            if (k == 0x0C || k == 0x04 || k == 0x12) return pal[1];
            if (k == 0x1A || k == 0x05 || k == 0x08) return pal[2];
            if (k == 0x07 || k == 0x1C || k == 0x13 || k == 0x18) return pal[3];
            if (k == 0x29) return pal[4];
            if (k == 0x33 || k == 0x38) return s.color;
            return scale(soft, 0.1);
        }
        // code: punctuation stands out, the rest steps back
        if (k == 0x29) return QColor("#ff3b3b");
        if ((k >= 0x2D && k <= 0x38 && k != 0x32) ) return pal[0];
        if (isNumber(k)) return pal[1];
        if (k == 0x28) return pal[2];
        if (k == 0x2A || k == 0x4C) return pal[3];
        if (isArrow(k)) return pal[4];
        if (isModifier(k)) return s.color;
        if (k == 0x2C) return QColor("#82aaff");
        return scale(soft, 0.4);
    }
    }
    return QColor(Qt::black);
}

QVector<QColor> Engine::render(const Settings &s, qint64 ms) {
    QVector<QColor> out(m_layout.leds(), QColor(Qt::black));
    if (s.identify >= 0) {
        // Everything glows dimly so the board is clearly alive, and the one
        // light being asked about stands out in white.
        out.fill(QColor(0, 14, 40));
        if (s.identify < out.size())
            out[s.identify] = QColor(Qt::white);
        return out;
    }
    if (!s.power || preset(s.preset).kind == Kind::Onboard)
        return out;
    // Speed 0 is a quarter of the natural pace, speed 100 is double.
    const double t = ms / 1000.0 * (0.25 + s.speed / 100.0 * 1.75);
    // Only a slice of the clock is used so the sines keep their precision.
    const double wrapped = std::fmod(t, 36000.0);
    const double level = s.brightness / 100.0;
    for (const Node &n : m_nodes)
        out[n.led] = scale(shade(s, n, wrapped, ms), level);
    return out;
}

// Layout of the report, from OpenRGB's SinowealthKeyboard10cController:
// report id 6, command 8, then the payload length (0x017A = 126 * 3) and RGB
// triplets from byte 8.
QByteArray Lighting::frame(const QVector<QColor> &colors) {
    QByteArray buf(FrameSize, '\0');
    buf[0] = 0x06;
    buf[1] = 0x08;
    buf[4] = 0x01;
    buf[6] = 0x7A;
    buf[7] = 0x01;
    for (int i = 0; i < Leds && i < colors.size(); ++i) {
        buf[8 + i * 3] = char(colors[i].red());
        buf[9 + i * 3] = char(colors[i].green());
        buf[10 + i * 3] = char(colors[i].blue());
    }
    return buf;
}

QColor Lighting::balanced(const QColor &c, int whiteRed, int whiteBlue) {
    if (whiteRed >= 100 && whiteBlue >= 100)
        return c;
    const double r = c.red() * whiteRed / 100.0;
    const double g = c.green();
    const double b = c.blue() * whiteBlue / 100.0;
    const double before = qMax(c.red(), qMax(c.green(), c.blue()));
    const double after = qMax(r, qMax(g, b));
    if (after <= 0)
        return QColor(Qt::black);
    const double k = before / after;
    return QColor(qBound(0, qRound(r * k), 255), qBound(0, qRound(g * k), 255), qBound(0, qRound(b * k), 255));
}

QVector<QColor> Lighting::balanced(QVector<QColor> colors, int whiteRed, int whiteBlue) {
    for (QColor &c : colors)
        c = balanced(c, whiteRed, whiteBlue);
    return colors;
}
