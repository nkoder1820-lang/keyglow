#include "backend.h"

#include <QJsonArray>

#include "device.h"
#include "theme.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

using Lighting::Leds;
using Lighting::Rows;

Backend::Backend(const QString &configDirectory, bool live, QObject *parent) : QObject(parent) {
    const QString directory = configDirectory.isEmpty()
        ? QFileInfo(Settings::defaultPath()).absolutePath() : configDirectory;
    m_configPath = directory + "/config.json";
    m_layoutPath = directory + "/layout.json";
    m_autostartPath = configDirectory.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/autostart/keyglow-daemon.desktop"
        : directory + "/autostart.desktop";

    m_settings = Settings::load(m_configPath);
    m_settings.identify = -1; // a teach walk that was cut short must not leave one LED on
    m_layout = KeyboardLayout::load(m_layoutPath);
    m_engine.setLayout(m_layout);

    // Dragging a slider changes the value many times a second; one write
    // shortly after the last change is enough for the keyboard to follow.
    m_save.setSingleShot(true);
    m_save.setInterval(40);
    connect(&m_save, &QTimer::timeout, this, [this] { m_settings.save(m_configPath); });

    m_noticeTimer.setSingleShot(true);
    m_noticeTimer.setInterval(9000);
    connect(&m_noticeTimer, &QTimer::timeout, this, [this] { setNotice({}); });
    connect(&m_renderTimer, &QTimer::timeout, this, &Backend::render);
    connect(&m_deviceTimer, &QTimer::timeout, this, &Backend::refreshDevice);
    connect(&m_input, &KeyInput::pressed, this, [this](int usage) {
        if (teaching())
            teachPress(usage);
        else
            m_engine.press(usage, QDateTime::currentMSecsSinceEpoch());
    });
    connect(&m_input, &KeyInput::changed, this, [this] { ++m_downTick; emit downChanged(); });
    render();
    refreshDevice();

    if (live) {
        QDir().mkpath(directory);
        m_watcher.addPath(directory);
        connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Backend::reloadIfChangedOutside);
        connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &Backend::reloadIfChangedOutside);
        m_renderTimer.start(40);
        m_deviceTimer.start(2000);
        m_input.setActive(true);
        m_settings.save(m_configPath);
        // The streamer holds a lock, so a second copy exits straight away. It is
        // detached from our output too, or a terminal that launched the window
        // would wait on it forever.
        QProcess streamer;
        streamer.setProgram(QCoreApplication::applicationFilePath());
        streamer.setArguments({"daemon"});
        streamer.setStandardInputFile(QProcess::nullDevice());
        streamer.setStandardOutputFile(QProcess::nullDevice());
        streamer.setStandardErrorFile(QProcess::nullDevice());
        streamer.startDetached();
    }
}

Backend::~Backend() {
    // Never leave the keyboard stuck on a single teaching light.
    if (m_settings.identify >= 0 || m_save.isActive()) {
        m_settings.identify = -1;
        m_settings.save(m_configPath);
    }
}

QVariantList Backend::presets() const {
    struct Text { const char *id; QString name, blurb; };
    const QVector<Text> texts = {
        {"terracotta", tr("Terracotta"), tr("A warm clay-orange spark, slowly turning as if deep in thought")},
        {"reactor", tr("Reactor"), tr("Deep red and gold with a bright core pulsing at the heart")},
        {"saber", tr("Saber Clash"), tr("A blue blade and a red blade, locked and sparking")},
        {"matrix", tr("Digital Rain"), tr("Green code falling down the columns")},
        {"grid", tr("The Grid"), tr("Light trails racing each row, cyan against orange")},
        {"scanner", tr("Scanner"), tr("One red eye sweeping side to side")},
        {"neoncity", tr("Neon City"), tr("Neon yellow, cyan and magenta, with glitches")},
        {"synthwave", tr("Synthwave"), tr("An '80s sunset in hot pink and electric blue")},
        {"kinetic", tr("Kinetic"), tr("Purple energy rippling out from every key you hit")},
        {"gamma", tr("Gamma"), tr("Radioactive green surging through purple")},
        {"swing", tr("Swing"), tr("Red and blue, swinging across the board")},
        {"nightwatch", tr("Night Watch"), tr("A dark city, a few yellow signals in the night")},
        {"theme", tr("Omarchy"), tr("Follows your desktop theme's accent, live")},
        {"tokyonight", tr("Tokyo Night"), tr("Blue, violet and a little neon green")},
        {"catppuccin", tr("Catppuccin"), tr("Soft mauve, pink and peach pastels")},
        {"gruvbox", tr("Gruvbox"), tr("Retro red, yellow and olive")},
        {"nord", tr("Nord"), tr("Cool arctic blues")},
        {"dracula", tr("Dracula"), tr("Purple and pink with a bite of green")},
        {"rosepine", tr("Rosé Pine"), tr("Dusty rose, gold and pine")},
        {"everforest", tr("Everforest"), tr("Mossy greens and warm bark")},
        {"kanagawa", tr("Kanagawa"), tr("Ink blue and wave-crest gold")},
        {"solarized", tr("Solarized"), tr("The classic sixteen, in motion")},
        {"aurora", tr("Aurora"), tr("Northern lights drifting across the keys")},
        {"sunset", tr("Sunset"), tr("Warm gold fading into violet")},
        {"ocean", tr("Ocean"), tr("Deep blue swells with white crests")},
        {"ember", tr("Ember"), tr("A fire glowing up from the bottom row")},
        {"candle", tr("Candlelight"), tr("A soft, warm flicker for late nights")},
        {"starlight", tr("Starlight"), tr("A night sky that twinkles")},
        {"fireflies", tr("Fireflies"), tr("Tiny lights blinking in a dark meadow")},
        {"sakura", tr("Sakura"), tr("Pink petals drifting down")},
        {"lava", tr("Lava"), tr("Molten rock, slowly churning")},
        {"glacier", tr("Glacier"), tr("Ice blue with glints of white")},
        {"nebula", tr("Nebula"), tr("Violet and magenta gas clouds")},
        {"storm", tr("Thunderstorm"), tr("Dark clouds, and lightning now and then")},
        {"forest", tr("Forest"), tr("Sunlight moving through leaves")},
        {"ripple", tr("Ripple"), tr("Every key press sends out a ring in your colour")},
        {"heat", tr("Typing Heat"), tr("Keys glow hot where you type, then cool")},
        {"frost", tr("Frostbite"), tr("Keys freeze over where you type")},
        {"fireworks", tr("Fireworks"), tr("Each key press bursts in a new colour")},
        {"spotlight", tr("Spotlight"), tr("Dark until you type; each key flares and fades")},
        {"raindrops", tr("Raindrops"), tr("Typing lands like rain on a dark pond")},
        {"focus", tr("Focus"), tr("Letters, numbers and modifiers colour-coded")},
        {"gamer", tr("WASD"), tr("Movement keys lit, the rest dimmed")},
        {"vim", tr("Vim"), tr("Motions, inserts, word jumps and operators picked out")},
        {"coder", tr("Code"), tr("Brackets and punctuation stand out")},
        {"solid", tr("Solid"), tr("One colour, every key")},
        {"breathe", tr("Breathe"), tr("One colour, slowly pulsing")},
        {"heartbeat", tr("Heartbeat"), tr("Your colour, beating from the centre")},
        {"duotone", tr("Duotone"), tr("Your colour blending into its opposite")},
        {"rainbow", tr("Rainbow"), tr("The classic, sweeping sideways")},
        {"cycle", tr("Colour Cycle"), tr("The whole board drifting through every hue")},
        {"custom", tr("Painted"), tr("Click keys to colour them yourself")},
        {"onboard", tr("Keyboard's Own"), tr("Hand control back to the keyboard")},
    };
    QVariantList out;
    for (const Text &t : texts) {
        const Preset &p = Engine::preset(t.id);
        out.append(QVariantMap{{"id", p.id}, {"name", t.name}, {"blurb", t.blurb}, {"category", p.category},
                               {"reactive", p.reactive}, {"usesColor", p.usesColor},
                               {"favorite", m_settings.favorites.contains(p.id)}});
    }
    return out;
}

QVariantList Backend::categories() const {
    return {
        QVariantMap{{"id", "all"}, {"name", tr("All")}},
        QVariantMap{{"id", "favorites"}, {"name", tr("★ Favourites")}},
        QVariantMap{{"id", "signature"}, {"name", tr("Signature")}},
        QVariantMap{{"id", "themes"}, {"name", tr("Themes")}},
        QVariantMap{{"id", "nature"}, {"name", tr("Nature")}},
        QVariantMap{{"id", "reactive"}, {"name", tr("Reactive")}},
        QVariantMap{{"id", "work"}, {"name", tr("Work")}},
        QVariantMap{{"id", "basics"}, {"name", tr("Basics")}},
    };
}

QVariantMap Backend::current() const {
    for (const QVariant &v : presets())
        if (v.toMap().value("id") == m_settings.preset)
            return v.toMap();
    return {};
}

void Backend::toggleFavorite(const QString &preset) {
    if (Engine::preset(preset).id != preset)
        return;
    if (!m_settings.favorites.removeOne(preset))
        m_settings.favorites.append(preset);
    m_save.start();
    emit presetsChanged();
    emit changed();
}

void Backend::finishOnboarding() {
    if (m_settings.onboarded)
        return;
    m_settings.onboarded = true;
    touch();
}

QVariantList Backend::keys() const {
    QVariantList out;
    for (const Key &k : m_layout.keys())
        out.append(QVariantMap{{"led", k.led}, {"usage", k.usage}, {"x", k.x}, {"y", k.y}, {"w", k.w},
                               {"label", k.label}});
    return out;
}

void Backend::touch() {
    m_save.start();
    emit changed();
    render();
}

void Backend::setPower(bool power) {
    if (power == m_settings.power)
        return;
    m_settings.power = power;
    touch();
}

void Backend::setPreset(const QString &preset) {
    if (Engine::preset(preset).id != preset)
        return;
    if (preset == m_settings.preset && m_settings.power)
        return;
    m_settings.preset = preset;
    m_settings.power = true; // picking a look is also "turn it on"
    touch();
}

void Backend::step(int by) {
    const auto &list = Engine::presets();
    int index = 0;
    for (int i = 0; i < list.size(); ++i)
        if (list[i].id == m_settings.preset)
            index = i;
    setPreset(list[(index + by % list.size() + list.size()) % list.size()].id);
}

void Backend::setColor(const QString &color) {
    const QColor c(color);
    if (!c.isValid() || c == m_settings.color)
        return;
    m_settings.color = c;
    touch();
}

void Backend::setBrightness(int brightness) {
    brightness = qBound(0, brightness, 100);
    if (brightness == m_settings.brightness)
        return;
    m_settings.brightness = brightness;
    touch();
}

void Backend::setWhiteRed(int percent) {
    percent = qBound(5, percent, 100);
    if (percent == m_settings.whiteRed)
        return;
    m_settings.whiteRed = percent;
    touch();
}

void Backend::setWhiteBlue(int percent) {
    percent = qBound(5, percent, 100);
    if (percent == m_settings.whiteBlue)
        return;
    m_settings.whiteBlue = percent;
    touch();
}

void Backend::reloadIfChangedOutside() {
    // Saving replaces the file, which drops it from the watch; re-arm each time.
    if (QFileInfo::exists(m_configPath) && !m_watcher.files().contains(m_configPath))
        m_watcher.addPath(m_configPath);
    // Our own unsaved change wins, and a teach walk is not to be interrupted.
    if (m_save.isActive() || teaching())
        return;
    Settings loaded = Settings::load(m_configPath);
    loaded.identify = -1;
    Settings mine = m_settings;
    mine.identify = -1;
    if (loaded == mine)
        return;
    m_settings = loaded;
    render();
    emit presetsChanged();
    emit changed();
}

void Backend::setSpeed(int speed) {
    speed = qBound(0, speed, 100);
    if (speed == m_settings.speed)
        return;
    m_settings.speed = speed;
    touch();
}

void Backend::paint(int led) {
    if (led < 0 || led >= Leds)
        return;
    if (m_settings.preset != "custom")
        setPreset("custom");
    if (m_settings.custom[led] == m_settings.color)
        return;
    m_settings.custom[led] = m_settings.color;
    touch();
}

void Backend::fillCustom() {
    m_settings.custom.fill(m_settings.color);
    m_settings.preset = "custom";
    m_settings.power = true;
    touch();
}

void Backend::paintFromCurrent() {
    // Taken at full brightness, so the brightness slider isn't applied twice.
    Settings full = m_settings;
    full.brightness = 100;
    full.power = true;
    m_settings.custom = m_engine.render(full, QDateTime::currentMSecsSinceEpoch());
    m_settings.preset = "custom";
    m_settings.power = true;
    touch();
}

void Backend::render() {
    m_engine.setAccent(QColor(Theme::readAccent(
        QDir::homePath() + "/.local/state/omarchy/current/theme/colors.toml")));
    const QVector<QColor> next = m_engine.render(m_settings, QDateTime::currentMSecsSinceEpoch());
    if (next != m_colors) {
        m_colors = next;
        // The average colour of the keys, for the glow behind the keyboard.
        double r = 0, g = 0, b = 0;
        for (const Key &k : m_layout.keys()) {
            r += m_colors[k.led].redF();
            g += m_colors[k.led].greenF();
            b += m_colors[k.led].blueF();
        }
        const double count = qMax(1, int(m_layout.keys().size()));
        m_ambient = QColor::fromRgbF(r / count, g / count, b / count);
        ++m_frame;
        emit frameChanged();
    }
}

void Backend::refreshDevice() {
    QString status;
    switch (Device::check()) {
    case Device::Ready: status = "ready"; break;
    case Device::NoPermission: status = "permission"; break;
    default: status = "missing"; break;
    }
    if (status != m_deviceStatus) {
        m_deviceStatus = status;
        emit deviceStatusChanged();
    }
}

bool Backend::autostart() const { return QFileInfo::exists(m_autostartPath); }

void Backend::setAutostart(bool on) {
    if (on == autostart())
        return;
    if (on) {
        QDir().mkpath(QFileInfo(m_autostartPath).absolutePath());
        QSaveFile file(m_autostartPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QString("[Desktop Entry]\nType=Application\nName=Keyglow lights\n"
                               "Exec=%1 daemon\nNoDisplay=true\n")
                           .arg(QCoreApplication::applicationFilePath()).toUtf8());
            file.commit();
        }
    } else {
        QFile::remove(m_autostartPath);
    }
    emit autostartChanged();
}

// ---------------------------------------------------------------- Teaching

int Backend::teachFound() const {
    if (m_quick)
        return qMin(m_quick->answers(), teachExpected());
    int found = 0;
    for (const auto &p : m_teachPairs)
        found += p.second >= 0; // lights that aren't keys don't count
    return found;
}

void Backend::setNotice(const QString &notice) {
    m_notice = notice;
    emit noticeChanged();
    if (!notice.isEmpty())
        m_noticeTimer.start();
}

void Backend::startQuickSync() {
    m_quick = std::make_unique<QuickSync>();
    m_quickUsages.clear();
    m_teachNote.clear();
    showTeachLed(m_quick->nextLed());
}

void Backend::quickAdvance() {
    if (m_quick->failed()) {
        // Not wired the way these boards usually are: check every light instead.
        const QString why = tr("This keyboard isn't wired the usual way, so Keyglow will check every light instead. Same idea: press the key under the white light.");
        startTeaching();
        m_teachNote = why;
        emit teachChanged();
        return;
    }
    const int next = m_quick->nextLed();
    if (next >= 0) {
        showTeachLed(next);
        return;
    }
    m_layout = KeyboardLayout::fromPairs(m_quick->result());
    m_layout.save(m_layoutPath);
    m_engine.setLayout(m_layout);
    ++m_layoutVersion;
        emit layoutChanged();
    cancelTeaching();
    setNotice(tr("Synced. Every light is now matched to its key. Try Typing heat and type a little to see it."));
}

void Backend::startTeaching() {
    m_quick.reset();
    m_teachPairs.clear();
    m_teachTrail.clear();
    m_teachSeen.clear();
    m_teachSweep = false;
    m_teachNote.clear();
    showTeachLed(0);
}

void Backend::showTeachLed(int led) {
    m_teachLed = led;
    m_teachShownAt = QDateTime::currentMSecsSinceEpoch();
    m_settings.identify = led;
    m_settings.save(m_configPath); // straight away: the user is waiting on the light
    m_save.stop();
    emit teachChanged();
    render();
}

// The first pass avoids most dead ends: the first matrix column tells which
// matrix rows have keys at all, later columns only try those rows, and a whole
// empty column past the board's width ends the pass. Anything skipped is only
// tried (the "sweep") if the count comes up short of the Artemis's 68 keys.
int Backend::nextTeachLed(int after) const {
    if (m_teachSweep) {
        for (int led = 0; led < Leds; ++led)
            if (!m_teachSeen.contains(led))
                return led;
        return -1;
    }
    QSet<int> liveRows, liveColumns;
    for (const auto &p : m_teachPairs) {
        if (p.first / Rows == 0)
            liveRows.insert(p.first % Rows);
        liveColumns.insert(p.first / Rows);
    }
    for (int led = after + 1; led < Leds; ++led) {
        const int column = led / Rows, row = led % Rows;
        if (column == 0)
            return led;
        if (column > 13 && !liveColumns.contains(column - 1))
            return -1;
        if (liveRows.contains(row))
            return led;
    }
    return -1;
}

void Backend::teachAdvance() {
    m_teachSeen.insert(m_teachLed);
    m_teachTrail.append(m_teachLed);
    int next = nextTeachLed(m_teachLed);
    if (next < 0 && !m_teachSweep && teachFound() < teachExpected()) {
        m_teachSweep = true;
        m_teachNote = tr("Checking the remaining lights for the last few keys.");
        next = nextTeachLed(-1);
    }
    if (next < 0)
        finishTeaching();
    else
        showTeachLed(next);
}

void Backend::teachPress(int usage) {
    if (!teaching())
        return;
    // The press that answered the previous light is still on its way up.
    if (QDateTime::currentMSecsSinceEpoch() - m_teachShownAt < 180)
        return;
    if (m_quick) {
        // A second answer with the same key is a slip, not information.
        if (m_quickUsages.contains(usage)) {
            m_teachNote = tr("%1 was already used. Press the key under the white light now.")
                              .arg(KeyboardLayout::labelFor(usage));
            emit teachChanged();
            return;
        }
        m_quickUsages.insert(usage);
        m_teachNote.clear();
        m_quick->pressed(m_teachLed, usage);
        quickAdvance();
        return;
    }
    for (const auto &p : m_teachPairs)
        if (p.second == usage) {
            m_teachNote = tr("%1 is already matched. Press the key that is glowing now, or go back.")
                              .arg(KeyboardLayout::labelFor(usage));
            emit teachChanged();
            return;
        }
    m_teachNote.clear();
    m_teachPairs.append({m_teachLed, usage});
    teachAdvance();
}

void Backend::teachNothing() {
    if (!teaching())
        return;
    m_teachNote.clear();
    if (m_quick) {
        m_quick->nothing(m_teachLed);
        quickAdvance();
        return;
    }
    teachAdvance();
}

void Backend::teachSilent(bool isKey) {
    if (!teaching())
        return;
    m_teachNote.clear();
    if (m_quick) {
        // A light with no key under it means this isn't the plain 68-key board.
        if (isKey) {
            m_quick->silentKey(m_teachLed);
            quickAdvance();
        } else {
            startTeaching();
        }
        return;
    }
    m_teachPairs.append({m_teachLed, isKey ? 0 : -1});
    teachAdvance();
}

void Backend::teachBack() {
    if (m_quick) {
        startQuickSync(); // few enough answers that starting over is the simple undo
        return;
    }
    if (!teaching() || m_teachTrail.isEmpty())
        return;
    const int led = m_teachTrail.takeLast();
    m_teachSeen.remove(led);
    for (int i = m_teachPairs.size() - 1; i >= 0; --i)
        if (m_teachPairs[i].first == led)
            m_teachPairs.removeAt(i);
    m_teachNote.clear();
    showTeachLed(led);
}

void Backend::finishTeaching() {
    if (!teaching())
        return;
    if (!m_quick && !m_teachPairs.isEmpty()) {
        m_layout = KeyboardLayout::fromPairs(m_teachPairs);
        m_layout.save(m_layoutPath);
        m_engine.setLayout(m_layout);
        ++m_layoutVersion;
        emit layoutChanged();
    }
    cancelTeaching();
}

void Backend::cancelTeaching() {
    m_quick.reset();
    m_teachLed = -1;
    m_teachNote.clear();
    m_settings.identify = -1;
    m_settings.save(m_configPath);
    emit teachChanged();
    render();
}

void Backend::forgetLayout() {
    QFile::remove(m_layoutPath);
    m_layout = KeyboardLayout();
    m_engine.setLayout(m_layout);
    ++m_layoutVersion;
        emit layoutChanged();
    // Nudge the streamer, which reloads the layout whenever a file changes.
    m_settings.save(m_configPath);
    render();
}

QJsonObject Cli::scenes(const Backend &backend) {
    QJsonArray scenes;
    for (const QVariant &v : backend.presets()) {
        QJsonObject scene = QJsonObject::fromVariantMap(v.toMap());
        const QString id = scene.value("id").toString();
        const Preset &p = Engine::preset(id);
        // A few colours to draw a swatch with; scenes that take the user's
        // colour have none of their own.
        QJsonArray palette;
        for (const QColor &c : p.palette)
            palette.append(c.name());
        scene.insert("palette", palette);
        scene.insert("usesSpeed", p.usesSpeed);
        scenes.append(scene);
    }
    return {{"categories", QJsonArray::fromVariantList(backend.categories())}, {"scenes", scenes}};
}

QJsonObject Cli::status(const Backend &backend) {
    const Settings &s = backend.settings();
    return {{"version", KEYGLOW_VERSION},
            {"power", s.power},
            {"preset", s.preset},
            {"name", backend.current().value("name").toString()},
            {"brightness", s.brightness},
            {"speed", s.speed},
            {"color", s.color.name()},
            {"usesColor", backend.usesColor()},
            {"usesSpeed", backend.usesSpeed()},
            {"device", backend.deviceStatus()},
            {"synced", backend.taught()},
            {"whiteRed", s.whiteRed},
            {"whiteBlue", s.whiteBlue},
            {"favorites", QJsonArray::fromStringList(s.favorites)}};
}
