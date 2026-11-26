#include "streamer.h"

#include "theme.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

Streamer::Streamer(QObject *parent)
    : QObject(parent), m_configPath(Settings::defaultPath()), m_layoutPath(KeyboardLayout::defaultPath()) {
    QDir().mkpath(QFileInfo(m_configPath).absolutePath());
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Streamer::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &Streamer::reload);
    connect(&m_timer, &QTimer::timeout, this, &Streamer::tick);
    connect(&m_keys, &KeyInput::pressed, this, [this](int usage) {
        m_engine.press(usage, QDateTime::currentMSecsSinceEpoch());
    });
    reload();
    m_timer.start(40);
}

Streamer::~Streamer() { Device::clearState(QFileInfo(m_configPath).absolutePath()); }

// Called when the keyboard being driven changes: note it for the window, and
// take the layout from the driver if it knows its own.
void Streamer::useDevice() {
    m_driver = m_device.driver();
    const QString directory = QFileInfo(m_configPath).absolutePath();
    if (m_driver == Device::None)
        Device::clearState(directory);
    else
        m_device.writeState(directory);
    m_last.clear();
    reload();
}

void Streamer::reload() {
    // Saving replaces the files, which drops them from the watch; re-arm each time.
    const QString directory = QFileInfo(m_configPath).absolutePath();
    if (!m_watcher.directories().contains(directory))
        m_watcher.addPath(directory);
    for (const QString &path : {m_configPath, m_layoutPath})
        if (QFileInfo::exists(path) && !m_watcher.files().contains(path))
            m_watcher.addPath(path);
    m_settings = Settings::load(m_configPath);
    m_engine.setLayout(m_driver == Device::ViaOpenRgb ? m_device.layout() : KeyboardLayout::load(m_layoutPath));
    // Key presses are only read while a preset that reacts to typing is on.
    m_keys.setActive(m_settings.power && m_settings.identify < 0 && Engine::preset(m_settings.preset).reactive);
    m_accentCountdown = 0;
}

void Streamer::tick() {
    if (Engine::preset(m_settings.preset).kind == Kind::Onboard && m_settings.power && m_settings.identify < 0) {
        // Hand the keyboard back to its own effect by going quiet.
        if (m_driver == Device::Native)
            m_device.close();
        m_last.clear();
        return;
    }
    if (!m_device.isOpen()) {
        if (!m_device.open()) {
            if (m_driver != Device::None)
                useDevice();
            m_timer.setInterval(2000); // nothing there: look again now and then
            return;
        }
        useDevice();
    }
    // The desktop accent, for the preset that follows the Omarchy theme.
    if (--m_accentCountdown <= 0) {
        m_accentCountdown = 50;
        m_engine.setAccent(QColor(Theme::readAccent(
            QDir::homePath() + "/.local/state/omarchy/current/theme/colors.toml")));
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const int fallback = Device::defaultWhite(m_driver);
    const QVector<QColor> colors = Lighting::balanced(m_engine.render(m_settings, now),
                                                      m_settings.whiteRed > 0 ? m_settings.whiteRed : fallback,
                                                      m_settings.whiteBlue > 0 ? m_settings.whiteBlue : fallback);
    // A still picture only needs a keep-alive; motion goes out every tick.
    if (colors == m_last && now - m_lastSent < 400)
        return;
    if (m_device.send(colors)) {
        m_last = colors;
        m_lastSent = now;
        // Keyboards behind OpenRGB vary a lot in how fast they take a frame.
        m_timer.setInterval(m_driver == Device::ViaOpenRgb ? 66 : 40);
    } else {
        useDevice();
        m_timer.setInterval(2000); // unplugged: look again now and then
    }
}
