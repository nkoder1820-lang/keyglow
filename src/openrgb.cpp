#include "openrgb.h"

#include <QHash>
#include <QtEndian>

namespace {

enum Packet : quint32 {
    ControllerCount = 0,
    ControllerData = 1,
    ProtocolVersion = 40,
    SetClientName = 50,
    DeviceListUpdated = 100,
    UpdateLeds = 1050,
    SetCustomMode = 1100,
};

// The oldest protocol that names the vendor. Servers answer in the version
// the client asks for, and the older layouts are the ones least likely to
// change, so nothing newer is requested.
constexpr quint32 kProtocol = 1;

// Little-endian, and 0 for anything past the end.
quint32 get32(const QByteArray &b, int at) {
    quint32 v = 0;
    for (int i = 0; i < 4; ++i)
        if (at + i < b.size())
            v |= quint32(quint8(b.at(at + i))) << (8 * i);
    return v;
}

void put32(QByteArray &b, quint32 v) {
    char raw[4];
    qToLittleEndian(v, raw);
    b.append(raw, 4);
}

void put16(QByteArray &b, quint16 v) {
    char raw[2];
    qToLittleEndian(v, raw);
    b.append(raw, 2);
}

// Reads the reply's fields in order; once anything runs past the end, `ok`
// goes false and every later read returns nothing.
struct Reader {
    const QByteArray &data;
    int at = 0;
    bool ok = true;

    bool has(int n) {
        if (n < 0 || at + n > data.size())
            ok = false;
        return ok;
    }
    quint32 u32() {
        if (!has(4))
            return 0;
        at += 4;
        return get32(data, at - 4);
    }
    quint16 u16() {
        if (!has(2))
            return 0;
        at += 2;
        return quint16(get32(data, at - 2) & 0xFFFF);
    }
    void skip(int n) {
        if (has(n))
            at += n;
    }
    QString text() {
        const int n = u16();
        if (!has(n))
            return {};
        QByteArray raw = data.mid(at, n);
        at += n;
        while (raw.endsWith('\0'))
            raw.chop(1);
        return QString::fromUtf8(raw);
    }
};

struct Named { const char *name; int usage; const char *label; };
// OpenRGB's key names, after "Key: ", in lower case.
const Named kNames[] = {
    {"escape", 0x29, "Esc"}, {"`", 0x35, "`"}, {"-", 0x2D, "-"}, {"=", 0x2E, "="}, {"backspace", 0x2A, "Bksp"},
    {"tab", 0x2B, "Tab"}, {"[", 0x2F, "["}, {"]", 0x30, "]"}, {"\\ (ansi)", 0x31, "\\"}, {"\\ (iso)", 0x64, "\\"},
    {"\\", 0x31, "\\"}, {"caps lock", 0x39, "Caps"}, {";", 0x33, ";"}, {"'", 0x34, "'"}, {"#", 0x32, "#"},
    {"enter", 0x28, "Enter"}, {"enter (iso)", 0x28, "Enter"}, {"left shift", 0xE1, "Shift"}, {",", 0x36, ","},
    {".", 0x37, "."}, {"/", 0x38, "/"}, {"right shift", 0xE5, "Shift"}, {"left control", 0xE0, "Ctrl"},
    {"left windows", 0xE3, "Super"}, {"left alt", 0xE2, "Alt"}, {"space", 0x2C, ""}, {"right alt", 0xE6, "Alt"},
    {"right windows", 0xE7, "Super"}, {"menu", 0x65, "Menu"}, {"right control", 0xE4, "Ctrl"},
    {"right fn", 0, "Fn"}, {"left fn", 0, "Fn"}, {"fn", 0, "Fn"}, {"print screen", 0x46, "PrtSc"},
    {"scroll lock", 0x47, "ScrLk"}, {"pause/break", 0x48, "Pause"}, {"insert", 0x49, "Ins"}, {"home", 0x4A, "Home"},
    {"page up", 0x4B, "PgUp"}, {"delete", 0x4C, "Del"}, {"end", 0x4D, "End"}, {"page down", 0x4E, "PgDn"},
    {"up arrow", 0x52, "↑"}, {"left arrow", 0x50, "←"}, {"down arrow", 0x51, "↓"}, {"right arrow", 0x4F, "→"},
    {"num lock", 0x53, "Num"}, {"number pad /", 0x54, "/"}, {"number pad *", 0x55, "*"},
    {"number pad -", 0x56, "-"}, {"number pad +", 0x57, "+"}, {"number pad enter", 0x58, "Ent"},
    {"number pad .", 0x63, "."}, {"number pad 0", 0x62, "0"},
};

}

int OpenRgb::usageFor(const QString &ledName, QString *label) {
    QString name = ledName.trimmed();
    const bool isKey = name.startsWith("Key:", Qt::CaseInsensitive);
    if (isKey)
        name = name.mid(4).trimmed();
    const QString lower = name.toLower();
    int usage = 0;
    QString text;
    bool known = false;
    if (isKey) {
        if (lower.size() == 1 && lower[0] >= 'a' && lower[0] <= 'z') {
            usage = 0x04 + lower[0].unicode() - 'a';
            text = lower.toUpper();
            known = true;
        } else if (lower.size() == 1 && lower[0] >= '1' && lower[0] <= '9') {
            usage = 0x1E + lower[0].unicode() - '1';
            text = lower;
            known = true;
        } else if (lower == "0") {
            usage = 0x27;
            text = lower;
            known = true;
        } else if (lower.startsWith('f') && lower.mid(1).toInt() >= 1 && lower.mid(1).toInt() <= 12
                   && QString::number(lower.mid(1).toInt()) == lower.mid(1)) {
            usage = 0x39 + lower.mid(1).toInt();
            text = lower.toUpper();
            known = true;
        } else if (lower.startsWith("number pad ") && lower.size() == 12 && lower[11] >= '1' && lower[11] <= '9') {
            usage = 0x59 + lower[11].unicode() - '1';
            text = lower.right(1);
            known = true;
        } else {
            for (const Named &n : kNames)
                if (lower == QLatin1String(n.name)) {
                    usage = n.usage;
                    text = QString::fromUtf8(n.label);
                    known = true;
                    break;
                }
        }
    }
    // Anything else (media keys, a logo, "LED 12") keeps a short form of its name.
    if (!known)
        text = name.size() > 5 ? name.left(4) : name;
    if (label)
        *label = text;
    return usage;
}

bool OpenRgb::parse(const QByteArray &data, int protocol, Controller *out) {
    Reader r{data};
    Controller c;
    r.u32(); // size of the whole block
    c.type = int(r.u32());
    c.name = r.text();
    if (protocol >= 1)
        c.vendor = r.text();
    r.text(); // description
    r.text(); // version
    r.text(); // serial
    r.text(); // location

    const int modes = r.u16();
    r.u32(); // active mode
    for (int i = 0; i < modes && r.ok; ++i) {
        r.text();
        // value, flags, speed min and max, (brightness min and max), colour
        // count min and max, speed, (brightness), direction, colour mode
        r.skip(4 * (protocol >= 3 ? 12 : 9));
        r.skip(4 * r.u16());
    }

    const int zones = r.u16();
    for (int i = 0; i < zones && r.ok; ++i) {
        Zone z;
        z.name = r.text();
        r.u32(); // type
        r.u32(); // fewest LEDs
        r.u32(); // most LEDs
        z.leds = int(r.u32());
        const int matrixBytes = r.u16();
        if (matrixBytes >= 8) {
            z.rows = int(r.u32());
            z.columns = int(r.u32());
            const int cells = (matrixBytes - 8) / 4;
            for (int k = 0; k < cells && r.ok; ++k)
                z.map.append(r.u32());
            r.skip(matrixBytes - 8 - cells * 4);
            if (z.rows <= 0 || z.columns <= 0 || qint64(z.rows) * z.columns > z.map.size())
                z.rows = z.columns = 0;
        } else {
            r.skip(matrixBytes);
        }
        if (protocol >= 4) {
            const int segments = r.u16();
            for (int k = 0; k < segments && r.ok; ++k) {
                r.text();
                r.skip(12); // type, first LED, LED count
            }
        }
        if (protocol >= 5)
            r.u32(); // flags
        if (z.leds < 0 || z.leds > Lighting::MaxLeds)
            return false;
        c.zones.append(z);
    }

    const int leds = r.u16();
    for (int i = 0; i < leds && r.ok; ++i) {
        c.ledNames.append(r.text());
        r.u32(); // value
    }
    r.skip(4 * r.u16()); // the colours showing now
    if (!r.ok || leds == 0 || leds > Lighting::MaxLeds)
        return false;
    c.index = out->index;
    *out = c;
    return true;
}

KeyboardLayout OpenRgb::layoutFor(const Controller &c) {
    const int total = c.ledNames.size();
    QVector<Key> keys;
    QVector<bool> placed(total, false);
    struct Strip { int first, count; double y; };
    QVector<Strip> strips;
    double y0 = 0;
    int first = 0;

    for (const Zone &z : c.zones) {
        if (z.rows > 0) {
            for (int row = 0; row < z.rows; ++row) {
                // The keys of this row by column, to give the wide ones their width.
                QVector<int> at(z.columns, -1);
                for (int column = 0; column < z.columns; ++column) {
                    const quint32 v = z.map[row * z.columns + column];
                    if (v == NoLed || int(v) >= z.leds || first + int(v) >= total || placed[first + int(v)])
                        continue;
                    Key k;
                    k.led = first + int(v);
                    k.usage = usageFor(c.ledNames[k.led], &k.label);
                    k.x = column;
                    k.y = y0 + row;
                    placed[k.led] = true;
                    at[column] = keys.size();
                    keys.append(k);
                }
                // A matrix gives every key one cell, with empty cells where a
                // wide key spills over. The space bar takes the whole gap on
                // both sides; the other wide keys take a gap of one or two
                // cells on the side they grow towards.
                auto gap = [&](int column, int step) {
                    int n = 0;
                    for (int x = column + step; x >= 0 && x < z.columns; x += step, ++n)
                        if (at[x] >= 0)
                            return n;
                    return 0; // open to the edge: not a gap between keys
                };
                for (int column = 0; column < z.columns; ++column) {
                    if (at[column] < 0)
                        continue;
                    Key &k = keys[at[column]];
                    const bool space = k.usage == 0x2C;
                    const bool growsRight = space || k.usage == 0x2B || k.usage == 0x39 || k.usage == 0xE1;
                    const bool growsLeft = space || k.usage == 0x28 || k.usage == 0xE5 || k.usage == 0x2A;
                    const int right = growsRight ? gap(column, 1) : 0, left = growsLeft ? gap(column, -1) : 0;
                    if (space || right <= 2)
                        k.w += right;
                    if (space || left <= 2) {
                        k.x -= left;
                        k.w += left;
                    }
                }
            }
            y0 += z.rows;
        } else if (z.leds > 0) {
            strips.append({first, z.leds, y0});
            y0 += 1;
        }
        first += z.leds;
    }

    // Whatever no matrix placed: unmapped zones, and LEDs a map left out.
    QVector<int> loose;
    for (const Strip &s : strips)
        for (int i = 0; i < s.count && s.first + i < total; ++i)
            placed[s.first + i] = true;
    for (int i = 0; i < total; ++i)
        if (!placed[i])
            loose.append(i);

    double width = 1;
    for (const Key &k : keys)
        width = qMax(width, k.x + k.w);
    const bool hasMatrix = !keys.isEmpty();
    if (!hasMatrix)
        width = 15;

    // With a matrix, strips are lights around the keys: lit, but not drawn.
    // Without one they are all there is, so they are drawn as a row of blocks.
    QVector<Key> lights;
    auto spread = [&](int led, int i, int count, double y) {
        Key k;
        k.led = led;
        k.usage = hasMatrix ? -1 : usageFor(c.ledNames.value(led), &k.label);
        k.w = width / count;
        k.x = i * k.w;
        k.y = y;
        (hasMatrix ? lights : keys).append(k);
    };
    for (const Strip &s : strips)
        for (int i = 0; i < s.count && s.first + i < total; ++i)
            spread(s.first + i, i, s.count, hasMatrix ? qMin(s.y, y0 - 1) : s.y);
    for (int i = 0; i < loose.size(); ++i)
        spread(loose[i], i, loose.size(), hasMatrix ? y0 - 1 : y0);
    return KeyboardLayout::placed(keys, lights, total);
}

bool OpenRgb::Client::write(quint32 device, quint32 id, const QByteArray &payload) {
    QByteArray packet("ORGB");
    put32(packet, device);
    put32(packet, id);
    put32(packet, quint32(payload.size()));
    packet.append(payload);
    return m_socket.write(packet) == packet.size();
}

// Waits for the reply to `id`, stepping over anything else the server sends.
bool OpenRgb::Client::read(quint32 id, QByteArray *payload, int ms) {
    for (;;) {
        while (m_socket.bytesAvailable() < 16)
            if (!m_socket.waitForReadyRead(ms))
                return false;
        const QByteArray header = m_socket.read(16);
        if (header.size() < 16 || !header.startsWith("ORGB"))
            return false;
        const quint32 got = get32(header, 8);
        const quint32 size = get32(header, 12);
        if (size > 8 * 1024 * 1024)
            return false;
        while (m_socket.bytesAvailable() < qint64(size))
            if (!m_socket.waitForReadyRead(ms))
                return false;
        const QByteArray body = m_socket.read(size);
        if (got == id) {
            *payload = body;
            return true;
        }
    }
}

bool OpenRgb::Client::open(const QString &host, int port) {
    close();
    // KEYGLOW_OPENRGB=host:port points at a server somewhere else.
    const QStringList where = qEnvironmentVariable("KEYGLOW_OPENRGB").split(':');
    const QString toHost = !host.isEmpty() ? host : where.value(0).isEmpty() ? "127.0.0.1" : where.value(0);
    const int toPort = port > 0 ? port : where.value(1).toInt() > 0 ? where.value(1).toInt() : 6742;
    m_socket.connectToHost(toHost, quint16(toPort));
    if (!m_socket.waitForConnected(300)) {
        close();
        return false;
    }
    m_socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);

    write(0, SetClientName, QByteArray("Keyglow") + '\0');
    QByteArray body;
    put32(body, kProtocol);
    write(0, ProtocolVersion, body);
    // A server from before versions existed doesn't answer this at all.
    quint32 protocol = 0;
    if (read(ProtocolVersion, &body, 700) && body.size() >= 4)
        protocol = qMin(kProtocol, get32(body, 0));

    write(0, ControllerCount);
    if (!read(ControllerCount, &body, 3000) || body.size() < 4) {
        close();
        return false;
    }
    const quint32 count = qMin(quint32(256), get32(body, 0));
    Controller found;
    for (quint32 i = 0; i < count; ++i) {
        QByteArray ask;
        if (protocol >= 1)
            put32(ask, protocol);
        write(i, ControllerData, ask);
        Controller c;
        c.index = int(i);
        if (!read(ControllerData, &body, 3000))
            break;
        if (!parse(body, int(protocol), &c))
            continue;
        if (c.isKeyboard()) {
            found = c;
            break;
        }
    }
    if (found.index < 0) {
        close();
        return false;
    }
    m_controller = found;
    // "Custom" is the mode in which the device shows the colours it is sent.
    write(quint32(found.index), SetCustomMode);
    return m_socket.flush() || m_socket.state() == QAbstractSocket::ConnectedState;
}

bool OpenRgb::Client::send(const QVector<QColor> &colors) {
    if (!isOpen())
        return false;
    // The server says when its device list changed; the keyboard may have
    // moved or gone, so start over.
    while (m_socket.bytesAvailable() >= 16) {
        const QByteArray header = m_socket.read(16);
        if (header.size() < 16 || !header.startsWith("ORGB") || get32(header, 8) == DeviceListUpdated) {
            close();
            return false;
        }
        m_socket.read(get32(header, 12));
    }
    // A slow keyboard must not pile frames up: skip this one instead.
    if (m_socket.bytesToWrite() > 0) {
        m_socket.flush();
        if (m_socket.bytesToWrite() > 0)
            return m_socket.state() == QAbstractSocket::ConnectedState;
    }
    const int leds = m_controller.ledNames.size();
    QByteArray body;
    put32(body, quint32(4 + 2 + 4 * leds));
    put16(body, quint16(leds));
    for (int i = 0; i < leds; ++i) {
        const QColor c = colors.value(i, QColor(Qt::black));
        body.append(char(c.red()));
        body.append(char(c.green()));
        body.append(char(c.blue()));
        body.append('\0');
    }
    if (!write(quint32(m_controller.index), UpdateLeds, body)) {
        close();
        return false;
    }
    m_socket.flush();
    // Reading is what notices a server that went away.
    m_socket.waitForReadyRead(0);
    if (m_socket.state() != QAbstractSocket::ConnectedState) {
        close();
        return false;
    }
    return true;
}

void OpenRgb::Client::close() {
    m_socket.abort();
    m_controller = Controller();
}
