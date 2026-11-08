// QrPreview.h - the preview window's serve-thread API, the counterpart of
// main.cpp's mapping. The window lifecycle (destruction, creation, screens)
// is QrPreview.cpp, all on the GUI thread; these calls marshal over from the
// serve() thread.
#ifndef RA9FAEL_QRPREVIEW_H
#define RA9FAEL_QRPREVIEW_H

#include <string>

#include "logic.h"

namespace ra9fael::qr {

// What the preview shows: the encoded payload and the interface language.
struct PreviewSpec {
    std::string text;
    bool zh = true;
};

// Shows the payload as a QR image in a small always-on-top window. Any
// earlier preview is closed and destroyed first (spec §4: a destroyed HWND
// before the next window is built, never two instances). False with an error.
bool showPreview(const PreviewSpec& spec, const Settings& settings, std::string& error);

// Closes and destroys the preview window, if any. Safe from any thread.
void closePreview();

// The clipboard's text (empty when there is none), from any thread.
std::string clipboardText();

// Puts the rendered image on the clipboard, from any thread.
bool putImageOnClipboard(const Bitmap& bmp);

// Save the original image atomically under the manifest's Pictures scope.
std::string savePng(const Code& code, const Settings& settings, bool zh, std::string& error);

// The JSON-RPC serve loop; qmlmain.cpp runs it on the worker thread.
int runServeLoop();

} // namespace ra9fael::qr
#endif // RA9FAEL_QRPREVIEW_H
