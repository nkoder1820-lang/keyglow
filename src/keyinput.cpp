#include "keyinput.h"

#include "device.h"

#include <fcntl.h>
#include <unistd.h>

KeyInput::KeyInput(QObject *parent) : QObject(parent) {
    // The keyboard may be plugged in later, or replugged.
    m_retry.setInterval(2000);
    connect(&m_retry, &QTimer::timeout, this, [this] {
        if (m_active && m_notifiers.isEmpty())
            open();
    });
}

KeyInput::~KeyInput() { close(); }

void KeyInput::setActive(bool active) {
    if (active == m_active)
        return;
    m_active = active;
    if (active) {
        open();
        m_retry.start();
    } else {
        m_retry.stop();
        close();
    }
}

void KeyInput::open() {
    for (const QString &path : Device::findAllPaths()) {
        const int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        auto *notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(notifier, &QSocketNotifier::activated, this, [this, fd] { read(fd); });
        m_notifiers.insert(fd, notifier);
    }
}

void KeyInput::close() {
    for (auto it = m_notifiers.begin(); it != m_notifiers.end(); ++it) {
        delete it.value();
        ::close(it.key());
    }
    m_notifiers.clear();
    m_down.clear();
}

bool KeyInput::isDown(int usage) const {
    for (const QSet<int> &set : m_down)
        if (set.contains(usage))
            return true;
    return false;
}

QSet<int> KeyInput::parse(const QByteArray &data) {
    QSet<int> keys;
    const auto *d = reinterpret_cast<const quint8 *>(data.constData());
    if (data.size() == 8) {
        for (int bit = 0; bit < 8; ++bit)
            if (d[0] & (1 << bit))
                keys.insert(0xE0 + bit);
        for (int i = 2; i < 8; ++i)
            if (d[i] >= 0x04)
                keys.insert(d[i]);
    } else if (data.size() >= 2 && d[0] == 0x04) {
        for (int i = 1; i < data.size(); ++i)
            for (int bit = 0; bit < 8; ++bit)
                if (d[i] & (1 << bit))
                    keys.insert(0x04 + (i - 1) * 8 + bit);
    }
    return keys;
}

void KeyInput::read(int fd) {
    char buffer[64];
    for (;;) {
        const ssize_t n = ::read(fd, buffer, sizeof buffer);
        if (n == 0 || (n < 0 && errno != EAGAIN)) {
            // Unplugged: drop everything and let the retry find it again.
            close();
            emit changed();
            return;
        }
        if (n < 0)
            return;
        const QByteArray data(buffer, int(n));
        // Media keys, the mouse emulation and vendor chatter are not keys.
        if (data.size() != 8 && quint8(data[0]) != 0x04)
            continue;
        const QSet<int> now = parse(data), before = m_down.value(fd);
        m_down[fd] = now;
        for (int usage : now)
            if (!before.contains(usage))
                emit pressed(usage);
        emit changed();
    }
}
