// Preview lifecycle and image actions run on the GUI thread.
#include "QrPreview.h"
#include "PreviewController.h"
#include "../winshun.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QThread>
#include <cmath>

using namespace Qt::StringLiterals;

namespace ra9fael::qr {
namespace {

QQmlEngine* g_engine = nullptr;
PreviewController* g_controller = nullptr;
QQuickWindow* g_window = nullptr;
QRect g_geometry;

void restoreSize()
{
    if (!g_window) return;
    const QRect work = g_window->screen()->availableGeometry();
    g_window->resize(qMin(660, work.width() - 20), qMin(730, work.height() - 50));
    g_window->setPosition(qBound(work.left(), g_window->x(), work.right() - g_window->width()),
                          qBound(work.top() + 30, g_window->y(), work.bottom() - g_window->height()));
}

void zoomWindow(double steps)
{
    if (!g_window || !std::isfinite(steps) || steps == 0)
        return;
    const QRect work = g_window->screen()->availableGeometry();
    const QRect old = g_window->geometry();
    const double factor = std::pow(1.15, qBound(-4.0, steps, 4.0));
    const int width = qBound(g_window->minimumWidth(), qRound((old.width() - 48) * factor) + 48,
                             qMax(g_window->minimumWidth(), work.width() - 20));
    const int height = qBound(g_window->minimumHeight(), qRound((old.height() - 112) * factor) + 112,
                              qMax(g_window->minimumHeight(), work.height() - 50));
    g_window->resize(width, height);
    g_window->setPosition(qBound(work.left(), old.center().x() - width / 2, work.right() - width),
                          qBound(work.top() + 30, old.center().y() - height / 2, work.bottom() - height));
}

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
    // Only explicit close actions destroy the preview.
    QObject::connect(g_controller, &PreviewController::closeRequested, g_controller,
                     [] { destroyWindow(); });
    QObject::connect(g_controller, &PreviewController::restoreRequested, g_controller, [] { restoreSize(); });
    QObject::connect(g_controller, &PreviewController::zoomRequested, g_controller, zoomWindow);
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
    g_geometry = g_window->geometry();
    auto* window = g_window;
    g_window = nullptr;
    window->close();
    window->deleteLater();
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
    if (g_geometry.isValid()) {
        const QRect work = window->screen()->availableGeometry();
        const QSize size = g_geometry.size().boundedTo(work.size() - QSize(20, 50));
        window->resize(size);
        window->setPosition(qBound(work.left(), g_geometry.x(), work.right() - size.width()),
                            qBound(work.top() + 30, g_geometry.y(), work.bottom() - size.height()));
    } else {
        restoreSize();
        const QRect work = window->screen()->availableGeometry();
        window->setPosition(work.center() - QPoint(window->width() / 2, window->height() / 2));
    }
    QObject::connect(window, &QQuickWindow::closing, controller, [] {
        if (g_window) {
            g_geometry = g_window->geometry();
            g_window->deleteLater();
            g_window = nullptr;
        }
    });
    window->show();
    window->requestActivate();
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    const DWORD previous = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const DWORD current = GetCurrentThreadId();
    const bool attached = previous && previous != current && AttachThreadInput(current, previous, TRUE);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);
    if (attached) AttachThreadInput(current, previous, FALSE);
    winshun::debug("ra9fael.qr: preview shown, " + std::to_string(spec.text.size()) + " bytes");
    return true;
}

std::string guiClipboardText()
{
    return QGuiApplication::clipboard()->text(QClipboard::Clipboard).toStdString();
}

bool guiPutImage(const Bitmap& bmp)
{
    QImage image(reinterpret_cast<const uchar*>(bmp.pixels.data()), bmp.width, bmp.height,
                 bmp.width * 4, QImage::Format_RGB32);
    image.detach();
    QGuiApplication::clipboard()->setImage(image, QClipboard::Clipboard);
    return QGuiApplication::clipboard()->image(QClipboard::Clipboard).convertToFormat(QImage::Format_RGB32) == image;
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

bool putImageOnClipboard(const Bitmap& bmp)
{
    if (QThread::currentThread() == qGuiApp->thread())
        return guiPutImage(bmp);
    bool ok = false;
    QMetaObject::invokeMethod(qGuiApp, [&] { ok = guiPutImage(bmp); }, Qt::BlockingQueuedConnection);
    return ok;
}

// %USERPROFILE%\Pictures (the manifest's files:write scope). Returns the
// full native path, or empty with `error` set.
std::string savePng(const Code& code, const Settings& st, bool zh, std::string& error)
{
    const QString pictures = qEnvironmentVariable("USERPROFILE") + "/Pictures";
    if (!QDir(pictures).exists() && !QDir().mkpath(pictures)) {
        error = zh ? "保存失败：Pictures 文件夹不可用" : "Save failed: the Pictures folder is unavailable";
        return {};
    }
    const QDateTime now = QDateTime::currentDateTime();
    const QDate d = now.date();
    const QTime t = now.time();
    const std::string name = saveFileName(d.year(), d.month(), d.day(), t.hour(), t.minute(),
                                          t.second(), [&pictures](const std::string& n) {
                                              return QFile::exists(pictures + "/"
                                                                   + QString::fromStdString(n));
                                          });
    if (name.empty()) {
        error = zh ? "保存失败：文件名冲突过多" : "Save failed: too many name collisions";
        return {};
    }
    const Bitmap bmp = renderImage(code, st);
    QImage image(reinterpret_cast<const uchar*>(bmp.pixels.data()), bmp.width, bmp.height,
                 bmp.width * 4, QImage::Format_RGB32);
    const QString file = pictures + "/" + QString::fromStdString(name);
    QFile output(file);
    if (image.isNull() || !output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        error = zh ? "保存失败：无法创建文件" : "Save failed: cannot create file";
        return {};
    }
    if (!image.save(&output, "PNG")) {
        output.close();
        output.remove();
        error = zh ? "保存失败：无法写入 " + pictures.toStdString()
                   : "Save failed: cannot write " + pictures.toStdString();
        return {};
    }
    return QDir::toNativeSeparators(file).toStdString();
}

void PreviewController::copyImage()
{
    const bool ok = putImageOnClipboard(renderImage(encode(m_text.toStdString(), m_settings.ecc), m_settings));
    m_feedback = ok ? (m_zh ? QStringLiteral("已复制图片") : QStringLiteral("Image copied"))
                    : (m_zh ? QStringLiteral("复制失败") : QStringLiteral("Copy failed"));
    emit feedbackChanged();
}

void PreviewController::saveImage()
{
    std::string error;
    const auto path = savePng(encode(m_text.toStdString(), m_settings.ecc), m_settings, m_zh, error);
    m_feedback = QString::fromStdString(path.empty() ? error : (m_zh ? "已保存 " : "Saved ") + path);
    emit feedbackChanged();
}

} // namespace ra9fael::qr
