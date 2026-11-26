#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// The keyboard's lighting interface: a hidraw node of the SinoWealth 258a:010c
// controller (sold as the Cosmic Byte CB Artemis, among others).
class Device {
public:
    enum Status { Ready, Missing, NoPermission, Unsupported };

    ~Device() { close(); }

    // Where the lighting interface is, or empty when the keyboard is unplugged.
    static QString findPath(const QString &sysRoot = "/sys/class/hidraw");
    // Every hidraw node of the keyboard: key presses arrive on both.
    static QStringList findAllPaths(const QString &sysRoot = "/sys/class/hidraw");
    // Whether we could talk to it, without sending anything.
    static Status check();

    bool send(const QByteArray &frame);
    void close();
    Status status() const { return m_status; }
    int model() const { return m_model; }

private:
    bool open();

    int m_fd = -1;
    int m_model = -1;
    Status m_status = Missing;
};
