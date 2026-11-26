#include "device.h"

#include "lighting.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cerrno>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <unistd.h>

QString Device::findPath(const QString &sysRoot) {
    const auto nodes = QDir(sysRoot).entryList({"hidraw*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &node : nodes) {
        QFile uevent(sysRoot + "/" + node + "/device/uevent");
        if (!uevent.open(QIODevice::ReadOnly) || !uevent.readAll().contains("0000258A:0000010C"))
            continue;
        // The keyboard exposes two HID interfaces. Lighting is the one with a
        // vendor page (06 00 FF) carrying feature report 6.
        QFile descriptor(sysRoot + "/" + node + "/device/report_descriptor");
        if (!descriptor.open(QIODevice::ReadOnly))
            continue;
        const QByteArray d = descriptor.readAll();
        if (d.contains(QByteArray::fromHex("0600ff")) && d.contains(QByteArray::fromHex("8506")))
            return "/dev/" + node;
    }
    return {};
}

QStringList Device::findAllPaths(const QString &sysRoot) {
    QStringList paths;
    const auto nodes = QDir(sysRoot).entryList({"hidraw*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &node : nodes) {
        QFile uevent(sysRoot + "/" + node + "/device/uevent");
        if (uevent.open(QIODevice::ReadOnly) && uevent.readAll().contains("0000258A:0000010C"))
            paths.append("/dev/" + node);
    }
    return paths;
}

QString Device::hidrawRoot() { return qEnvironmentVariable("KEYGLOW_HIDRAW_ROOT", "/sys/class/hidraw"); }

Device::Status Device::check() {
    const QString path = findPath(hidrawRoot());
    if (path.isEmpty())
        return Missing;
    return access(path.toLocal8Bit().constData(), R_OK | W_OK) == 0 ? Ready : NoPermission;
}

static const char *kNativeName = "CB Artemis";

QString Device::layoutPath(const QString &directory, Driver driver) {
    return directory + (driver == ViaOpenRgb ? "/openrgb-layout.json" : "/layout.json");
}

Device::Info Device::info() const {
    Info i;
    i.status = m_status;
    i.driver = driver();
    i.name = i.driver == ViaOpenRgb ? m_openRgb.controller().name : i.driver == Native ? kNativeName : QString();
    return i;
}

void Device::writeState(const QString &directory) const {
    const Info i = info();
    if (i.driver == ViaOpenRgb)
        layout().save(layoutPath(directory, ViaOpenRgb));
    QSaveFile file(directory + "/device.json");
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(QJsonObject{{"driver", i.driver == ViaOpenRgb ? "openrgb" : i.driver == Native ? "native" : ""},
                                         {"name", i.name},
                                         {"pid", qint64(getpid())}})
                   .toJson(QJsonDocument::Compact));
    file.commit();
}

void Device::clearState(const QString &directory) { QFile::remove(directory + "/device.json"); }

Device::Info Device::probe(const QString &directory) {
    Info i;
    i.status = check();
    if (i.status != Missing) {
        i.driver = Native;
        i.name = kNativeName;
        return i;
    }
    // Not the native keyboard: the streamer says what it found, if it is
    // still running.
    QFile file(directory + "/device.json");
    if (!file.open(QIODevice::ReadOnly))
        return i;
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    if (o.value("driver").toString() == "openrgb" && QFileInfo::exists("/proc/" + QString::number(o.value("pid").toInteger()))) {
        i.status = Ready;
        i.driver = ViaOpenRgb;
        i.name = o.value("name").toString();
    }
    return i;
}

bool Device::open() {
    close();
    const QString path = findPath(hidrawRoot());
    if (path.isEmpty()) {
        m_status = m_openRgb.open() ? Ready : Missing;
        return m_status == Ready;
    }
    m_fd = ::open(path.toLocal8Bit().constData(), O_RDWR | O_CLOEXEC);
    if (m_fd < 0) {
        m_status = errno == EACCES ? NoPermission : Missing;
        return false;
    }

    // Other keyboards reuse this USB id with different firmware, and writing
    // the wrong protocol to those has bricked some. Ask the keyboard to
    // identify itself (a read) and only go on if it answers as this family.
    QByteArray query(Lighting::FrameSize, '\0');
    memcpy(query.data(), "\x06\x82\x01\x00\x01\x00\x06", 7);
    QByteArray reply(Lighting::FrameSize, '\0');
    reply[0] = 0x06;
    if (ioctl(m_fd, HIDIOCSFEATURE(query.size()), query.data()) < 0
        || ioctl(m_fd, HIDIOCGFEATURE(reply.size()), reply.data()) < 14
        || quint8(reply[1]) != 0x82) {
        close();
        m_status = Unsupported;
        return false;
    }
    m_model = quint8(reply[13]);
    m_status = Ready;
    return true;
}

bool Device::send(const QVector<QColor> &colors) {
    if (!isOpen() && !open())
        return false;
    if (m_fd < 0) {
        if (m_openRgb.send(colors))
            return true;
        close();
        m_status = Missing;
        return false;
    }
    const QByteArray frame = Lighting::frame(colors);
    if (ioctl(m_fd, HIDIOCSFEATURE(frame.size()), frame.constData()) < 0) {
        // Unplugged, or asleep: drop the handle and look for it again next time.
        close();
        m_status = Missing;
        return false;
    }
    return true;
}

void Device::close() {
    if (m_fd >= 0)
        ::close(m_fd);
    m_fd = -1;
    m_openRgb.close();
}
