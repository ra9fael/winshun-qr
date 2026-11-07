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

// The same payload with the authentication token already decided: a saved
// profile's XML says what the network really is, so nothing has to be guessed
// from the passphrase.
inline std::string wifiPayloadWith(const std::string& ssid, const std::string& password,
                                   const std::string& token)
{
    std::string out = "WIFI:T:" + token + ";S:" + escapeWifi(ssid);
    if (!password.empty() && token != "nopass") {
        out += ";P:";
        out += escapeWifi(password);
    }
    out += ";;";
    return out;
}

inline std::string wifiPayload(const std::string& ssid, const std::string& password)
{
    if (password.empty())
        return wifiPayloadWith(ssid, "", "nopass");
    return wifiPayloadWith(ssid, password, containsWep(password) ? "WEP" : "WPA");
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

// ---- saved Wi-Fi profiles ----------------------------------------------------
//
// Windows keeps every network the user has joined as a profile; its XML names
// the SSID, the authentication and (for a personal network) the passphrase.
// native.h collects the XMLs, the rules below decide what a QR code can carry,
// so the unit tests need no wireless adapter.

struct WifiProfile {
    std::string ssid;
    std::string authText; // raw <authentication>: WPA2PSK, WPA3SAE, open, …
    std::string key;      // plaintext keyMaterial; empty when protected
    bool keyPresent = false;   // a <sharedKey> section exists
    bool keyProtected = false; // the key came back as an encrypted blob
    bool oneX = false;         // 802.1X: no shared passphrase at all
    bool connected = false;    // this SSID is the current connection
};

// The text of the first <tag>…</tag>, entities decoded; empty when the tag is
// absent or self-closing. The element name must match whole, so <name> does
// not match <nameX>; xmlns attributes are ignored.
inline std::string xmlDecode(const std::string& text);

inline void appendXmlUtf8(std::string& out, unsigned cp)
{
    if (cp < 0x80)
        out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

inline bool xmlHexDigits(const std::string& digits, bool hex)
{
    if (digits.empty())
        return false;
    for (const char c : digits) {
        const bool ok = hex ? ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
                            : (c >= '0' && c <= '9');
        if (!ok)
            return false;
    }
    return true;
}

inline std::string xmlDecode(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out += text[i];
            continue;
        }
        const std::size_t semi = text.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 10) { // a bare &, not an entity
            out += text[i];
            continue;
        }
        const std::string name = text.substr(i + 1, semi - i - 1);
        bool consumed = true;
        if (name == "amp")
            out += '&';
        else if (name == "lt")
            out += '<';
        else if (name == "gt")
            out += '>';
        else if (name == "quot")
            out += '"';
        else if (name == "apos")
            out += '\'';
        else if (name.size() > 1 && name[0] == '#') {
            const bool hex = name[1] == 'x' || name[1] == 'X';
            const std::string digits = name.substr(hex ? 2 : 1);
            if (xmlHexDigits(digits, hex)) {
                unsigned long cp = 0;
                try {
                    cp = std::stoul(digits, nullptr, hex ? 16 : 10);
                } catch (...) {
                    consumed = false;
                }
                if (consumed)
                    appendXmlUtf8(out, static_cast<unsigned>(cp));
            } else
                consumed = false;
        } else
            consumed = false;
        if (!consumed) {
            out += text[i];
            continue;
        }
        i = semi;
    }
    return out;
}

inline std::string xmlText(const std::string& xml, const std::string& tag)
{
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    for (std::size_t at = xml.find('<'); at != std::string::npos; at = xml.find('<', at + 1)) {
        if (at + 1 + tag.size() >= xml.size())
            return {};
        if (xml.compare(at + 1, tag.size(), tag) != 0)
            continue;
        const char after = xml[at + 1 + tag.size()];
        if (!(after == '>' || isSpace(after) || after == '/'))
            continue;
        const std::size_t open = xml.find('>', at);
        if (open == std::string::npos)
            return {};
        if (xml[open - 1] == '/')
            return {}; // <tag/>
        const std::size_t close = xml.find("</" + tag, open + 1);
        if (close == std::string::npos)
            return {};
        return xmlDecode(xml.substr(open + 1, close - open - 1));
    }
    return {};
}

// <hex>4D794E6574</hex> to its bytes; an odd length or a non-hex character
// yields "".
inline std::string hexToBytes(const std::string& hex)
{
    if (hex.empty() || hex.size() % 2 != 0)
        return {};
    std::string out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        int value = 0;
        for (int k = 0; k < 2; ++k) {
            const char c = hex[i + k];
            value <<= 4;
            if (c >= '0' && c <= '9')
                value |= c - '0';
            else if (c >= 'a' && c <= 'f')
                value |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                value |= c - 'A' + 10;
            else
                return {};
        }
        out += static_cast<char>(value);
    }
    return out;
}

// A profile XML to the fields a QR code needs. The SSID element carries either
// <name> or <hex>; the document's first <name> is the profile's own name, so
// read inside <SSID> for the network's.
inline WifiProfile parseWifiProfile(const std::string& xml)
{
    WifiProfile p;
    p.authText = xmlText(xml, "authentication");
    const std::string oneX = xmlText(xml, "useOneX");
    p.oneX = oneX == "true" || oneX == "1";
    p.keyPresent = xml.find("<sharedKey") != std::string::npos;
    p.key = xmlText(xml, "keyMaterial");
    const std::string prot = xmlText(xml, "protected");
    p.keyProtected = prot == "true" || prot == "1";
    if (p.keyProtected)
        p.key.clear(); // the blob is not a passphrase

    const std::size_t at = xml.find("<SSID>");
    if (at != std::string::npos) {
        const std::size_t end = xml.find("</SSID>", at);
        const std::string block = end == std::string::npos ? xml.substr(at) : xml.substr(at, end - at);
        p.ssid = xmlText(block, "name");
        if (p.ssid.empty())
            p.ssid = hexToBytes(xmlText(block, "hex"));
    }
    return p;
}

inline std::string upperText(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text)
        out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// The payload's T value. WPA/WPA2/WPA3 personal all read as WPA (the Wi-Fi QR
// convention has no WPA3 token); 802.1X has no shared passphrase at all.
inline std::string wifiAuthToken(const std::string& authText, bool oneX)
{
    const std::string t = upperText(authText);
    if (oneX || t.find("802.1X") != std::string::npos || t.find("EAP") != std::string::npos
        || t.find("ENT") != std::string::npos)
        return "enterprise";
    if (t.find("WEP") != std::string::npos)
        return "WEP";
    if (t.find("SAE") != std::string::npos || t.find("WPA") != std::string::npos)
        return "WPA";
    return "nopass"; // open / none / empty
}

inline std::string wifiAuthToken(const WifiProfile& p)
{
    return wifiAuthToken(p.authText, p.oneX);
}

// What a QR code can do with this profile.
enum class WifiShare { Ready, Open, NeedsKey, Enterprise };

inline WifiShare wifiShare(const WifiProfile& p)
{
    const std::string token = wifiAuthToken(p);
    if (token == "enterprise")
        return WifiShare::Enterprise;
    if (token == "nopass")
        return WifiShare::Open;
    if (p.key.empty() || p.keyProtected)
        return WifiShare::NeedsKey;
    return WifiShare::Ready;
}

// The payload for a shareable profile; empty when there is nothing to share.
inline std::string wifiPayloadFor(const WifiProfile& p)
{
    switch (wifiShare(p)) {
    case WifiShare::Ready: return wifiPayloadWith(p.ssid, p.key, wifiAuthToken(p));
    case WifiShare::Open: return wifiPayloadWith(p.ssid, "", "nopass");
    case WifiShare::NeedsKey:
    case WifiShare::Enterprise: break;
    }
    return {};
}

// Subtitle note for a row: what the code carries, or why it cannot be made.
inline std::string wifiShareNote(WifiShare share, bool zh)
{
    switch (share) {
    case WifiShare::Ready: return zh ? "含密码" : "with key";
    case WifiShare::Open: return zh ? "开放网络" : "open";
    case WifiShare::NeedsKey: return zh ? "读不到密码，无法生成" : "the key is unreadable";
    case WifiShare::Enterprise: return zh ? "企业网络（802.1X）没有共享密码" : "enterprise (802.1X) has no shared key";
    }
    return {};
}

// Case-insensitive SSID substring; an empty filter matches everything.
inline bool ssidMatches(const std::string& ssid, const std::string& filter)
{
    if (filter.empty())
        return true;
    return upperText(ssid).find(upperText(filter)) != std::string::npos;
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
