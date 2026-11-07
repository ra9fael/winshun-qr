// main.cpp - ra9fael.qr's protocol mapping, living in QrApp.dll.
//
// The `qr` command turns the query text (or the clipboard text when the query
// is empty) into a QR image; `qrwifi` lists the Wi-Fi networks this machine has
// joined — read from their saved profiles, passphrase included — and falls back
// to "SSID password" for a network Windows does not know yet. The image itself
// is produced on demand: query() only reports what would be encoded (type,
// version, ECC, bytes), and the actions copy the image, preview it or save it
// as a PNG.
//
// Pure logic lives in qrcode.h (the encoder) and logic.h (payload, profile XML,
// settings, rendering, save names); native.h reads the WLAN profiles. The
// preview window lives in the QML app (qmlapp/QrPreview); clipboard access and
// image handoff marshal to the GUI thread through the same wrappers.

#include "native.h" // first: winsock2.h has to precede windows.h

#include "logic.h"
#include "qrcode.h"
#include "qmlapp/QrPreview.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QThread>

#include <cstdio>
#include <string>

namespace ra9fael::qr {
namespace {

const char* eccName(Ecc level)
{
    switch (level) {
    case Ecc::L: return "L";
    case Ecc::M: return "M";
    case Ecc::Q: return "Q";
    case Ecc::H: return "H";
    }
    return "M";
}

// A UTF-8-safe cut for the title: never splits a code point.
std::string truncate(const std::string& text, std::size_t maxBytes)
{
    if (text.size() <= maxBytes)
        return text;
    std::size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
        --cut;
    return text.substr(0, cut) + "…";
}

Settings settingsNow(const winshun::Plugin& plugin)
{
    const winshun::Json& s = plugin.settings();
    return applySettings(static_cast<int>(s["moduleSize"].toInt(8)),
                         static_cast<int>(s["quietZone"].toInt(4)), s["ecc"].toString(),
                         s["darkColor"].toString(), s["lightColor"].toString());
}

winshun::Item hintRow(const std::string& title, bool invalid = false,
                      const std::string& subtitle = std::string())
{
    winshun::Item row("hint", title, subtitle);
    row.withIcon("color:#E8A33D").withScore(50);
    if (invalid)
        row.invalid();
    return row;
}

// "WPA" / "WEP" / "802.1X" / "开放", the short form the saved-network rows
// show next to the SSID.
std::string wifiAuthLabel(const WifiProfile& profile, bool zh)
{
    const std::string token = wifiAuthToken(profile);
    if (token == "WPA")
        return "WPA";
    if (token == "WEP")
        return "WEP";
    if (token == "enterprise")
        return "802.1X";
    return zh ? "开放" : "open";
}

std::string joinNote(const std::vector<std::string>& parts)
{
    std::string out;
    for (const std::string& part : parts) {
        if (part.empty())
            continue;
        if (!out.empty())
            out += " · ";
        out += part;
    }
    return out;
}

} // namespace

class QrPlugin : public winshun::Plugin {
public:
    std::string name() const override { return "ra9fael.qr"; }

    winshun::Items query(const winshun::Query& q) override
    {
        const bool zh = chinese();
        const Settings st = settingsNow(*this);

        if (q.command == "qrwifi")
            return wifiQuery(q.text, zh, st);

        std::string payload = q.text;
        if (payload.empty()) {
            payload = clipboardText();
            if (payload.empty())
                return {hintRow(zh ? "剪贴板没有文本" : "The clipboard holds no text")};
        }

        const Code code = encode(payload, st.ecc);
        if (!code.ok) {
            char text[128];
            std::snprintf(text, sizeof(text),
                          zh ? "太长：当前纠错级别最长 %d 字节" : "Too long: %d bytes max at this ECC",
                          capacityBytes(st.ecc));
            return {hintRow(text, true)};
        }
        return {resultRow(payload, q.text, q.command, code, zh, std::string())};
    }

    winshun::Effects action(const winshun::ActionRequest& a) override
    {
        const bool zh = chinese();
        std::string payload = a.item["data"]["payload"].toString();
        const std::string command = a.item["data"]["command"].toString();
        const std::string text = a.item["data"]["text"].toString();
        // A saved-network row carries only its SSID: the passphrase stays out of
        // the item (and so out of the host's memory) and is read again here.
        const std::string savedSsid = a.item["data"]["wifi"].toString();
        if (!savedSsid.empty()) {
            payload.clear();
            std::string why;
            const WifiCollection wifi = savedWifiProfiles();
            for (const WifiProfile& profile : wifi.profiles) {
                if (profile.ssid != savedSsid)
                    continue;
                payload = wifiPayloadFor(profile);
                if (payload.empty()) // the same reasons the row itself cannot share
                    why = wifiShare(profile) == WifiShare::Enterprise
                        ? (zh ? "企业网络（802.1X）没有可共享的密码"
                              : "An enterprise (802.1X) network has no shared key")
                        : wifiKeyNote(profile, zh);
                break;
            }
            if (payload.empty())
                return {winshun::effect::notify(why.empty()
                    ? (zh ? "该网络已不在保存的 Wi-Fi 里" : "The network is no longer among the saved ones")
                    : why)};
        }
        if (payload.empty())
            return {winshun::effect::notify(zh ? "先运行一次查询，再执行动作" : "Run a query before an action")};

        const Settings st = settingsNow(*this);
        const Code code = encode(payload, st.ecc);
        if (!code.ok)
            return {winshun::effect::notify(zh ? "二维码生成失败" : "Failed to build the QR code")};

        if (a.action == "copyImage") {
            const Bitmap bmp = renderImage(code, st);
            putImageOnClipboard(bmp);
            // The row says so; no toast (spec §3).
            return {winshun::effect::replace({resultRow(payload, text, command, code, zh, zh ? "已复制" : "copied")})};
        }
        if (a.action == "preview") {
            PreviewSpec spec;
            spec.text = payload;
            spec.zh = zh;
            std::string error;
            if (!showPreview(spec, st, error))
                return {winshun::effect::notify(error.empty() ? "预览窗口打开失败" : error)};
            return {winshun::effect::hide()};
        }
        if (a.action == "savePng") {
            std::string error;
            const std::string path = savePng(code, st, error);
            if (path.empty())
                return {winshun::effect::notify(error.empty() ? "保存失败" : error)};
            return {winshun::effect::replace({resultRow(payload, text, command, code, zh,
                                                      (zh ? "已保存 " : "saved ") + path)})};
        }
        return {};
    }

    void shuttingDown() override { closePreview(); }

private:
    // One row per Wi-Fi network this machine has joined, filtered by what was
    // typed. Rows for networks Windows cannot share (802.1X, an unreadable key)
    // are still shown, marked invalid, so the list says why they are missing
    // from the codes. With nothing matched the query falls back to the manual
    // "SSID password" form.
    winshun::Items wifiQuery(const std::string& input, bool zh, const Settings& st)
    {
        const std::string filter = winshun::trim(input);
        const WifiCollection wifi = savedWifiProfiles();

        winshun::Items rows;
        for (const WifiProfile& profile : wifi.profiles) {
            if (profile.ssid.empty() || !ssidMatches(profile.ssid, filter))
                continue;
            rows.push_back(savedWifiRow(profile, zh, st));
        }
        if (!rows.empty())
            return rows;

        std::string ssid;
        std::string password;
        if (splitWifiInput(filter, ssid, password)) {
            const std::string payload = wifiPayload(ssid, password);
            const Code code = encode(payload, st.ecc);
            if (!code.ok) {
                char text[128];
                std::snprintf(text, sizeof(text),
                              zh ? "太长：当前纠错级别最长 %d 字节" : "Too long: %d bytes max at this ECC",
                              capacityBytes(st.ecc));
                return {hintRow(text, true)};
            }
            return {resultRow(payload, filter, "qrwifi", code, zh, std::string())};
        }

        // Nothing typed and nothing to list: say why the list is empty.
        if (wifi.noAdapter)
            return {hintRow(zh ? "没有无线网卡" : "No wireless adapter",
                            false, zh ? "插上无线网卡或启用 WLAN 电台" : "Plug in an adapter or turn the WLAN radio on")};
        if (wifi.serviceDown)
            return {hintRow(zh ? "WLAN AutoConfig 服务未运行" : "The WLAN AutoConfig service is not running",
                            false, zh ? "在服务里启动 WlanSvc" : "Start WlanSvc in Services")};
        if (!wifi.problem.empty())
            return {hintRow(zh ? "读不到已保存的 Wi-Fi" : "Cannot read the saved Wi-Fi", true, wifi.problem)};
        return {hintRow(zh ? "没有已保存的 Wi-Fi" : "No saved Wi-Fi", false,
                        zh ? "先连接一次，或在后面输入 SSID 密码" : "Join one once, or type an SSID and password")};
    }

    // A saved network's row. The item carries only the SSID — the passphrase is
    // read again when an action runs, so it never sits in the host's item. The
    // id is unique per SSID: the host keeps the first row of a duplicated id
    // (Merge.cpp), which is how a three-network list once became one row.
    static winshun::Item savedWifiRow(const WifiProfile& profile, bool zh, const Settings& st)
    {
        const WifiShare share = wifiShare(profile);
        std::vector<std::string> parts {"Wi-Fi"};
        if (!profile.readFailed)
            parts.push_back(wifiAuthLabel(profile, zh)); // a failed read knows no auth either
        if (profile.connected)
            parts.push_back(zh ? "已连接" : "connected");
        parts.push_back(wifiKeyNote(profile, zh));

        if (share == WifiShare::Ready || share == WifiShare::Open) {
            const std::string payload = wifiPayloadFor(profile);
            const Code code = encode(payload, st.ecc);
            if (code.ok) {
                char tail[80];
                std::snprintf(tail, sizeof(tail), zh ? " · v%d · %s · %zu 字节" : " · v%d · %s · %zu B",
                              code.version, eccName(code.level), payload.size());
                winshun::Item row(wifiItemId(profile.ssid), profile.ssid, joinNote(parts) + tail);
                row.withData(winshun::Json::Object {{"command", "qrwifi"},
                                                    {"text", profile.ssid},
                                                    {"wifi", profile.ssid}});
                row.withActions({{"copyImage", zh ? "复制图片" : "Copy image", "Enter"},
                                 {"preview", zh ? "预览" : "Preview", "Ctrl+Enter"},
                                 {"savePng", zh ? "保存 PNG" : "Save PNG", "Shift+Enter"}});
                return row;
            }
        }

        // Not shareable yet. The reason is in `parts`; the actions stay, since
        // they read the profile again — the host may run elevated by then, or
        // the profile have been re-saved per-user — and say why not otherwise.
        // An enterprise network has no shared key for any run to find, so its
        // row gets none.
        winshun::Item row(wifiItemId(profile.ssid), profile.ssid, joinNote(parts));
        row.withIcon("color:#E8A33D").withScore(50).invalid();
        if (share != WifiShare::Enterprise)
            row.withActions({{"copyImage", zh ? "复制图片" : "Copy image", "Enter"},
                             {"preview", zh ? "预览" : "Preview", "Ctrl+Enter"},
                             {"savePng", zh ? "保存 PNG" : "Save PNG", "Shift+Enter"}});
        return row;
    }

    // The one result row: title is the content, subtitle the type, version,
    // ECC and byte count — with `extra` (已复制 / the saved path) appended.
    static winshun::Item resultRow(const std::string& payload, const std::string& text,
                                   const std::string& command, const Code& code, bool zh,
                                   const std::string& extra)
    {
        const std::string shown = text.empty() ? payload : text;
        std::string subtitle = std::string(kindLabel(classify(payload), zh));
        char tail[80];
        std::snprintf(tail, sizeof(tail), zh ? " · v%d · %s · %zu 字节" : " · v%d · %s · %zu B",
                      code.version, eccName(code.level), payload.size());
        subtitle += tail;
        if (!extra.empty())
            subtitle += " · " + extra;

        winshun::Item row("code", truncate(shown, 60), subtitle);
        row.withData(winshun::Json::Object {{"payload", payload},
                                            {"command", command},
                                            {"text", text}});
        row.withActions({{"copyImage", zh ? "复制图片" : "Copy image", "Enter"},
                         {"preview", zh ? "预览" : "Preview", "Ctrl+Enter"},
                         {"savePng", zh ? "保存 PNG" : "Save PNG", "Shift+Enter"}});
        return row;
    }

    // %USERPROFILE%\Pictures (the manifest's files:write scope). Returns the
    // full native path, or empty with `error` set.
    std::string savePng(const Code& code, const Settings& st, std::string& error)
    {
        const bool zh = chinese();
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
        if (image.isNull() || !image.save(file, "PNG")) {
            error = zh ? "保存失败：无法写入 " + pictures.toStdString()
                       : "Save failed: cannot write " + pictures.toStdString();
            return {};
        }
        return QDir::toNativeSeparators(file).toStdString();
    }
};

// The plugin protocol loop (qmlapp/qmlmain.cpp starts it on the worker thread).
int runServeLoop()
{
    QrPlugin plugin;
    return plugin.serve();
}

} // namespace ra9fael::qr
