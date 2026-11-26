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

void Streamer::reload() {
    // Saving replaces the files, which drops them from the watch; re-arm each time.
    const QString directory = QFileInfo(m_configPath).absolutePath();
    if (!m_watcher.directories().contains(directory))
        m_watcher.addPath(directory);
    for (const QString &path : {m_configPath, m_layoutPath})
        if (QFileInfo::exists(path) && !m_watcher.files().contains(path))
            m_watcher.addPath(path);
    m_settings = Settings::load(m_configPath);
    m_engine.setLayout(KeyboardLayout::load(m_layoutPath));
    // Key presses are only read while a preset that reacts to typing is on.
    m_keys.setActive(m_settings.power && m_settings.identify < 0 && Engine::preset(m_settings.preset).reactive);
    m_accentCountdown = 0;
}

void Streamer::tick() {
    if (Engine::preset(m_settings.preset).kind == Kind::Onboard && m_settings.power && m_settings.identify < 0) {
        // Hand the keyboard back to its own effect by going quiet.
        m_device.close();
        m_last.clear();
        return;
    }
    // The desktop accent, for the preset that follows the Omarchy theme.
    if (--m_accentCountdown <= 0) {
        m_accentCountdown = 50;
        m_engine.setAccent(QColor(Theme::readAccent(
            QDir::homePath() + "/.local/state/omarchy/current/theme/colors.toml")));
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QByteArray frame = Lighting::frame(Lighting::balanced(m_engine.render(m_settings, now), m_settings));
    // A still picture only needs a keep-alive; motion goes out every tick.
    if (frame == m_last && now - m_lastSent < 400)
        return;
    if (m_device.send(frame)) {
        m_last = frame;
        m_lastSent = now;
        m_timer.setInterval(40);
    } else {
        m_last.clear();
        m_timer.setInterval(2000); // unplugged: look again now and then
    }
}
