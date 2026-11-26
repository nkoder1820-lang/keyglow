#pragma once

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QSet>
#include <functional>
#include <QString>
#include <QStringList>
#include <QVector>

// What the user picked. Shared by the window, which edits and saves it, and
// the background streamer, which turns it into frames for the keyboard.
struct Settings {
    bool power = true;
    bool onboarded = false;
    QStringList favorites;
    QString preset = "aurora";
    QColor color = QColor("#7d5cff"); // the user's colour, for presets that take one
    int brightness = 100;             // 0-100
    int speed = 50;                   // 0-100
    QVector<QColor> custom;           // one colour per LED, for the painted preset
    int identify = -1;                // while teaching: the single LED to light
    // How much red and blue the keyboard needs next to full green for white
    // to look white, in percent. 0 means "not set": the connected keyboard's
    // own default is used (see Device::defaultWhite).
    int whiteRed = 0, whiteBlue = 0;

    Settings();
    static QString defaultPath();
    static Settings load(const QString &path);
    bool save(const QString &path) const;
    bool operator==(const Settings &other) const;
};

// How a scene paints. Most scenes are one of these with its own palette, so
// adding a scene is a line in the table, not new drawing code.
enum class Kind {
    Solid, Breathe, Custom, Onboard, Accent, Rainbow, Cycle, Flow, Gradient, Plasma, Fire, Crest, Rain,
    Twinkle, Core, Spokes, Clash, Scanner, Grid, Glitch, Storm, Heartbeat, Ripple, Heat, Fireworks,
    Spotlight, Zones
};

struct Preset {
    QString id, category;
    Kind kind;
    QVector<QColor> palette;
    double a = 0, b = 0;      // meaning depends on the kind
    bool usesColor = false, usesSpeed = true, reactive = false;
};

struct Key {
    int led = -1;    // index in the controller's matrix
    int usage = 0;   // HID usage it sends, 0 for Fn and for lights that aren't keys
    double x = 0, y = 0, w = 1; // top-left corner and width, in key units
    QString label;
};

// Which LED sits under which key. The controller only knows a 21 x 6 matrix
// (index = column * 6 + row), so the real arrangement is learned once from
// the user ("teach") and kept in layout.json. Until then a guess is used and
// every LED of the matrix is lit, so nothing stays dark.
class KeyboardLayout {
public:
    KeyboardLayout();
    static QString defaultPath();
    static KeyboardLayout load(const QString &path);
    static KeyboardLayout fromPairs(const QVector<QPair<int, int>> &ledUsage);
    // A layout that is already known in full, as other drivers report it:
    // every key with its place, lights that aren't keys with theirs, and how
    // many LEDs the keyboard has in all.
    static KeyboardLayout placed(const QVector<Key> &keys, const QVector<Key> &lights, int leds);
    bool save(const QString &path) const;

    const QVector<Key> &keys() const { return m_keys; }
    // Lights that aren't under a key (a light bar, say): lit, but not drawn.
    const QVector<int> &extras() const { return m_extras; }
    // The same, with where each sits; only filled for placed layouts.
    const QVector<Key> &lights() const { return m_lights; }
    bool isPlaced() const { return m_placed; }
    int leds() const { return m_leds; }
    bool taught() const { return m_taught; }
    double width() const { return m_width; }
    double height() const { return m_height; }
    int ledForUsage(int usage) const;
    static QString labelFor(int usage);

private:
    void arrange(QVector<QPair<int, int>> ledUsage);

    QVector<Key> m_keys;
    QVector<int> m_extras;
    QVector<Key> m_lights;
    bool m_taught = false, m_placed = false;
    int m_leds = 126;
    double m_width = 16, m_height = 5;
};

// Works out the whole key-to-light map from about a dozen answers. Boards on
// this controller are wired as a grid: a key's matrix row is its physical
// row (give or take an offset) and columns run left to right, with gaps
// around the wide keys that differ from board to board. So every plausible
// wiring of the Artemis's 68 keys is listed up front, and each answer ("this
// light is under that key", or "that light doesn't exist") strikes out the
// ones it contradicts. The next light asked about is the one that splits the
// survivors best. If nothing survives, the board is wired some other way and
// the caller falls back to checking every light.
class QuickSync {
public:
    QuickSync();

    int nextLed() const;                // the light to ask about, -1 when done or failed
    void pressed(int led, int usage);   // that light is under the key sending `usage`
    void nothing(int led);              // that light never came on
    void silentKey(int led);            // lit, but the key sends nothing: Fn
    bool failed() const;
    bool done() const { return !failed() && nextLed() < 0; }
    int answers() const { return m_answers; }
    QVector<QPair<int, int>> result() const;

private:
    using Row = QVector<int>;           // the matrix column of each key in a row
    void keep(int offset, int row, const std::function<bool(const Row &)> &fits);
    void place(int led, int row, int slot);
    int offset() const;

    QVector<QVector<Row>> m_rows[2];    // per matrix-row offset (0 or 1), per row: surviving wirings
    bool m_alive[2] = {true, true};
    QHash<int, int> m_extraUsage;       // light -> usage, for the right-hand column
    QSet<int> m_answered;
    int m_answers = 0;
};

// Turns settings, the layout and recent key presses into colours.
class Engine {
public:
    static const QVector<Preset> &presets();
    static const Preset &preset(const QString &id);

    void setLayout(const KeyboardLayout &layout);
    void setAccent(const QColor &accent) { m_accent = accent; }
    void press(int usage, qint64 ms);
    // One colour per matrix LED at wall-clock time `ms`. The wall clock keeps
    // the on-screen keyboard in step with the real one.
    QVector<QColor> render(const Settings &settings, qint64 ms);

private:
    struct Node { int led; int usage; double x, y, u, v; };
    struct Hit { double x, y; int led; qint64 ms; };

    QColor shade(const Settings &s, const Node &n, double t, qint64 ms) const;

    QVector<Node> m_nodes;
    QVector<Hit> m_hits;
    double m_cx = 8, m_cy = 2.5;
    KeyboardLayout m_layout;
    QColor m_accent = QColor("#FFD60A");
};

namespace Lighting {
constexpr int Columns = 21;
constexpr int Rows = 6;
constexpr int Leds = Columns * Rows;
constexpr int MaxLeds = 1024; // the most any keyboard is allowed to report
constexpr int FrameSize = 520;
// The SinoWealth feature report that shows those colours once (direct mode).
QByteArray frame(const QVector<QColor> &colors);
// The colour to send so the keyboard shows `c` as the screen does: red and
// blue are cut to the keyboard's white balance, then the result is scaled
// back up so its strongest channel is as bright as before. Pure red stays
// full red; white becomes mostly green.
QColor balanced(const QColor &c, int whiteRed, int whiteBlue);
QVector<QColor> balanced(QVector<QColor> colors, int whiteRed, int whiteBlue);
}
