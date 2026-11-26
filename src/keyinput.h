#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QSocketNotifier>
#include <QTimer>

// Watches which keys are held, straight from the keyboard's HID reports, so
// the lights can react to typing. Presses are only turned into light: they
// are never stored or sent anywhere. It runs only while something needs it.
class KeyInput : public QObject {
    Q_OBJECT

public:
    explicit KeyInput(QObject *parent = nullptr);
    ~KeyInput() override;

    void setActive(bool active);
    bool isDown(int usage) const;

    // The keys held in one report, as HID usages. `data` is a boot keyboard
    // report (8 bytes, no id) or the n-key report (id 4, one bit per usage).
    static QSet<int> parse(const QByteArray &data);

signals:
    void pressed(int usage);
    void changed();

private:
    void open();
    void close();
    void read(int fd);

    bool m_active = false;
    QHash<int, QSocketNotifier *> m_notifiers;
    QHash<int, QSet<int>> m_down; // per node, since each reports its own keys
    QTimer m_retry;
};
