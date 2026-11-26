#pragma once

#include <QByteArray>
#include <QColor>
#include <QString>
#include <QStringList>
#include <QTcpSocket>
#include <QVector>

#include "lighting.h"

// Every keyboard Keyglow has no driver of its own for is reached through
// OpenRGB, which knows several hundred of them. OpenRGB runs as a local
// server ("openrgb --server", port 6742); this is a small client for the
// part of its SDK protocol that is needed: list the devices, pick the
// keyboard, and send it one colour per LED.
// Protocol: gitlab.com/CalcProgrammer1/OpenRGB, Documentation/OpenRGBSDK.md
namespace OpenRgb {

constexpr quint32 NoLed = 0xFFFFFFFF;

struct Zone {
    QString name;
    int leds = 0;
    int rows = 0, columns = 0; // 0 when the zone has no matrix map
    QVector<quint32> map;      // rows * columns LED indexes within the zone, NoLed for gaps
};

struct Controller {
    int index = -1;
    int type = -1;
    QString name, vendor;
    QVector<Zone> zones;
    QStringList ledNames;

    bool isKeyboard() const { return type == 5; }
};

// One "controller data" reply, as sent for protocol versions 0 to 5.
bool parse(const QByteArray &data, int protocol, Controller *out);

// The HID usage a key sends, from OpenRGB's name for its LED ("Key: A").
// 0 when the LED isn't a key with a usage; `label` gets a short key cap text.
int usageFor(const QString &ledName, QString *label = nullptr);

// Where everything sits, from the zones' matrix maps. Zones without a map
// (a light bar, or a keyboard lit in a few zones) are laid out in a row.
KeyboardLayout layoutFor(const Controller &controller);

class Client {
public:
    // Connects and picks the first keyboard. False when no server is
    // running or it has no keyboard.
    bool open(const QString &host = {}, int port = 0);
    bool isOpen() const { return m_socket.state() == QAbstractSocket::ConnectedState && m_controller.index >= 0; }
    bool send(const QVector<QColor> &colors);
    void close();
    const Controller &controller() const { return m_controller; }

private:
    bool write(quint32 device, quint32 id, const QByteArray &payload = {});
    bool read(quint32 id, QByteArray *payload, int ms);

    QTcpSocket m_socket;
    Controller m_controller;
};

}
