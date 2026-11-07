// logic.h - the pure logic of the qr plugin: the Wi-Fi payload, the query
// classification, settings validation, image rendering into a plain pixel
// buffer and the save-file naming. No Qt, no Win32, so the unit tests can
// run the whole thing without a window server.
#ifndef RA9FAEL_LOGIC_H
#define RA9FAEL_LOGIC_H

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "qrcode.h"

namespace ra9fael::qr {

// ---- settings ----------------------------------------------------------------

// Validated settings; a bad value falls back to its default (spec §8).
struct Settings {
    Ecc ecc = Ecc::M;
    int moduleSize = 8;  // pixels per module, 2-20
    int quietZone = 4;   // light border modules, 0-8
    uint32_t dark = 0x000000;  // 0xRRGGBB
    uint32_t light = 0xFFFFFF; // 0xRRGGBB
};

inline bool isHexDigit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

inline int hexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// Strictly "#rgb" or "#rrggbb"; anything else fails and the caller keeps the
// previous value.
inline bool parseColor(const std::string& text, uint32_t& out)
{
    if (text.empty() || text[0] != '#')
        return false;
    if (text.size() == 4) {
        int v[3];
        for (int i = 0; i < 3; ++i) {
            v[i] = hexValue(text[1 + i]);
            if (v[i] < 0)
                return false;
        }
        out = (v[0] << 20u) | (v[0] << 16u) | (v[1] << 12u) | (v[1] << 8u) | (v[2] << 4u) | v[2];
        return true;
    }
    if (text.size() == 7) {
        uint32_t v = 0;
        for (int i = 1; i <= 6; ++i) {
            const int d = hexValue(text[i]);
            if (d < 0)
                return false;
            v = (v << 4) | static_cast<uint32_t>(d);
        }
        out = v;
        return true;
    }
    return false;
}

inline Ecc parseEcc(const std::string& text)
{
    if (text == "L" || text == "l")
        return Ecc::L;
    if (text == "Q" || text == "q")
        return Ecc::Q;
    if (text == "H" || text == "h")
        return Ecc::H;
    return Ecc::M; // "M" and anything else
}

inline int clampInt(int value, int lo, int hi, int fallback)
{
    if (value < lo || value > hi)
        return fallback;
    return value;
}

inline Settings applySettings(int moduleSize, int quietZone, const std::string& ecc,
                              const std::string& darkText, const std::string& lightText)
{
    Settings s;
    s.ecc = parseEcc(ecc);
    s.moduleSize = clampInt(moduleSize, 2, 20, 8);
    s.quietZone = clampInt(quietZone, 0, 8, 4);
    uint32_t dark = 0;
    uint32_t light = 0;
    if (parseColor(darkText, dark))
        s.dark = dark;
    if (parseColor(lightText, light))
        s.light = light;
    return s;
}

// ---- the Wi-Fi payload -------------------------------------------------------

// WIFI:T:WPA;S:<ssid>;P:<password>;; per the Wi-Fi QR convention: escape
// backslash, semicolon, comma, colon and double quote with a backslash. An
// empty password means an open network: T:nopass and no P segment. A password
// containing "wep" (case-insensitive) is written as T:WEP.
inline std::string escapeWifi(const std::string& text)
{
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"')
            out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

inline bool containsWep(const std::string& text)
{
    for (std::size_t i = 0; i + 3 <= text.size(); ++i) {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i + 1])));
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i + 2])));
        if (a == 'w' && b == 'e' && c == 'p')
            return true;
    }
    return false;
}

inline std::string wifiPayload(const std::string& ssid, const std::string& password)
{
    std::string out = "WIFI:T:";
    if (password.empty())
        out += "nopass";
    else if (containsWep(password))
        out += "WEP";
    else
        out += "WPA";
    out += ";S:";
    out += escapeWifi(ssid);
    if (!password.empty()) {
        out += ";P:";
        out += escapeWifi(password);
    }
    out += ";;";
    return out;
}

// "SSID 密码" splits at the first space; the password may itself contain
// spaces or be missing.
inline bool splitWifiInput(const std::string& input, std::string& ssid, std::string& password)
{
    const auto space = input.find(' ');
    if (space == std::string::npos) {
        ssid = input;
        password.clear();
    } else {
        ssid = input.substr(0, space);
        password = input.substr(space + 1);
    }
    return !ssid.empty();
}

// ---- classification ----------------------------------------------------------

inline bool looksLikeUrl(const std::string& text)
{
    const auto sep = text.find("://");
    if (sep == std::string::npos || sep == 0)
        return false;
    if (text.find(' ') != std::string::npos && text.find(' ') < sep)
        return false; // a space before the scheme separator: not a URL
    for (std::size_t i = 0; i < sep; ++i) {
        const char c = text[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                        || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
        if (!ok)
            return false;
    }
    return true;
}

enum class Kind { Url, Wifi, Text };

inline Kind classify(const std::string& text)
{
    if (text.rfind("WIFI:", 0) == 0)
        return Kind::Wifi;
    if (looksLikeUrl(text))
        return Kind::Url;
    return Kind::Text;
}

// zh/en aware kind label (the caller picks by locale).
inline const char* kindLabel(Kind kind, bool zh)
{
    switch (kind) {
    case Kind::Url: return zh ? "链接" : "URL";
    case Kind::Wifi: return zh ? "Wi-Fi" : "Wi-Fi";
    case Kind::Text: return zh ? "文本" : "text";
    }
    return "";
}

// ---- rendering ---------------------------------------------------------------

// A plain RGB image, one uint32_t per pixel as 0xAARRGGBB (alpha 0xFF), so the
// unit tests can check pixels and the plugin can hand it to QImage with one
// copy. The quiet zone is part of the bitmap.
struct Bitmap {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels; // width * height, 0xAARRGGBB
};

inline Bitmap renderImage(const Code& code, const Settings& settings)
{
    Bitmap bmp;
    if (!code.ok || code.size <= 0)
        return bmp;
    const int modules = code.size + 2 * settings.quietZone;
    const int px = modules * settings.moduleSize;
    bmp.width = px;
    bmp.height = px;
    bmp.pixels.assign(static_cast<std::size_t>(px) * px,
                      0xFF000000u | settings.light);

    const auto darkPixel = 0xFF000000u | settings.dark;
    for (int row = 0; row < code.size; ++row)
        for (int col = 0; col < code.size; ++col) {
            if (code.rows[static_cast<std::size_t>(row) * (code.size + 1) + col] != '1')
                continue;
            const int y0 = (row + settings.quietZone) * settings.moduleSize;
            const int x0 = (col + settings.quietZone) * settings.moduleSize;
            for (int dy = 0; dy < settings.moduleSize; ++dy) {
                auto* line = &bmp.pixels[static_cast<std::size_t>(y0 + dy) * px + x0];
                std::fill(line, line + settings.moduleSize, darkPixel);
            }
        }
    return bmp;
}

// ---- save paths --------------------------------------------------------------

// qr-<yyyymmdd>-<hhmmss>.png in the given directory; when that name is taken,
// -2, -3, ... is appended, never overwriting (spec §5).
inline std::string saveFileName(int year, int month, int day, int hour, int minute, int second,
                                const std::function<bool(const std::string&)>& exists)
{
    char base[64];
    std::snprintf(base, sizeof(base), "qr-%04d%02d%02d-%02d%02d%02d.png", year, month, day,
                  hour, minute, second);
    std::string name(base);
    int counter = 0;
    while (exists(name)) {
        ++counter;
        std::snprintf(base, sizeof(base), "qr-%04d%02d%02d-%02d%02d%02d-%d.png", year, month, day,
                      hour, minute, second, counter);
        name = base;
        if (counter >= 1000)
            return std::string(); // give up rather than loop forever
    }
    return name;
}

} // namespace ra9fael::qr

#endif // RA9FAEL_LOGIC_H
