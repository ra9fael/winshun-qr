// PreviewController.h - the preview window's model and its image source.
#ifndef RA9FAEL_PREVIEWCONTROLLER_H
#define RA9FAEL_PREVIEWCONTROLLER_H

#include <QObject>
#include <QQuickImageProvider>
#include <QString>

#include "../logic.h"

namespace ra9fael::qr {

// The preview window's model: the payload, the language, the render settings,
// and a generation counter the QML Image watches so a second preview redraws.
// Lives on the GUI thread, created with the engine (QrPreview.cpp).
class PreviewController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString text READ text CONSTANT)
    Q_PROPERTY(bool zh READ zh CONSTANT)
    Q_PROPERTY(int generation READ generation NOTIFY generationChanged)

public:
    explicit PreviewController(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    QString text() const { return m_text; }
    bool zh() const { return m_zh; }
    int generation() const { return m_generation; }
    const Settings& settings() const { return m_settings; }

    void setText(const QString& text, bool zh, const Settings& settings)
    {
        m_text = text;
        m_zh = zh;
        m_settings = settings;
        ++m_generation;
        emit generationChanged();
    }

    Q_INVOKABLE void close() { emit closeRequested(); }

signals:
    void generationChanged();
    void closeRequested();

private:
    QString m_text;
    bool m_zh = true;
    int m_generation = 0;
    Settings m_settings;
};

// The "qr" image scheme: image://qr/<generation>. The generation in the URL
// makes Qt re-request when the payload changes; the QML asks for no caching.
class QrImageProvider : public QQuickImageProvider {
public:
    explicit QrImageProvider(const PreviewController* controller)
        : QQuickImageProvider(QQuickImageProvider::Image), m_controller(controller)
    {
    }

    QImage requestImage(const QString& id, QSize* size, const QSize& requested) override
    {
        Q_UNUSED(id);
        Q_UNUSED(requested);
        const std::string text = m_controller->text().toStdString();
        const Code code = encode(text, m_controller->settings().ecc);
        if (!code.ok)
            return QImage(); // cannot happen: the row was built from a good encode
        const Bitmap bmp = renderImage(code, m_controller->settings());
        QImage image(reinterpret_cast<const uchar*>(bmp.pixels.data()), bmp.width, bmp.height,
                     bmp.width * 4, QImage::Format_RGB32);
        image.detach(); // own the pixels; bmp dies with this scope
        if (size)
            *size = image.size();
        return image;
    }

private:
    const PreviewController* m_controller;
};

} // namespace ra9fael::qr
#endif // RA9FAEL_PREVIEWCONTROLLER_H
