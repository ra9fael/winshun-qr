// Qt image/clipboard actions and the same native preview used by Wi-Fi.
#include "QrPreview.h"
#include "../qr_preview_window.h"
#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QThread>
#include <QTimer>

#include <memory>
#include <mutex>
#include <vector>

namespace ra9fael::qr {
namespace {
// One preview per action: a new one is a new window, and the earlier ones stay
// open. Each window closes on its own thread, so the list is swept when they do.
std::mutex g_mutex;
std::vector<std::unique_ptr<qrpreview::QrPreview>> g_previews;
QTimer* g_reaper = nullptr; // the GUI thread only

// Joins what ended and frees what it held. True while a window is still up.
bool sweepPreviews()
{
    std::lock_guard lock(g_mutex);
    g_previews.erase(std::remove_if(g_previews.begin(), g_previews.end(),
                         [](const std::unique_ptr<qrpreview::QrPreview>& preview) {
                             return preview->window() == nullptr;
                         }),
        g_previews.end());
    return !g_previews.empty();
}

// The RPC thread has no further call to come after a window closes itself, and
// the GUI thread is idle then: its timer does the sweeping while previews exist.
void ensureReaper()
{
    QMetaObject::invokeMethod(
        qGuiApp,
        [] {
            if (!g_reaper) {
                g_reaper = new QTimer(qGuiApp);
                g_reaper->setInterval(500);
                QObject::connect(g_reaper, &QTimer::timeout, qGuiApp, [] {
                    if (!sweepPreviews())
                        g_reaper->stop();
                });
            }
            if (!g_reaper->isActive())
                g_reaper->start();
        },
        Qt::QueuedConnection);
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
    const auto code = encode(spec.text, settings.ecc);
    if (!code.ok) { error = "Failed to encode preview"; return false; }
    const auto image = renderImage(code, settings);
    qrpreview::Bitmap snapshot {image.width, image.pixels, code.size + 2 * settings.quietZone};
    auto preview = std::make_unique<qrpreview::QrPreview>();
    if (!preview->show(std::move(snapshot), spec.text, spec.zh, L"QR", L"qr")) {
        error = "Failed to open preview";
        return false;
    }
    {
        std::lock_guard lock(g_mutex);
        g_previews.push_back(std::move(preview));
    }
    ensureReaper();
    return true;
}
void closePreview()
{
    std::vector<std::unique_ptr<qrpreview::QrPreview>> closing;
    {
        std::lock_guard lock(g_mutex);
        closing.swap(g_previews);
    }
    for (auto& preview : closing)
        preview->close();
}
bool previewIsOpen() { return sweepPreviews(); }

std::string clipboardText()
{
    if (QThread::currentThread() == qGuiApp->thread())
        return QGuiApplication::clipboard()->text().toStdString();
    std::string text;
    QMetaObject::invokeMethod(qGuiApp, [&] { text = QGuiApplication::clipboard()->text().toStdString(); }, Qt::BlockingQueuedConnection);
    return text;
}
bool putImageOnClipboard(const Bitmap& bmp)
{
    if (QThread::currentThread() == qGuiApp->thread()) return guiPutImage(bmp);
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


} // namespace ra9fael::qr
