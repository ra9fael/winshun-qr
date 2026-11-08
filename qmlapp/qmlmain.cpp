// Entries of QrApp.dll. Qt Core/Gui own application and clipboard services;
// the protocol and native preview use separate threads. The standalone
// --preview entry exits when its native window closes.
#include "QrPreview.h"

#include "../winshun.h"

#include <QGuiApplication>
#include "../qt_window_icon.h"
#include <QTimer>

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <string>
#include <thread>

using namespace Qt::StringLiterals;

namespace {

// The GUI thread's setup, shared by both entries.
void prepareGui(const QString& appDir)
{
    QCoreApplication::setLibraryPaths({appDir}); // platforms\qwindows.dll: WinShun's
    qputenv("QT_QPA_PLATFORM", "windows:fontengine=gdi");
    QCoreApplication::setOrganizationName(u"WinShun"_s);
    QCoreApplication::setApplicationName(u"WinShun"_s);
}

void startGui()
{
    qunsetenv("QT_QPA_PLATFORM");
    setPluginWindowIcon();
}

} // namespace

// The plugin: WinShun starts qr.exe, the stub loads this and calls here.
extern "C" __declspec(dllexport) int winshunQrMain(const wchar_t* appDirName)
{
    const QString appDir = QString::fromWCharArray(appDirName);
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    prepareGui(appDir);
    int argc = 1;
    char name[] = "QrPlugin";
    char* argv[] = {name, nullptr};
    QGuiApplication app(argc, argv);
    startGui();
    QGuiApplication::setQuitOnLastWindowClosed(false); // the preview closes, not quits

    // The plugin protocol on a worker thread: serve() blocks on stdin, the GUI
    // event loop runs here. serve() returning (shutdown, stdin closed) is the
    // process's end.
    int rc = 0;
    std::thread serve([&] {
        rc = ra9fael::qr::runServeLoop();
        QMetaObject::invokeMethod(&app, &QCoreApplication::quit, Qt::QueuedConnection);
    });
    app.exec();
    serve.join();
    return rc;
}

// The standalone check: one preview window, no WinShun. --text names the
// payload, --clipboard takes it from the clipboard.
extern "C" __declspec(dllexport) int winshunQrPreview(const wchar_t* appDirName)
{
    const QString appDir = QString::fromWCharArray(appDirName);
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    QStringList args;
    int count = 0;
    if (LPWSTR* wide = ::CommandLineToArgvW(::GetCommandLineW(), &count)) {
        for (int i = 1; i < count; ++i)
            args.push_back(QString::fromWCharArray(wide[i]));
        ::LocalFree(wide);
    }
    prepareGui(appDir);
    int argc = 1;
    char name[] = "QrPreview";
    char* argv[] = {name, nullptr};
    QGuiApplication app(argc, argv);
    startGui();
    QGuiApplication::setQuitOnLastWindowClosed(true); // the preview ends the run

    ra9fael::qr::PreviewSpec spec;
    spec.zh = !args.contains(u"--en"_s);
    bool fromClipboard = false;
    for (int i = 0; i < args.size(); ++i) {
        const QString& arg = args.at(i);
        if (arg == u"--text" && i + 1 < args.size())
            spec.text = args.at(++i).toStdString();
        else if (arg == u"--clipboard")
            fromClipboard = true;
    }
    if (fromClipboard)
        spec.text = ra9fael::qr::clipboardText(); // the GUI thread: no marshaling
    if (spec.text.empty()) {
        std::fprintf(stderr, "qr: --text <payload> or --clipboard is required\n");
        std::fflush(stderr);
        return 2;
    }
    std::string error;
    if (!ra9fael::qr::showPreview(spec, ra9fael::qr::Settings {}, error)) {
        std::fprintf(stderr, "qr: %s\n", error.c_str());
        std::fflush(stderr);
        return 2;
    }
    QTimer lifetime;
    QObject::connect(&lifetime, &QTimer::timeout, &app, [&] {
        if (!ra9fael::qr::previewIsOpen()) app.quit();
    });
    lifetime.start(50);
    return app.exec();
}
