// The 2D drawing of the menus (see menu_draw.hpp): the ordering table's chains, the primitives, OpenDash's tags and the
// builders several menus share.
#include "game/menu_draw.hpp"

#include <algorithm>
#include <cstdlib>

#include "engine/data_source.hpp"
#include "game/display.hpp"
#include "game/game_state.hpp"
#include "game/language_pack.hpp"
#include "game/list_cursor.hpp"
#include "game/menu_banks.hpp"
#include "game/message_state.hpp"

namespace od::port {

namespace {

int gExtra = 0; // the widescreen margin (screenExtra)

// the PC font's ASCII metrics (exe table 0x8b187c: {u8 left offset, s8 width} by byte)
struct AsciiGlyphMetric : DataRecord {
    uint8_t left = 0;
    int8_t width = 0;
    static AsciiGlyphMetric load(const TableReader& r) {
        AsciiGlyphMetric rec;
        rec.left = r.u8(0);
        rec.width = r.s8(1);
        return rec;
    }
};
const DataTable<RecordTable<AsciiGlyphMetric, 0x100, 2>> asciiMetrics{TableId::MenuDrawAsciiMetrics};
constexpr uint32_t kAsciiSpaceWidth = 4;

// the backdrop's tiles: 21 x 16 of 16 px from the sheet's 4 x 4 cells at (0x30, 0x20), clut 0x7f92, tpage 0xd
constexpr uint16_t kBackdropMode = 0x0d;
constexpr uint32_t kBackdropClut = 0x7f92;
constexpr int kBackdropColumns = 0x15, kBackdropRows = 0x10, kBackdropTile = 0x10;
// the scroll bar's art (tpage 0xd, clut 0x7f53)
constexpr uint16_t kScrollBarMode = 0x0d;
constexpr uint32_t kScrollBarTopCap = 0x7f5380f0, kScrollBarBottomCap = 0x7f5398f0, kScrollBarUpArrow = 0x7f5380f8,
                   kScrollBarDownArrow = 0x7f5388f8;
constexpr uint16_t kScrollBarClut = 0x7f53;

struct OtChain {
    uint32_t slot;
    std::vector<ScreenPrim> p;
};
std::vector<OtChain> gOt;
std::vector<ScreenPrim>* gSink = nullptr;
constexpr uint16_t kModeKind = 0xffff; // a draw-mode packet in a chain (ScreenPrim::code), resolved by the flush
// the tags of the next primitives
uint16_t gArt = 0, gArtSeq = 0, gVec = 0;
uint16_t gLabel = 0, gLabelArg = 0;
uint32_t gLabelRef = 0;
int16_t gRow = -1;

} // namespace

void setScreenExtra(int px) { gExtra = px < 0 ? 0 : px; }
int screenExtra() { return gExtra; }

void loadMenuBackdropDraw(std::vector<ScreenPrim>* draw) {
    if (!draw) return;
    menu_draw::begin(draw);
    menu_draw::backdropDraw(0);
    menu_draw::end();
}

namespace menu_draw {

void begin(std::vector<ScreenPrim>* draw) { gSink = draw; }
void discard() { gOt.clear(); }
bool drawing() { return gSink != nullptr; }

void end() {
    if (gSink) {
        std::vector<uint32_t> slots;
        for (const OtChain& c : gOt)
            if (std::find(slots.begin(), slots.end(), c.slot) == slots.end()) slots.push_back(c.slot);
        std::sort(slots.rbegin(), slots.rend());
        uint16_t mode = 0;
        for (const uint32_t s : slots)
            for (size_t i = gOt.size(); i-- > 0;) {
                if (gOt[i].slot != s) continue;
                for (ScreenPrim p : gOt[i].p) {
                    if (p.kind == ScreenPrim::Clip && p.code == kModeKind) {
                        mode = p.tpage;
                        continue;
                    }
                    if (p.kind == ScreenPrim::Sprite || p.kind == ScreenPrim::Flat) p.tpage = mode;
                    gSink->push_back(p);
                }
            }
    }
    gOt.clear();
    gSink = nullptr;
}

size_t otOpen(uint32_t slot) {
    gOt.push_back({slot, {}});
    return gOt.size() - 1;
}

ScreenPrim& otPut(size_t c, ScreenPrim::Kind k) {
    gOt[c].p.emplace_back();
    ScreenPrim& p = gOt[c].p.back();
    p.kind = k;
    p.art = gArt;
    p.vec = gVec;
    p.label = gLabel;
    p.labelArg = gLabelArg;
    p.labelRef = gLabelRef;
    p.row = gRow;
    gLabel = gLabelArg = 0;
    gLabelRef = 0;
    gRow = -1;
    return p;
}

ScreenPrim& otLast(size_t c) { return gOt[c].p.back(); }

void otDrawMode(size_t c, uint16_t word) {
    ScreenPrim& p = otPut(c, ScreenPrim::Clip);
    p.code = kModeKind;
    p.tpage = word;
}

void otSprite(size_t c, int x, int y, uint32_t uv, int w, int h, uint32_t col, uint8_t code) {
    ScreenPrim& p = otPut(c, ScreenPrim::Sprite);
    p.x = int16_t(x);
    p.y = int16_t(y);
    p.w = int16_t(w);
    p.h = int16_t(h);
    p.u = uint8_t(uv);
    p.v = uint8_t(uv >> 8);
    p.clut = uint16_t(uv >> 16);
    p.colour = col & 0xffffff;
    p.semi = (code & 2) != 0;
    p.raw = (code & 1) != 0;
    p.half = (code & 0xfc) == kSpriteHalf;
}

void otTile(size_t c, int x, int y, int w, int h, uint32_t col, uint8_t code) {
    ScreenPrim& p = otPut(c, ScreenPrim::Flat);
    p.x = int16_t(x);
    p.y = int16_t(y);
    p.w = int16_t(w);
    p.h = int16_t(h);
    p.colour = col & 0xffffff;
    p.semi = (code & 2) != 0;
}

void otQuad(size_t c, Q4 x, Q4 y, uint32_t col, uint8_t code, Q4 uu, Q4 vv, uint16_t clut, uint16_t tpage) {
    ScreenPrim& p = otPut(c, ScreenPrim::Quad);
    for (int k = 0; k < 4; ++k) {
        p.qx[k] = int16_t(x.a[k]);
        p.qy[k] = int16_t(y.a[k]);
        p.qu[k] = uint8_t(uu.a[k]);
        p.qv[k] = uint8_t(vv.a[k]);
    }
    p.colour = col & 0xffffff;
    p.textured = (code & 4) != 0;
    p.semi = (code & 2) != 0;
    p.raw = (code & 1) != 0;
    p.clut = clut;
    p.tpage = tpage;
}

void otGlyph(size_t c, int x, int y, uint16_t code, uint32_t col, uint8_t font, uint8_t size) {
    ScreenPrim& p = otPut(c, ScreenPrim::Glyph);
    p.x = int16_t(x);
    p.y = int16_t(y);
    p.gx = float(x);
    p.gy = float(y);
    p.code = code;
    p.colour = col & 0xffffff;
    p.font = font;
    p.size = size;
}

void otClip(size_t c, int x, int y, int w, int h) {
    ScreenPrim& p = otPut(c, ScreenPrim::Clip);
    p.x = int16_t(x);
    p.y = int16_t(y > 0xff ? y - 0x100 : y);
    p.w = int16_t(w);
    p.h = int16_t(h < 0 ? 0 : h);
}

void otClipDrawArea(size_t c) {
    const DrawArea area = displayDrawArea();
    otClip(c, area.x, area.y, area.w, area.h);
}

void otHLine(size_t c, int x0, int x1, int y) {
    otTile(c, std::min(x0, x1), y, std::abs(x1 - x0) + 1, 1, kNeutral, kTileOpaque);
}
void otVLine(size_t c, int x, int y0, int y1) {
    otTile(c, x, std::min(y0, y1), 1, std::abs(y1 - y0) + 1, kNeutral, kTileOpaque);
}

void otHitRow(size_t c, int x, int y, int w, int h, int row) {
    ScreenPrim& p = otPut(c, ScreenPrim::Flat);
    p.x = int16_t(x);
    p.y = int16_t(y);
    p.w = int16_t(w);
    p.h = int16_t(h);
    p.row = int16_t(row);
    p.hitOnly = true;
    p.art = p.vec = 0;
    p.label = 0;
}

void artBegin(uint16_t vec) {
    if (++gArtSeq == 0) gArtSeq = 1;
    gArt = gArtSeq;
    gVec = vec;
}
void artEnd() { gArt = gVec = 0; }
void labelNext(uint16_t label, uint16_t arg, uint32_t ref) {
    gLabel = label;
    gLabelArg = arg;
    gLabelRef = ref;
}
void rowNext(int row) { gRow = int16_t(row); }

void bankTextDraw(uint32_t packed, uint32_t bank, int16_t id, uint32_t mode) {
    const MessageBank messages(bank);
    if (!gSink || !messages.entryMapped(id)) return;
    const TextBytes text(messages.message(id));
    uint32_t p = 0;
    int n = 0;
    for (int guard = 0; guard < 512 && text.mappedAt(p, 4); ++guard) { // the glyph count (ends at ff / fb 1f)
        const uint8_t c = text.byte(p);
        if (c < 0xfb) {
            ++n;
            p += c > 0xcf ? 2 : 1;
        } else if (c == 0xfb) {
            if (text.byte(p + 1) == 0x1f) break;
            p += text.byte(p + 1) == 0x2e ? 3 : 2;
        } else if (c == 0xfe) {
            ++n;
            const uint32_t q = p + (text.byte(p + 1) > 0xcf ? 2 : 1);
            p = q + text.byte(q + 1) + 3;
        } else {
            break;
        }
    }
    int x = int(packed & 0x3ff);
    switch (mode & 0xf) {
    case kTextSpread: x = x - 1 + int(int8_t(((10 - n) * 0xc) / (n + 1))); break;
    case kTextRight: x = x - n * 0xc + 0x77; break;
    case kTextCentred: x = x - 1 + (((10 - n) * 0xc) >> 1); break;
    default: break;
    }
    const size_t c = otOpen(kOtText);
    ScreenPrim& t = otPut(c, ScreenPrim::Text);
    t.x = int16_t(x & 0x3ff);
    t.y = int16_t((packed >> 10) & 0x1ff);
    t.ax = int16_t(packed & 0x3ff);
    t.align = uint8_t(mode & 0xf);
    t.palette = uint8_t((packed >> 19) & 7);
    t.colour = (packed & kTextDimBit) ? kDim : kNeutral;
    t.ruby = (mode & kTextNoRuby) == 0 && gameOptions().ruby != 0;
    t.bank = bank;
    t.id = id;
    const MenuBanks exe = exeMenuBanks();
    const MenuBanks& b = menuBanks();
    t.lang = (bank == b.names && bank != exe.names) || (bank == b.descs && bank != exe.descs) ||
             (bank == b.areas && bank != exe.areas) ||
             (b.shopPrices != exe.shopPrices && bank >= b.shopPrices &&
              bank < b.shopPrices + kShopPriceStrings * kShopPriceStringSize) ||
             (bank == stateAddress(TableId::StatusItemsCountText) && ps1Text()); // the item count, PS1-encoded by the items page
}

void asciiTextDraw(const std::string& text, int x, int y, bool proportional) {
    float fx = float(x);
    for (const char ch : text) {
        const uint8_t c = uint8_t(ch);
        if (c == 0x20) {
            fx += float(kAsciiSpaceWidth);
            continue;
        }
        if (gSink) {
            const size_t k = otOpen(kOtOverlay);
            ScreenPrim& p = otPut(k, ScreenPrim::Glyph);
            p.gx = fx;
            p.gy = float(y);
            p.x = int16_t(fx);
            p.y = int16_t(y);
            p.code = c;
            p.size = 0x18;
            p.u = proportional ? uint8_t(asciiMetrics[c].left) : 0;
        }
        fx += proportional ? float(int(asciiMetrics[c].width)) * 0.5f + 1.0f : 12.0f;
    }
}

uint32_t asciiTextWidth(const std::string& text) {
    uint32_t w = 0;
    for (const char c : text) w += c == ' ' ? kAsciiSpaceWidth : uint32_t(int(asciiMetrics[uint8_t(c)].width) + 1);
    return w >> 1;
}

void backdropDraw(int switching) {
    uint8_t t = uint8_t(gameState().frame);
    if (display().pace == kTitleDisplayPace) t >>= 1;
    const size_t c = otOpen(switching < 1 ? kOtBackdrop : kOtOverlay);
    if (gExtra > 0) otClip(c, -gExtra, 0, kScreenWidth + 2 * gExtra, kScreenHeight);
    otDrawMode(c, kBackdropMode);
    const int ph = ((t & 0x3f) >> 4) * 0xf, start = int(t & 0xf) - kBackdropTile;
    const int more = (gExtra + 15) / 16;
    for (int row = 0; row < kBackdropRows; ++row)
        for (int col = -more; col < kBackdropColumns + more; ++col)
            otSprite(c, start + col * kBackdropTile, start + row * kBackdropTile,
                     spriteUv(kBackdropClut, uint32_t(((ph + col) & 3) * 0x10 + 0x30),
                              uint32_t(((ph + row) & 3) * 0x10 + 0x20)),
                     kBackdropTile, kBackdropTile, kNeutral, kSprite16);
}

void listScrollBarDraw(const ListCursor& list, int x, int y) {
    const uint8_t rows = list.rows, vis = list.visibleRows;
    const int len = int(vis) * 0x10 - 0x18;
    const size_t c = otOpen(kOtText);
    otDrawMode(c, kScrollBarMode);
    otSprite(c, x, y, kScrollBarTopCap, 8, 0x10);
    const int top = y + 0x10, bot = len + y + 0x10;
    otQuad(c, {x, x + 8, x, x + 8}, {top, top, bot, bot}, kNeutral, kPolyFT4, {0xf0, 0xf8, 0xf0, 0xf8},
           {0x90, 0x90, 0x91, 0x91}, kScrollBarClut, kScrollBarMode);
    otSprite(c, x, bot, kScrollBarBottomCap, 8, 0x10);
    const int mv = int(list.scrollStep) + int(list.rowStep);
    if (mv != 0) {
        if (mv < 0) otSprite(c, x, y + 8, kScrollBarUpArrow, 8, 8);
        else otSprite(c, x, bot, kScrollBarDownArrow, 8, 8);
    }
    int ty = y + 0x10, tl = 0;
    if (rows != 0) {
        tl = len;
        if (vis < rows) {
            ty += (int(int8_t(list.firstRow)) * len) / int(rows);
            tl = (int(vis) * len - 1 + int(rows)) / int(rows);
        }
    }
    otQuad(c, {x, x + 7, x, x + 7}, {ty, ty, ty + tl, ty + tl}, kNeutral, kPolyFT4, {0xf8, 0xff, 0xf8, 0xff},
           {0x90, 0x90, 0x91, 0x91}, kScrollBarClut, kScrollBarMode);
}

uint32_t barPulse() {
    const uint8_t ph = uint8_t(uint8_t(gameState().frame) >> 1);
    return uint8_t(uint8_t((ph & 0x1f) < 0x11 ? ph * 8 : ph * 0xf8) + 0x7f);
}

} // namespace menu_draw

} // namespace od::port
