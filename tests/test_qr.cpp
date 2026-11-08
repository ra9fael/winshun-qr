// test_qr.cpp - the pure logic of ra9fael.qr: the QR encoder (qrcode.h) and
// the plugin logic (logic.h). No Qt, no windows. The matrix of the known
// vector (the stopcode URL, the same one bosskey's encoder was verified
// against) can be dumped with QR_DUMP=<file> for the independent decode
// check by tools/verify/qr_check.py.
#include "logic.h"
#include "qrcode.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

void expectEq(const std::string& got, const std::string& want, const char* what)
{
    if (got != want) {
        std::printf("FAIL: %s: got '%s', want '%s'\n", what, got.c_str(), want.c_str());
        ++g_failures;
    }
}

char cellAt(const ra9fael::qr::Code& code, int row, int col)
{
    return code.rows[static_cast<std::size_t>(row) * (code.size + 1) + col];
}

// ---- the QR encoder ----------------------------------------------------------

// The format word's 15 bits, read back from copy 1 (qrcode.h's layout).
int formatBitCopy1(const ra9fael::qr::Code& code, int i)
{
    int row;
    int col;
    if (i <= 5) {
        row = i;
        col = 8;
    } else if (i == 6) {
        row = 7;
        col = 8;
    } else if (i == 7) {
        row = 8;
        col = 8;
    } else if (i == 8) {
        row = 8;
        col = 7;
    } else {
        row = 8;
        col = 14 - i;
    }
    return cellAt(code, row, col) == '1' ? 1 : 0;
}

// The same bit from copy 2, the split around the other two finders.
int formatBitCopy2(const ra9fael::qr::Code& code, int i)
{
    if (i <= 7)
        return cellAt(code, 8, code.size - 1 - i) == '1' ? 1 : 0;
    return cellAt(code, code.size - 15 + i, 8) == '1' ? 1 : 0;
}

void testEncoderStructure()
{
    using namespace ra9fael::qr;

    const std::string url = "https://www.windows.com/stopcode";
    const Code code = encode(url, Ecc::L);
    expect(code.ok, "qr: the stopcode URL fits");
    // 32 bytes fill version 2 level L exactly (bosskey's fixed-v3 encoder
    // padded it up; this one picks the smallest version).
    expect(code.version == 2, "qr: the stopcode URL is version 2");
    expect(code.size == 25, "qr: version 2 is 25 modules");
    expect(code.level == Ecc::L, "qr: the level is reported back");
    expect(code.rows.size() == 25 * 26 - 1, "qr: 25 rows of 25 joined by newlines");

    // The three finder patterns: a 7x7 ring around a 3x3 core, with white
    // separators outside. These are what makes it read as a QR code.
    for (const auto& [top, left] :
         {std::pair {0, 0}, std::pair {0, code.size - 7}, std::pair {code.size - 7, 0}}) {
        expect(cellAt(code, top + 0, left + 0) == '1', "qr: finder outer ring");
        expect(cellAt(code, top + 1, left + 1) == '0', "qr: finder white ring");
        expect(cellAt(code, top + 2, left + 2) == '1', "qr: finder core");
        expect(cellAt(code, top + 3, left + 3) == '1', "qr: finder core filled");
        expect(cellAt(code, top + 6, left + 6) == '1', "qr: finder outer corner");
    }
    // The separators sit on the inner sides of the three corners.
    expect(cellAt(code, 7, 7) == '0', "qr: finder separator, top left");
    expect(cellAt(code, 7, code.size - 8) == '0', "qr: finder separator, top right");
    expect(cellAt(code, code.size - 8, 7) == '0', "qr: finder separator, bottom left");

    // The timing patterns alternate on row 6 and column 6.
    expect(cellAt(code, 6, 8) == '1', "qr: timing starts dark");
    expect(cellAt(code, 6, 9) == '0', "qr: timing alternates");
    expect(cellAt(code, 8, 6) == '1', "qr: vertical timing");
    expect(cellAt(code, 9, 6) == '0', "qr: vertical timing alternates");

    // The alignment pattern (version 2's single one, at size-7) and the dark
    // module.
    expect(cellAt(code, code.size - 7, code.size - 7) == '1', "qr: alignment centre");
    expect(cellAt(code, code.size - 9, code.size - 9) == '1', "qr: alignment outer ring");
    expect(cellAt(code, code.size - 8, code.size - 8) == '0', "qr: alignment white ring");
    expect(cellAt(code, code.size - 8, 8) == '1', "qr: dark module");

    // Deterministic, and different input gives a different matrix.
    const Code again = encode(url, Ecc::L);
    expect(again.rows == code.rows, "qr: deterministic");
    expect(encode("https://www.windows.com/", Ecc::L).rows != code.rows,
           "qr: input changes the code");

    // The empty string is legal.
    expect(encode("", Ecc::M).ok, "qr: empty text encodes");
}

void testFormatAndVersion()
{
    using namespace ra9fael::qr;

    // The two copies of the format word agree bit by bit, the 5 data bits say
    // L (01), the mask is in range, and the BCH(15,5) check part checks out —
    // all independent of which mask was chosen.
    const Code code = encode("https://www.windows.com/stopcode", Ecc::L);
    int bits = 0;
    for (int i = 14; i >= 0; --i)
        bits = (bits << 1) | formatBitCopy1(code, i);
    bool agree = true;
    for (int i = 0; i < 15; ++i)
        agree = agree && formatBitCopy1(code, i) == formatBitCopy2(code, i);
    expect(agree, "format: the two copies agree");
    const int data = bits ^ 0x5412;
    expect(((data >> 13) & 3) == 1, "format: the level bits say L (01)");
    const int mask = (data >> 10) & 7;
    expect(mask >= 0 && mask <= 7, "format: the mask is in range");
    int rem = data >> 10;
    for (int i = 0; i < 10; ++i)
        rem = (rem << 1) ^ (((rem >> 9) & 1) ? 0x537 : 0);
    expect(rem == (data & 0x3FF), "format: the BCH check part checks out");

    // The same format check on other levels: the level bits change, the rest
    // of the machinery holds.
    for (const auto& [level, levelBits] :
         {std::pair {Ecc::M, 0}, std::pair {Ecc::Q, 3}, std::pair {Ecc::H, 2}}) {
        const Code c = encode("https://www.windows.com/stopcode", level);
        int b = 0;
        for (int i = 14; i >= 0; --i)
            b = (b << 1) | formatBitCopy1(c, i);
        const int d = b ^ 0x5412;
        expect(((d >> 13) & 3) == levelBits, "format: the level bits match the settings");
        int r = d >> 10;
        for (int i = 0; i < 10; ++i)
            r = (r << 1) ^ (((r >> 9) & 1) ? 0x537 : 0);
        expect(r == (d & 0x3FF), "format: the BCH check part checks out (other levels)");
    }

    // Version 7 and up carry the 18-bit version word in both corners; the
    // bits are recomputed here from the generator polynomial (0x1F25).
    const Code v7 = encode(std::string(150, 'x'), Ecc::L); // v7-L holds 155
    expect(v7.ok && v7.version == 7, "qr: 150 bytes at L is version 7");
    expect(v7.size == 45, "qr: version 7 is 45 modules");
    int verBits = 7;
    int vrem = 7;
    for (int i = 0; i < 12; ++i)
        vrem = (vrem << 1) ^ (((vrem >> 11) & 1) ? 0x1F25 : 0);
    verBits = (7 << 12) | vrem;
    bool versionOk = true;
    for (int i = 0; i < 18; ++i) {
        const int bit = (verBits >> i) & 1;
        const char drawn = cellAt(v7, v7.size - 11 + i % 3, i / 3);
        const char drawnT = cellAt(v7, i / 3, v7.size - 11 + i % 3);
        versionOk = versionOk && drawn == (bit ? '1' : '0') && drawnT == (bit ? '1' : '0');
    }
    expect(versionOk, "version: the 18-bit word sits in both corners");
}

void testCapacityAndSizes()
{
    using namespace ra9fael::qr;

    // The four capacity ceilings of version 40, the numbers every source
    // agrees on — a strong check that the block/ECC tables are right.
    expect(capacityBytes(Ecc::L) == 2953, "capacity: v40 L is 2953");
    expect(capacityBytes(Ecc::M) == 2331, "capacity: v40 M is 2331");
    expect(capacityBytes(Ecc::Q) == 1663, "capacity: v40 Q is 1663");
    expect(capacityBytes(Ecc::H) == 1273, "capacity: v40 H is 1273");

    // Version-1 byte capacities per level.
    expect(versionForBytes(17, Ecc::L) == 1, "capacity: 17 bytes at L is v1");
    expect(versionForBytes(18, Ecc::L) == 2, "capacity: 18 bytes at L is v2");
    expect(versionForBytes(14, Ecc::M) == 1, "capacity: 14 bytes at M is v1");
    expect(versionForBytes(15, Ecc::M) == 2, "capacity: 15 bytes at M is v2");
    expect(versionForBytes(11, Ecc::Q) == 1, "capacity: 11 bytes at Q is v1");
    expect(versionForBytes(7, Ecc::H) == 1, "capacity: 7 bytes at H is v1");
    expect(versionForBytes(8, Ecc::H) == 2, "capacity: 8 bytes at H is v2");

    // The 8-bit to 16-bit length-counter switch sits between v9 and v10:
    // v9-L holds 230 bytes, v10-L holds 271 (the standard capacity table).
    expect(versionForBytes(230, Ecc::L) == 9, "capacity: 230 bytes at L is v9");
    expect(versionForBytes(231, Ecc::L) == 10, "capacity: 231 bytes at L is v10");
    expect(versionForBytes(271, Ecc::L) == 10, "capacity: 271 bytes at L is v10");
    expect(versionForBytes(272, Ecc::L) == 11, "capacity: 272 bytes at L is v11");

    // Walking the payload length 1..cap(L) the version climbs 1..40 one step
    // at a time: every version is reachable, none is skipped.
    int seen = 0;
    for (int n = 1; n <= capacityBytes(Ecc::L); ++n) {
        const int got = versionForBytes(static_cast<std::size_t>(n), Ecc::L);
        expect(got == seen || got == seen + 1, "capacity: versions climb one at a time");
        seen = got;
    }
    expect(seen == 40, "capacity: the walk reaches v40");
    for (const Ecc level : {Ecc::L, Ecc::M, Ecc::Q, Ecc::H}) {
        for (const int bytes : {1, 100, 1000}) {
            if (bytes > capacityBytes(level))
                continue;
            const Code code = encode(std::string(bytes, 'x'), level);
            const int version = versionForBytes(static_cast<std::size_t>(bytes), level);
            expect(code.ok && code.version == version && code.size == 17 + 4 * version,
                   "capacity: size matches version");
            expect(code.rows.size() == static_cast<std::size_t>(code.size) * (code.size + 1) - 1,
                   "capacity: rows well formed");
        }
    }

    // The exact ceiling encodes; one byte over is refused.
    for (const Ecc level : {Ecc::L, Ecc::M, Ecc::Q, Ecc::H}) {
        const int cap = capacityBytes(level);
        expect(encode(std::string(cap, 'a'), level).ok, "capacity: the ceiling encodes");
        expect(!encode(std::string(cap + 1, 'a'), level).ok, "capacity: one over is refused");
    }

    // v40 at L really is 177 modules.
    const Code big = encode(std::string(2953, 'a'), Ecc::L);
    expect(big.size == 177, "capacity: v40 is 177 modules");
}

void testSettings()
{
    using namespace ra9fael::qr;

    const Settings def = applySettings(8, 4, "M", "#000000", "#ffffff");
    expect(def.moduleSize == 8 && def.quietZone == 4 && def.ecc == Ecc::M
               && def.dark == 0x000000 && def.light == 0xFFFFFF,
           "settings: the defaults survive validation");

    // Out of range falls back to the default, not clamped (spec §8).
    expect(applySettings(1, 4, "M", "", "").moduleSize == 8, "settings: moduleSize 1 -> default");
    expect(applySettings(21, 4, "M", "", "").moduleSize == 8, "settings: moduleSize 21 -> default");
    expect(applySettings(2, 4, "M", "", "").moduleSize == 2, "settings: moduleSize 2 stays");
    expect(applySettings(20, 4, "M", "", "").moduleSize == 20, "settings: moduleSize 20 stays");
    expect(applySettings(8, -1, "M", "", "").quietZone == 4, "settings: quietZone -1 -> default");
    expect(applySettings(8, 9, "M", "", "").quietZone == 4, "settings: quietZone 9 -> default");
    expect(applySettings(8, 0, "M", "", "").quietZone == 0, "settings: quietZone 0 stays");
    expect(applySettings(8, 8, "M", "", "").quietZone == 8, "settings: quietZone 8 stays");

    // The ECC choice is case-tolerant, anything else is M.
    expect(applySettings(8, 4, "L", "", "").ecc == Ecc::L, "settings: L");
    expect(applySettings(8, 4, "q", "", "").ecc == Ecc::Q, "settings: lowercase q");
    expect(applySettings(8, 4, "H", "", "").ecc == Ecc::H, "settings: H");
    expect(applySettings(8, 4, "M", "", "").ecc == Ecc::M, "settings: M");
    expect(applySettings(8, 4, "", "", "").ecc == Ecc::M, "settings: empty -> M");
    expect(applySettings(8, 4, "high", "", "").ecc == Ecc::M, "settings: junk -> M");

    // Colors: strict #rgb / #rrggbb, junk keeps the default.
    const uint32_t dark = applySettings(8, 4, "M", "#123456", "").dark;
    expect(dark == 0x123456, "settings: #rrggbb parsed");
    expect(applySettings(8, 4, "M", "#fff", "").dark == 0xFFFFFF, "settings: #rgb expanded");
    expect(applySettings(8, 4, "M", "#1f3", "").dark == 0x11FF33, "settings: #rgb expanded exactly");
    expect(applySettings(8, 4, "M", "red", "").dark == 0x000000, "settings: junk color -> default");
    expect(applySettings(8, 4, "M", "#12345", "").dark == 0x000000, "settings: 5 digits -> default");
    expect(applySettings(8, 4, "M", "#12g456", "").dark == 0x000000, "settings: bad digit -> default");
    expect(applySettings(8, 4, "M", "#1234", "").dark == 0x000000, "settings: 4 digits -> default");
    expect(applySettings(8, 4, "M", "", "#0f0").light == 0x00FF00, "settings: light parsed");
    expect(applySettings(8, 4, "M", "", "nope").light == 0xFFFFFF,
           "settings: junk light -> default");
}

// ---- save names --------------------------------------------------------------

void testSaveNames()
{
    using namespace ra9fael::qr;

    const auto free_ = [](const std::string&) { return false; };
    expectEq(saveFileName(2026, 11, 8, 9, 5, 3, free_), "qr-20261108-090503.png",
             "save: the plain timestamp name");

    // The first collision gets -1, the second -2; nothing is ever overwritten.
    std::set<std::string> taken;
    const auto exists = [&taken](const std::string& n) { return taken.count(n) > 0; };
    taken.insert("qr-20261108-090503.png");
    expectEq(saveFileName(2026, 11, 8, 9, 5, 3, exists), "qr-20261108-090503-1.png",
             "save: the first collision takes -1");
    taken.insert("qr-20261108-090503-1.png");
    expectEq(saveFileName(2026, 11, 8, 9, 5, 3, exists), "qr-20261108-090503-2.png",
             "save: the second collision takes -2");

    // Everything taken: the name gives up instead of looping forever.
    const auto always = [](const std::string&) { return true; };
    expect(saveFileName(2026, 11, 8, 9, 5, 3, always).empty(), "save: all taken gives up");

    // Zero-padded, month and day included.
    expectEq(saveFileName(2026, 1, 2, 3, 4, 5, free_), "qr-20260102-030405.png",
             "save: zero padding");
}

// ---- classification ----------------------------------------------------------

void testClassify()
{
    using namespace ra9fael::qr;

    expect(looksLikeUrl("https://x.com"), "url: https");
    expect(looksLikeUrl("http://a.b/c?d=e&f=g"), "url: http with query");
    expect(looksLikeUrl("ftp://host/file.txt"), "url: ftp");
    expect(looksLikeUrl("mailto+custom-scheme://h"), "url: scheme characters");
    expect(!looksLikeUrl("www.example.com"), "url: no scheme");
    expect(!looksLikeUrl(" https://x.com"), "url: leading space");
    expect(!looksLikeUrl("ht tp://x"), "url: space inside scheme");
    expect(!looksLikeUrl("://x"), "url: empty scheme");
    expect(!looksLikeUrl("中文://x"), "url: non-ascii scheme");
    expect(!looksLikeUrl(""), "url: empty text");

    expect(classify("WIFI:T:WPA;S:x;;") == Kind::Text, "classify: arbitrary payload stays text");
    expect(classify("https://a.b") == Kind::Url, "classify: url");
    expect(classify("hello 世界") == Kind::Text, "classify: text");

    expectEq(kindLabel(Kind::Url, true), "链接", "label: zh url");
    expectEq(kindLabel(Kind::Url, false), "URL", "label: en url");
    expectEq(kindLabel(Kind::Text, true), "文本", "label: zh text");
    expectEq(kindLabel(Kind::Text, false), "text", "label: en text");
}

// ---- rendering ---------------------------------------------------------------

void testRender()
{
    using namespace ra9fael::qr;

    const Code code = encode("hi", Ecc::M); // v1: 21 modules
    expect(code.size == 21, "render: v1 is 21 modules");

    const Settings st = applySettings(3, 2, "M", "#102030", "#f0e0d0");
    const Bitmap bmp = renderImage(code, st);
    expect(bmp.width == (21 + 2 * 2) * 3, "render: width counts the quiet zone");
    expect(bmp.height == bmp.width, "render: the bitmap is square");
    expect(bmp.pixels.size() == static_cast<std::size_t>(bmp.width) * bmp.width,
           "render: every pixel present");

    // Corners are the light quiet zone; the first module (a finder corner) is
    // dark from the module grid offset onward.
    const uint32_t light = 0xFF000000u | 0xF0E0D0;
    const uint32_t dark = 0xFF000000u | 0x102030;
    expect(bmp.pixels[0] == light, "render: top-left pixel is light");
    expect(bmp.pixels[bmp.pixels.size() - 1] == light, "render: bottom-right pixel is light");
    const int quiet = 2 * 3; // 2 modules of quiet zone at 3 px
    expect(bmp.pixels[static_cast<std::size_t>(quiet) * bmp.width + quiet] == dark,
           "render: the finder corner module is dark");
    expect(bmp.pixels[static_cast<std::size_t>(quiet - 1) * bmp.width + quiet - 1] == light,
           "render: the pixel just outside the grid is light");
    // A whole module block is solid: 3x3 of the same color.
    bool solid = true;
    for (int dy = 0; dy < 3; ++dy)
        for (int dx = 0; dx < 3; ++dx)
            solid = solid
                && bmp.pixels[static_cast<std::size_t>(quiet + dy) * bmp.width + quiet + dx] == dark;
    expect(solid, "render: a module fills its square");

    // Zero quiet zone: no border at all.
    const Settings tight = applySettings(4, 0, "M", "#000000", "#ffffff");
    expect(renderImage(code, tight).width == 21 * 4, "render: quietZone 0 drops the border");

    // A bad code renders nothing rather than crashing.
    Code broken;
    broken.ok = false;
    expect(renderImage(broken, st).pixels.empty(), "render: an invalid code is empty");
}

} // namespace

int main()
{
    testEncoderStructure();
    testFormatAndVersion();
    testCapacityAndSizes();
    testSettings();
    testSaveNames();
    testClassify();
    testRender();

    // The known vector's matrix can be handed to the independent decode check
    // (tools/verify/qr_check.py): QR_DUMP=<file> qr_tests
    if (const char* path = std::getenv("QR_DUMP")) {
        const ra9fael::qr::Code code = ra9fael::qr::encode("https://www.windows.com/stopcode",
                                                           ra9fael::qr::Ecc::L);
        if (FILE* f = std::fopen(path, "wb")) {
            std::fwrite(code.rows.data(), 1, code.rows.size(), f);
            std::fclose(f);
            std::printf("qr: matrix written to %s\n", path);
        }
    }

    if (g_failures == 0) {
        std::printf("all qr logic tests passed\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
