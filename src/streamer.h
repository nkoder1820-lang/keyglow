#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

#include "device.h"
#include "keyinput.h"
#include "lighting.h"

// The background half of the app: it draws the scene and sends it to the
// keyboard, and keeps doing so after the window is closed. (The Artemis only
// stays in direct mode while frames keep arriving; it falls back to its own
// effect after about a second.)
class Streamer : public QObject {
    Q_OBJECT

public:
    explicit Streamer(QObject *parent = nullptr);
    ~Streamer() override;

private:
    void reload();
    void tick();
    void useDevice();

    QString m_configPath, m_layoutPath;
    Settings m_settings;
    Engine m_engine;
    Device m_device;
    KeyInput m_keys;
    QFileSystemWatcher m_watcher;
    QTimer m_timer;
    QVector<QColor> m_last;
    Device::Driver m_driver = Device::None;
    qint64 m_lastSent = 0;
    int m_accentCountdown = 0;
};
