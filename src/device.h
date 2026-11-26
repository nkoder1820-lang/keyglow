#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <QColor>
#include <QVector>

#include "lighting.h"
#include "openrgb.h"

// The keyboard being lit, whichever way it is reached. Two drivers:
//  - Native: the SinoWealth 258a:010c controller (sold as the Cosmic Byte CB
//    Artemis, among others), spoken to directly over hidraw.
//  - ViaOpenRgb: any keyboard a running OpenRGB server lists.
// The native one is tried first; OpenRGB is only asked when it isn't there.
class Device {
public:
    enum Status { Ready, Missing, NoPermission, Unsupported };
    enum Driver { None, Native, ViaOpenRgb };
    struct Info {
        Status status = Missing;
        Driver driver = None;
        QString name;
    };

    ~Device() { close(); }

    // Where the lighting interface is, or empty when the keyboard is unplugged.
    static QString findPath(const QString &sysRoot = "/sys/class/hidraw");
    // Every hidraw node of the keyboard: key presses arrive on both.
    static QStringList findAllPaths(const QString &sysRoot = "/sys/class/hidraw");
    // Where hidraw devices are listed; KEYGLOW_HIDRAW_ROOT moves it, for tests.
    static QString hidrawRoot();
    // Whether we could talk to the native keyboard, without sending anything.
    static Status check();

    // What the window and the command line show. They don't open the
    // keyboard themselves: the native one is checked on the spot, and for
    // anything else the streamer's note in `directory` is read.
    static Info probe(const QString &directory);
    // The streamer's note of what it is driving, and the layout it was given.
    void writeState(const QString &directory) const;
    static void clearState(const QString &directory);
    static QString layoutPath(const QString &directory, Driver driver);
    // Red and blue next to full green for white to look white, in percent.
    // The Artemis's green LEDs are far weaker than the other two.
    static int defaultWhite(Driver driver) { return driver == Native ? 28 : 100; }

    bool open();
    bool isOpen() const { return m_fd >= 0 || m_openRgb.isOpen(); }
    // One colour per LED.
    bool send(const QVector<QColor> &colors);
    void close();
    Status status() const { return m_status; }
    Driver driver() const { return m_fd >= 0 ? Native : m_openRgb.isOpen() ? ViaOpenRgb : None; }
    Info info() const;
    // Only for drivers that know their own layout.
    KeyboardLayout layout() const { return ::OpenRgb::layoutFor(m_openRgb.controller()); }
    int model() const { return m_model; }

private:
    int m_fd = -1;
    int m_model = -1;
    Status m_status = Missing;
    ::OpenRgb::Client m_openRgb;
};
