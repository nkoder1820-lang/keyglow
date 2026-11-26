#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

#include "device.h"
#include "keyinput.h"
#include "lighting.h"

// The background half of the app. The keyboard only stays in direct mode
// while frames keep arriving (it falls back to its own effect after about a
// second), so this keeps sending them after the window is closed.
class Streamer : public QObject {
    Q_OBJECT

public:
    explicit Streamer(QObject *parent = nullptr);

private:
    void reload();
    void tick();

    QString m_configPath, m_layoutPath;
    Settings m_settings;
    Engine m_engine;
    Device m_device;
    KeyInput m_keys;
    QFileSystemWatcher m_watcher;
    QTimer m_timer;
    QByteArray m_last;
    qint64 m_lastSent = 0;
    int m_accentCountdown = 0;
};
