// main.cpp - text/clipboard QR protocol mapping; Wi-Fi sharing lives in ra9fael.wifi.

#include "winshun.h"
#include "logic.h"
#include "qrcode.h"
#include "qmlapp/QrPreview.h"

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

} // namespace

class QrPlugin : public winshun::Plugin {
public:
    std::string name() const override { return "ra9fael.qr"; }

    winshun::Items query(const winshun::Query& q) override
    {
        const bool zh = chinese();
        const Settings st = settingsNow(*this);

        if (q.command != "qr")
            return {};

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
        if (a.command != "qr")
            return {};
        const bool zh = chinese();
        std::string payload = a.item["data"]["payload"].toString();
        const std::string command = a.item["data"]["command"].toString();
        const std::string text = a.item["data"]["text"].toString();
        if (payload.empty())
            return {winshun::effect::notify(zh ? "先运行一次查询，再执行动作" : "Run a query before an action")};

        const Settings st = settingsNow(*this);
        const Code code = encode(payload, st.ecc);
        if (!code.ok)
            return {winshun::effect::notify(zh ? "二维码生成失败" : "Failed to build the QR code")};

        if (a.action == "copyImage") {
            const Bitmap bmp = renderImage(code, st);
            if (!putImageOnClipboard(bmp))
                return {winshun::effect::notify(zh ? "复制图片失败" : "Failed to copy image")};
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
            const std::string path = savePng(code, st, zh, error);
            if (path.empty())
                return {winshun::effect::notify(error.empty() ? "保存失败" : error)};
            return {winshun::effect::replace({resultRow(payload, text, command, code, zh,
                                                      (zh ? "已保存 " : "saved ") + path)})};
        }
        return {};
    }

    void shuttingDown() override { closePreview(); }

private:
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


};

// The plugin protocol loop (qmlapp/qmlmain.cpp starts it on the worker thread).
int runServeLoop()
{
    QrPlugin plugin;
    return plugin.serve();
}

} // namespace ra9fael::qr
