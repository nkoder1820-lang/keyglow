#pragma once

#include <QQuickImageProvider>

class Backend;

// Draws the little keyboard on each scene card: the real layout, lit the way
// that scene would light it. The image id is "<scene>/<colour>/<tick>/...";
// a card that is being hovered counts the tick up, which makes it play.
class SceneImages : public QQuickImageProvider {
public:
    explicit SceneImages(const Backend *backend)
        : QQuickImageProvider(QQuickImageProvider::Image), m_backend(backend) {}

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    const Backend *m_backend;
};
