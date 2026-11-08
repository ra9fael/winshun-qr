// QrPreview.cpp - the preview window's lifecycle, all on the GUI thread.
//
// One QQmlEngine lives for the process (QtQuick comes from WinShun's qml\
// folder). There is at most one preview window, and a new one is never built
// while an old one is alive (spec §4): showPreview() destroys the previous
// instance first — the DOMINO crash of a reused HWND is exactly what this
// avoids — then builds a fresh window from Preview.qml.
//
// Destruction is close() + deleteLater(), never a direct delete: the close
// request arrives from the window's own event handling (Esc, or the focus
// loss that means "clicked outside"), and a delete there would return through
// frames that belong to the freed window — the 0xC0000409 crashes. The
// DeferredDelete is flushed before the next window is built, so the HWND
// guarantee still holds.
#include "QrPreview.h"
#include "PreviewController.h"
#include "../winshun.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QThread>

using namespace Qt::StringLiterals;

namespace ra9fael::qr {
namespace {

QQmlEngine* g_engine = nullptr;
PreviewController* g_controller = nullptr;
QQuickWindow* g_window = nullptr;

QString appDir()
{
    return qEnvironmentVariable("WS_APP_DIR"); // set by the stub
}

void destroyWindow(); // defined below; the controller's close() leads here

PreviewController* ensureController()
{
    if (g_controller)
        return g_controller;
    g_engine = new QQmlEngine;
    g_engine->addImportPath(appDir() + u"/qml"_s); // QtQuick: WinShun's
    g_controller = new PreviewController;
    g_engine->addImageProvider(u"qr"_s, new QrImageProvider(g_controller));
    // The QML's Esc and the click-outside both come through close().
    QObject::connect(g_controller, &PreviewController::closeRequested, g_controller,
                     [] { destroyWindow(); });
    return g_controller;
}

void flushWindowDeletes()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void destroyWindow()
{
    if (!g_window)
        return;
    g_window->close();       // the HWND is destroyed here
    g_window->deleteLater(); // never a direct delete: see the file comment
    g_window = nullptr;
}

bool guiShow(const PreviewSpec& spec, const Settings& settings, std::string& error)
{
    destroyWindow();
    flushWindowDeletes(); // the old window is really gone before the next HWND
    PreviewController* controller = ensureController();
    controller->setText(QString::fromStdString(spec.text), spec.zh, settings);

    QQmlComponent component(g_engine, QUrl(u"qrc:/qt/qml/QrApp/Preview.qml"_s),
                            QQmlComponent::PreferSynchronous);
    if (component.isError()) {
        error = "ra9fael.qr: Preview.qml: " + component.errorString().toStdString();
        return false;
    }
    QObject* object = component.createWithInitialProperties({
        {u"preview"_s, QVariant::fromValue<QObject*>(controller)},
    });
    auto* window = qobject_cast<QQuickWindow*>(object);
    if (!window) {
        error = "ra9fael.qr: Preview.qml is not a window: " + component.errorString().toStdString();
        delete object;
        return false;
    }
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen)
        window->setScreen(screen);
    window->create(); // native handle now, so show() is one call
    g_window = window;
    window->show();
    window->requestActivate(); // Esc and the click-outside need the focus
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);
    winshun::debug("ra9fael.qr: preview shown, " + std::to_string(spec.text.size()) + " bytes");
    return true;
}

std::string guiClipboardText()
{
    return QGuiApplication::clipboard()->text(QClipboard::Clipboard).toStdString();
}

void guiPutImage(const Bitmap& bmp)
{
    QImage image(reinterpret_cast<const uchar*>(bmp.pixels.data()), bmp.width, bmp.height,
                 bmp.width * 4, QImage::Format_RGB32);
    image.detach();
    QGuiApplication::clipboard()->setImage(image, QClipboard::Clipboard);
}

} // namespace

bool showPreview(const PreviewSpec& spec, const Settings& settings, std::string& error)
{
    if (QThread::currentThread() == qGuiApp->thread())
        return guiShow(spec, settings, error);
    std::string guiError;
    QMetaObject::invokeMethod(qGuiApp, [&] { guiShow(spec, settings, guiError); },
                              Qt::BlockingQueuedConnection);
    error = std::move(guiError);
    return error.empty();
}

void closePreview()
{
    if (!g_engine)
        return;
    if (QThread::currentThread() == qGuiApp->thread()) {
        destroyWindow();
        return;
    }
    QMetaObject::invokeMethod(qGuiApp, [] { destroyWindow(); }, Qt::BlockingQueuedConnection);
}

std::string clipboardText()
{
    if (QThread::currentThread() == qGuiApp->thread())
        return guiClipboardText();
    std::string out;
    QMetaObject::invokeMethod(qGuiApp, [&] { out = guiClipboardText(); },
                              Qt::BlockingQueuedConnection);
    return out;
}

void putImageOnClipboard(const Bitmap& bmp)
{
    if (QThread::currentThread() == qGuiApp->thread()) {
        guiPutImage(bmp);
        return;
    }
    QMetaObject::invokeMethod(qGuiApp, [&] { guiPutImage(bmp); }, Qt::BlockingQueuedConnection);
}

} // namespace ra9fael::qr
