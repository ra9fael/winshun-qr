// Resident-thread API for Qt image actions and the shared native preview.
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

// Show one native preview, preserving session geometry when replacing it.
bool showPreview(const PreviewSpec& spec, const Settings& settings, std::string& error);

// Closes and destroys the preview window, if any. Safe from any thread.
void closePreview();
bool previewIsOpen();

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
