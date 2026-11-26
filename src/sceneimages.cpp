#include "sceneimages.h"

#include "backend.h"
#include "theme.h"

#include <QDir>
#include <QPainter>

QImage SceneImages::requestImage(const QString &id, QSize *size, const QSize &) {
    const QStringList parts = id.split('/');
    const int tick = parts.value(2).toInt();
    const KeyboardLayout &layout = m_backend->layout();

    Settings settings = m_backend->settings();
    settings.preset = Engine::preset(parts.value(0)).id;
    settings.power = true;
    settings.brightness = 100;
    settings.speed = 50;
    settings.identify = -1;

    Engine engine;
    engine.setLayout(layout);
    engine.setAccent(QColor(Theme::readAccent(
        QDir::homePath() + "/.local/state/omarchy/current/theme/colors.toml")));
    // A fixed moment, so every card is the same picture until it is hovered.
    // (Chosen to fall on a beat of the Heartbeat scene, which is dark between beats.)
    const qint64 now = 40524 + tick * 90;
    // Scenes that answer typing get a few pretend key presses to show for it.
    if (Engine::preset(settings.preset).reactive)
        for (int i = 0; i < 5; ++i) {
            const int usages[] = {0x09, 0x0E, 0x1A, 0x2C, 0x37};
            engine.press(usages[i], now - 250 - (i * 730 + tick * 90) % 1900);
        }
    const QVector<QColor> colors = engine.render(settings, now);

    const double unit = 22;
    QImage image(int(layout.width() * unit), int(layout.height() * unit), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    const bool unlit = Engine::preset(settings.preset).kind == Kind::Onboard;
    for (const Key &k : layout.keys()) {
        painter.setBrush(unlit ? QColor("#3a3a44") : colors.value(k.led));
        painter.drawRoundedRect(QRectF(k.x * unit + 1.5, k.y * unit + 1.5, k.w * unit - 3, unit - 3), 4, 4);
    }
    if (size)
        *size = image.size();
    return image;
}
