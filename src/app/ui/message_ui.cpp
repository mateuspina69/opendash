#include "app/ui/message_ui.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <exception>
#include <stdexcept>
#include <system_error>

#include "app/ui/prim_painter.hpp"
#include "engine/dat.hpp"
#include "engine/data_source.hpp"
#include "engine/lang.hpp"
#include "game/menu_art.hpp"
#include "game/message.hpp"
#include "game/game_state.hpp"
#include "game/message_state.hpp"
#include "game/save_slots.hpp"

#include "game/language_pack.hpp"
#include "game/text_format.hpp"
#include "game/text_script.hpp"
#include "game/window_frame.hpp"

namespace od::app {

namespace {
const DataTable<ScalarTable<int8_t, 16>> handBob{TableId::MessageHandBob}; // the choice hand's bob (exe 0x8b1f3c)
} // namespace

uint32_t MessageUi::nameAt(uint32_t id) {
    return od::port::MessageBank(od::port::nameBank()).message(int32_t(uint16_t(id - 0x380)));
}

float MessageUi::ps1Advance(uint8_t c) const { return ps1.advance(c); }

MessageUi::Ps1Font MessageUi::loadFont(od::Renderer& r, const od::lang::LangPack& pack, bool allColours) {
    Ps1Font f;
    try {
        const auto page = pack.file("font.bin");
        const auto w = pack.file("widths.bin");
        if (page.size() < 0x8000 || w.size() < 256) throw std::runtime_error("font.bin / widths.bin too small");
        std::copy_n(w.begin(), 256, f.widths.begin());
        if (w.size() > 256) f.limit = w[256];
        constexpr int kUp = 4;
        auto opt = [&](const char* name, size_t need) {
            std::vector<uint8_t> v;
            if (pack.has(name)) v = pack.file(name);
            if (v.size() < need) v.clear();
            return v;
        };
        const std::vector<uint8_t> colours = opt("colours.bin", 8 * 32), buttons = opt("buttons.bin", 4 * 32);
        f.buttons = !buttons.empty();
        auto rgba = [](uint32_t c) -> uint32_t { // PS1 BGR555, 0 = transparent (the glyph sprites are opaque)
            if (c == 0) return 0x00ffffffu;
            return 0xff000000u | ((c >> 10) & 31) * 255 / 31 << 16 | ((c >> 5) & 31) * 255 / 31 << 8 | (c & 31) * 255 / 31;
        };
        auto clutAt = [](const std::vector<uint8_t>& t, int k, int n) {
            return uint32_t(t[size_t(k * 32 + n * 2)]) | uint32_t(t[size_t(k * 32 + n * 2 + 1)]) << 8;
        };
        // colour 7's CLUT is all transparent (no texture: its text is not drawn, as on the PS1)
        const int n = colours.empty() ? 1 : (allColours ? 7 : 1);
        std::vector<uint32_t> px(size_t(256 * kUp) * size_t(256 * kUp));
        for (int ci = 0; ci < n; ++ci) {
            for (int y = 0; y < 256 * kUp; ++y)
                for (int x = 0; x < 256 * kUp; ++x) {
                    const int sx = x / kUp, sy = y / kUp;
                    const uint8_t b = page[size_t(sy) * 128 + size_t(sx) / 2];
                    const int nib = (sx & 1) ? (b >> 4) : (b & 15);
                    const int g = sx < 240 ? (sy / 12) * 20 + sx / 12 : -1;
                    uint32_t c;
                    if (f.buttons && Ps1Font::button(g)) c = clutAt(buttons, g - 0x51, nib);
                    else if (!colours.empty()) c = clutAt(colours, ci, nib);
                    else c = (nib & 1) ? 0x7fffu : 0u; // no CLUTs in the pack: plane 0 in white
                    px[size_t(y) * size_t(256 * kUp) + size_t(x)] = rgba(c);
                }
            f.col[size_t(ci)] = r.createTexture(256 * kUp, 256 * kUp, px.data());
        }
        f.tex = f.col[0];
        for (int g = 0; g < 256; ++g) { // the glyph texels (CLUT entry 1) of cell g
            int lo = 99, hi = -1;
            for (int y = 0; y < 12 && g < 21 * 20; ++y)
                for (int x = 0; x < 12; ++x) {
                    const int sx = (g % 20) * 12 + x, sy = (g / 20) * 12 + y;
                    const uint8_t b = page[size_t(sy) * 128 + size_t(sx) / 2];
                    if ((((sx & 1) ? (b >> 4) : b) & 3) != 1) continue;
                    lo = std::min(lo, x);
                    hi = std::max(hi, x);
                }
            f.inkCentre[size_t(g)] = hi < 0 ? -1.0f : float(lo + hi + 1) * 0.5f;
        }
        for (int ci = 0; ci < 8; ++ci) {
            const bool known = !colours.empty() && ci < 7; // colour 7: not drawn (no texture)
            f.fill[size_t(ci)] = known ? rgba(clutAt(colours, ci, 1)) : rgba(0x7fffu);
            f.edge[size_t(ci)] = known ? rgba(clutAt(colours, ci, 3)) : 0u;
        }
        SDL_Log("message font: PS1 pack %s%s%s", pack.id().c_str(), colours.empty() ? "" : " (+ text CLUTs)",
                f.buttons ? " (+ button CLUTs)" : "");
    } catch (const std::exception& e) {
        SDL_Log("message font (PS1 pack %s): %s", pack.id().c_str(), e.what());
    }
    return f;
}

void MessageUi::usePack(od::Renderer& r, const od::lang::LangPack* pack, const od::lang::LangPack* labels) {
    if (pack && ps1Pack != pack->id()) {
        ps1Pack = pack->id();
        ps1 = loadFont(r, *pack);
    }
    usePs1 = pack != nullptr;
    if (!labelTried) { // the Latin labels name the installed packs: with none, Japanese only, no label to draw
        labelTried = true;
        if (labels) labelFont = loadFont(r, *labels, false);
    }
}

void MessageUi::load(od::Renderer& r) {
    tried = true;
    try {
        const od::GameFile d = od::dataSource().gameFile("INIT.DAT");
        for (const od::FilePart& seg : d.parts) {
            if (seg.kind != od::FilePart::Kind::PcFont || seg.arg > 1) continue;
            Font& f = font[seg.arg];
            f.cell = int(seg.extra);
            const size_t bytes = size_t(f.cell / 2) * size_t(f.cell);
            f.count = int(seg.bytes.size() / bytes);
            f.cols = 2048 / f.cell;
            f.texW = f.cols * f.cell;
            f.texH = (f.count + f.cols - 1) / f.cols * f.cell;
            std::vector<uint32_t> px(size_t(f.texW) * size_t(f.texH), 0x00ffffffu);
            const uint8_t* src = seg.bytes.data();
            for (int g = 0; g < f.count; ++g) {
                const uint8_t* gb = src + size_t(g) * bytes;
                const int ox = (g % f.cols) * f.cell, oy = (g / f.cols) * f.cell;
                for (int y = 0; y < f.cell; ++y)
                    for (int x = 0; x < f.cell; ++x) {
                        const uint8_t b = gb[(y * f.cell + x) / 2];
                        const uint32_t v = (x & 1) ? uint32_t(b >> 4) : uint32_t(b & 15u);
                        px[size_t(oy + y) * size_t(f.texW) + size_t(ox + x)] = (v * 17u) << 24 | 0x00ffffffu;
                    }
            }
            f.tex = r.createTexture(f.texW, f.texH, px.data());
            if (!f.tex) continue;
            SDL_Log("message font %u: %d glyphs of %dx%d", seg.arg, f.count, f.cell, f.cell);
        }
    } catch (const std::exception& e) {
        SDL_Log("message font: %s", e.what());
    }
}

void MessageUi::glyph(ui::Canvas& cv, int f, int g, float x, float y, float size, float s, ui::Vec2 o, ui::Color col) const {
    const Font& F = font[f];
    if (!F.tex || g < 0 || g >= F.count) return;
    const float u0 = float((g % F.cols) * F.cell) / float(F.texW), v0 = float((g / F.cols) * F.cell) / float(F.texH);
    const float u1 = u0 + float(F.cell) / float(F.texW), v1 = v0 + float(F.cell) / float(F.texH);
    const float iu = 0.5f / float(F.texW), iv = 0.5f / float(F.texH); // half a texel in (see glyphPs1)
    cv.image(F.tex, ui::Vec2(o.x + x * s, o.y + y * s), ui::Vec2(o.x + (x + size) * s, o.y + (y + size) * s),
                 ui::Vec2(u0 + iu, v0 + iv), ui::Vec2(u1 - iu, v1 - iv), col);
}

void MessageUi::glyphPs1(ui::Canvas& cv, int g, float x, float y, float s, ui::Vec2 o, ui::Color col, const Ps1Font* font,
                  int colour) const {
    const Ps1Font& F = font ? *font : ps1;
    if (!F.col[size_t(colour & 7)] || g < 0 || g >= 21 * 20) return;
    if (const uint32_t cp = hdLayoutCodepoint(g, F); cp && cp != 0x20) {
        // a lone glyph: its ink centred where the PS1 glyph's ink is (else the cell's centre), on whole pixels
        const int px = hdPx(s);
        const od::ui::Font::Glyph f = hd.glyph(cp, px, 0);
        const float ink = F.inkCentre[size_t(g)] >= 0.0f ? F.inkCentre[size_t(g)] : F.advance(uint8_t(g)) * 0.5f;
        const float pen = std::floor(o.x + (x + ink) * s - (f.x0 + f.x1) * 0.5f + 0.5f);
        hdDraw(cv, cp, pen, std::floor(o.y + (y + 12.0f - hdRaise) * s + 0.5f), px, s, col, F, colour);
        return;
    }
    ps1Bitmap(cv, g, x, y, s, o, col, F, colour);
}

void MessageUi::ps1Bitmap(ui::Canvas& cv, int g, float x, float y, float s, ui::Vec2 o, ui::Color col, const Ps1Font& F,
                          int colour) const {
    const od::TextureHandle tex = F.col[size_t(colour & 7)];
    if (!tex || g < 0 || g >= 21 * 20) return;
    if (g == 0x34 || g == 0x37 || g == 0x59 || g == 0x5b) y += 1.0f;
    else if (g == 0x3d || g == 0x3e || g == 0x46) y += 2.0f;
    const float w = g < 256 ? F.advance(uint8_t(g)) : 12.0f; // the sprite is the glyph's width
    if (F.buttons && Ps1Font::button(g)) { // the CLUT's colours, the text colour's brightness
        const ui::Color m = std::max({col & 0xffu, (col >> 8) & 0xffu, (col >> 16) & 0xffu});
        col = (col & 0xff000000u) | m << 16 | m << 8 | m;
    }
    // the UI sampler is bilinear: half a texel in from the cell's edges, or a scaled glyph picks up the edge of
    // its neighbours in the atlas (thin marks above the text at non-integer scales)
    const float u0 = float((g % 20) * 12) / 256.0f, v0 = float((g / 20) * 12) / 256.0f, in = 0.5f / 256.0f;
    cv.image(tex, ui::Vec2(o.x + x * s, o.y + y * s), ui::Vec2(o.x + (x + w) * s, o.y + (y + 12.0f) * s),
                 ui::Vec2(u0 + in, v0 + in), ui::Vec2(u0 + w / 256.0f - in, v0 + 12.0f / 256.0f - in), col);
}

void MessageUi::loadHd(od::Renderer& r) {
    hdTried = true;
    const char* base = SDL_GetBasePath(); // opendash/build/<config>/ -> opendash/assets/fonts
    const auto path = std::filesystem::path(base ? base : "").parent_path().parent_path().parent_path() / "assets" /
                      "fonts" / "MPLUSRounded1c-Medium-Latin.ttf";
    if (!hd.load(path)) {
        SDL_Log("hd text: no font at %s (PS1 bitmaps)", path.string().c_str());
        return;
    }
    hdTex = r.createTexture(od::ui::Font::kAtlas, od::ui::Font::kAtlas, hd.atlas().data());
    hd.dirty = false;
    SDL_Log("hd text: %s", path.string().c_str());
}

void MessageUi::flushHd(od::Renderer& r) {
    if (!hd.dirty || !hdTex) return;
    r.updateTexture(hdTex, hd.atlas().data());
    hd.dirty = false;
}

uint32_t MessageUi::hdCodepoint(int g) {
    static const std::array<uint32_t, 256> kTable = [] {
        std::array<uint32_t, 256> t{};
        for (int k = 0; k < 256; ++k) {
            // the JP full stop (a small circle) and the corner brackets keep their own shapes
            if (k == 0x0b || k == 0x0f || k == 0x4d) continue;
            const std::string_view s = od::lang::ps1GlyphText(uint8_t(k));
            if (s == "...") {
                t[size_t(k)] = 0x2026;
                continue;
            }
            const auto* u = reinterpret_cast<const unsigned char*>(s.data());
            if (s.size() == 1 && u[0] > 0x20) t[size_t(k)] = u[0];
            else if (s.size() == 2 && (u[0] & 0xe0) == 0xc0) t[size_t(k)] = uint32_t(u[0] & 0x1f) << 6 | (u[1] & 0x3f);
            else if (s.size() == 3 && (u[0] & 0xf0) == 0xe0)
                t[size_t(k)] = uint32_t(u[0] & 0x0f) << 12 | uint32_t(u[1] & 0x3f) << 6 | (u[2] & 0x3f);
        }
        return t;
    }();
    return g >= 0 && g < 256 ? kTable[size_t(g)] : 0;
}

uint32_t MessageUi::hdLayoutCodepoint(int g, const Ps1Font& F) const {
    if (!hdText || !hdTex || g < 0 || g >= 256 || (F.buttons && Ps1Font::button(g))) return 0;
    if (od::lang::ps1GlyphSpace(uint8_t(g))) return 0x20;
    const uint32_t cp = hdCodepoint(g);
    return cp && hd.has(cp) ? cp : 0;
}

int MessageUi::hdPx(float s) const {
    constexpr float kCap = 10.0f; // PS1 pixels: the capitals' height
    return int(std::lround(hd.emForCapHeight(kCap * s)));
}

void MessageUi::hdDraw(ui::Canvas& cv, uint32_t cp, float pen, float base, int px, float s, ui::Color col,
                       const Ps1Font& F, int colour) const {
    constexpr float kEdge = 0.5f; // PS1 pixels: the dark edge's width
    const int edgePx = std::max(1, int(std::lround(kEdge * s)));
    const od::ui::Font::Glyph e = hd.glyph(cp, px, edgePx); // copies: a full atlas starts over
    const od::ui::Font::Glyph f = hd.glyph(cp, px, 0);
    if (!f.ok) return;
    auto mul = [](ui::Color a, ui::Color b) {
        ui::Color r = 0;
        for (int k = 0; k < 32; k += 8) r |= (((a >> k) & 0xffu) * ((b >> k) & 0xffu) + 127u) / 255u << k;
        return r;
    };
    auto put = [&](const od::ui::Font::Glyph& q, ui::Color c) {
        cv.image(hdTex, ui::Vec2(pen + q.x0, base + q.y0), ui::Vec2(pen + q.x1, base + q.y1), ui::Vec2(q.u0, q.v0),
                 ui::Vec2(q.u1, q.v1), c);
    };
    if (e.ok) put(e, mul(F.edge[size_t(colour & 7)], col));
    put(f, mul(F.fill[size_t(colour & 7)], col));
}

float MessageUi::run(ui::Canvas* cv, const std::vector<RunGlyph>& gs, size_t shown, float x, float y, float s,
                     ui::Vec2 o, const Ps1Font& F) const {
    float ps1W = 0.0f;
    for (const RunGlyph& r : gs) ps1W += F.advance(r.g);
    shown = cv ? std::min(shown, gs.size()) : 0;
    if (!hdText || !hdTex) {
        float cx = x;
        for (size_t i = 0; i < shown; ++i) {
            if (!od::lang::ps1GlyphSpace(gs[i].g)) ps1Bitmap(*cv, gs[i].g, cx, y, s, o, gs[i].col, F, gs[i].colour);
            cx += F.advance(gs[i].g);
        }
        return ps1W;
    }
    // the advances (screen px): the font's, plus the kerning with the next glyph; a PS1 bitmap keeps its PS1 advance
    const int px = hdPx(s);
    std::vector<uint32_t> cps(gs.size());
    std::vector<float> adv(gs.size());
    float w = 0.0f;
    for (size_t i = 0; i < gs.size(); ++i) {
        cps[i] = hdLayoutCodepoint(gs[i].g, F);
        adv[i] = cps[i] ? hd.glyph(cps[i], px, 0).advance : F.advance(gs[i].g) * s;
        if (i > 0 && cps[i - 1] && cps[i]) adv[i - 1] += hd.kern(cps[i - 1], cps[i], px);
    }
    for (const float a : adv) w += a;
    // wider than the PS1 run: the excess taken out of the gaps between the glyphs
    const float track = w > ps1W * s && gs.size() > 1 ? (ps1W * s - w) / float(gs.size() - 1) : 0.0f;
    const float base = std::floor(o.y + (y + 12.0f - hdRaise) * s + 0.5f);
    float pen = o.x + x * s;
    for (size_t i = 0; i < shown; ++i) {
        const RunGlyph& r = gs[i];
        if (F.col[size_t(r.colour & 7)]) { // colour 7: not drawn
            if (cps[i] && cps[i] != 0x20) hdDraw(*cv, cps[i], std::floor(pen + 0.5f), base, px, s, r.col, F, r.colour);
            else if (!cps[i] && !od::lang::ps1GlyphSpace(r.g)) ps1Bitmap(*cv, r.g, (pen - o.x) / s, y, s, o, r.col, F, r.colour);
        }
        pen += adv[i] + track;
    }
    return (w + track * float(gs.size() > 1 ? gs.size() - 1 : 0)) / s;
}

MessageUi::PageState MessageUi::pageState(const od::port::MessageWindow& w) {
    PageState ps;
    ps.v68 = w.number;
    ps.v64 = w.timeValue;
    ps.v6e = w.place;
    return ps;
}

bool MessageUi::pageOp(const od::port::MessageWindow& win, PageState& ps, uint32_t& p, uint32_t& ret) {
    if (p >= kLoc || ret != 0) return false;
    const od::port::TextBytes t(p);
    const uint8_t op = t.byte(1);
    if (op == 0x45) {
        int8_t k = t.signedByte(2);
        if (k < 0) k = win.record;
        if (k < 5) {
            ps.v68 = k + 1;
            const od::port::SaveSlotSummary& slot = od::port::saveSlotSummaries().slots[size_t(k)];
            ps.v6e = slot.place;
            ps.v64 = slot.time;
        } else {
            ps.v68 = -1;
            ps.v64 = -2;
            ps.v6e = 0;
        }
        p += 3;
        return true;
    }
    if (op == 0x21) {
        const uint8_t k = t.byte(2);
        ps.loc = od::port::messageNumberText((k & 0x80) ? int32_t(od::port::gameState().zenny) : ps.v68, k & 0x7f,
                                             od::port::ps1Window(win));
    } else if (op == 0x23) {
        const int8_t k = t.signedByte(2);
        const bool pk = od::port::ps1Window(win);
        ps.loc = k == 0   ? od::port::messageTimeText(int32_t(od::port::gameState().playTime), od::port::TimeFormat::Clock, pk)
                 : k == 1 ? od::port::messageTimeText(ps.v64, od::port::TimeFormat::Clock, pk)
                 : k == 2 ? od::port::messageTimeText(ps.v64, od::port::TimeFormat::Lap, pk)
                          : std::vector<uint8_t>{0xfb, 0x1f};
    } else if (op == 0x44) {
        ret = p + 3;
        p = od::port::MessageBank(od::port::bankRedirect(od::stateAddress(od::TableId::TextScriptPlaceNameBank))).message(ps.v6e);
        return true;
    } else {
        return false;
    }
    ret = p + 3;
    p = kLoc;
    return true;
}

void MessageUi::draw(ui::Canvas& cv, float width, float height, int pass) const {
    const float s = height / 240.0f;
    const ui::Vec2 o((width - 320.0f * s) * 0.5f, 0.0f);
    // the windows in the exe's drawing order: each links its text and frame into OT 0xa67840 + 0x70 + layer (+0x23)
    // * 4, the OT is walked from its highest entry, an entry's chains newest first (window 4 before window 0)
    std::array<uint32_t, 5> order{};
    std::array<int, 5> layer{};
    // the window w's record to draw: the window, or OpenDash's copy of it when the layout pass closed it this tick
    auto shownWindow = [](uint32_t w) -> const od::port::MessageWindow& {
        const od::port::MessageWindow& live = od::port::messageWindow(w);
        return live.flags == 0 ? od::port::drawnWindowCopy(w) : live;
    };
    for (uint32_t w = 0; w < 5; ++w) {
        order[w] = w;
        layer[w] = int(int8_t(shownWindow(w).layer));
    }
    std::stable_sort(order.begin(), order.end(), [&](uint32_t x, uint32_t y) {
        return layer[x] != layer[y] ? layer[x] > layer[y] : x > y;
    });
    std::vector<od::port::ScreenPrim> frame;
    for (const uint32_t w : order) {
        if (pass == 0 && layer[w] < 3) continue;
        if (pass == 1 && layer[w] >= 3) continue;
        const od::port::MessageWindow& win = shownWindow(w);
        // frame shown (0x8000), or a frameless text window (no fb 05: e.g. the shop's price question) whose layout
        // pass runs (flags 1 | 2)
        const bool framed = (win.flags & 0x8000) != 0;
        const bool pk = usePs1 && ps1.tex && od::port::ps1Window(win); // the pack's text (else the exe's own bank)
        if (!framed && (win.flags & 3) != 3) continue;
        // the frame (FUN_005583f0: linked after the text into the same OT entry, so drawn under it)
        frame.clear();
        od::port::windowFrameDraw(win, frame);
        FlatPaint::drawAll(cv, s, o, frame, vectorLines);
        // FUN_00557eb0: the text pass runs only with flags & 0xf0 and & 2, and its glyphs are linked under a draw
        // area = the window rect +0x54.. (x + 1, y + 3, w - 2, h - 3; empty when w or h < 4), framed or not: the
        // text shrinks and vanishes with the closing frame. The hand cursor belongs to the frame pass
        // (FUN_005583f0) and is not clipped.
        const uint32_t wf = win.flags;
        const float rx = win.frame.x, ry = win.frame.y, rw = uint16_t(win.frame.w), rh = uint16_t(win.frame.h);
        const bool textOn = (wf & 0xf0) && (wf & 2) && rw >= 4 && rh >= 4;
        if (textOn)
            cv.pushClip(ui::Vec2(o.x + (rx + 1) * s, o.y + (ry + 3) * s),
                             ui::Vec2(o.x + (rx + rw - 1) * s, o.y + (ry + rh) * s), true);
        const float x0 = win.x, y0 = win.y;
        const uint32_t end = win.layoutAt;
        uint32_t p = textOn ? win.pageStart + 2 : end;
        // the glyph packets at x0 + x, y0 + FUN_00559620(line) (FUN_00557eb0 / FUN_00558110)
        int line = 0;
        bool big = false; // a 24 px glyph (>= 0xe0) on this line: flag 0x400000, the next fc goes down two lines
        float cx = 0.0f, cy = float(od::port::messageLineY(win, 0));
        const ui::Color white = ui::rgba(255, 255, 255, 255);
        ui::Color col = white; // +0x1b: 8 = 0x808080, 4 = 0x404040 (the options a choice does not point at)
        // the colour index +0x21 (the text pass starts from +0x20; fb 0a / 0b, the option markers): a pack's text
        // through the PS1 glyph CLUT of that index (the PC palettes 0x8b176c are not modelled: the exe's glyphs white)
        int ci = win.baseColour & 7;
        uint8_t prevCi = win.savedColour;
        const ui::Color dim = ui::rgba(128, 128, 128, 255);
        uint32_t ret = 0; // fb 20 / 21 / 22 / 23 / 44 insert a string ended by fb 1f (as the layout pass)
        auto be16 = [&](uint32_t q) { return od::port::TextBytes(q).be16(0); };
        PageState ps = pageState(win);
        auto rd = [&](uint32_t q) -> uint8_t {
            if (q >= kLoc) return q - kLoc < ps.loc.size() ? ps.loc[q - kLoc] : uint8_t(0xff);
            return od::port::TextBytes(q).byte(0);
        };
        // the pack glyphs of the run being collected (up to a line break or a cursor move), drawn together (see
        // hdText); past `end` (the text not typed out yet) they are only measured, so a run keeps its spacing while
        // it types out
        std::vector<RunGlyph> runGlyphs;
        size_t runShown = 0;
        float runX = 0.0f, runY = 0.0f;
        bool typed = true;
        auto flush = [&] {
            hdRaise = kWindowTextRaise;
            if (!runGlyphs.empty()) run(&cv, runGlyphs, runShown, x0 + runX, y0 + runY, s, o, ps1);
            hdRaise = 0.0f;
            runGlyphs.clear();
            runShown = 0;
        };
        // the run ends here: drawn (true = past `end`, the walk stops)
        auto runEnds = [&] {
            if (!typed) return true;
            flush();
            return false;
        };
        for (int guard = 0; guard < 2048 && (p >= kLoc || od::port::textMapped(p, 4)); ++guard) {
            if (typed && (p == end || (ret == 0 && p >= end))) {
                if (runGlyphs.empty()) break;
                typed = false;
            }
            const uint8_t c = rd(p);
            if (c == 0xfb) {
                const uint8_t op = rd(p + 1);
                if (op == 0x1f && ret != 0) { p = ret; ret = 0; continue; }
                if (pageOp(win, ps, p, ret)) continue;
                if (op == 0x20 && ret == 0) { ret = p + 4; p = nameAt(be16(p + 2)); continue; }
                if (op == 0x3d && ret == 0) { ret = p + 2; p = nameAt(win.itemName); continue; }
                if (op == 0x21 && ret == 0) { ret = p + 3; p = 0x8b172c; continue; }
                if (op == 0x22 && ret == 0) { ret = p + 3; p = od::port::MessageBank(od::stateAddress(od::TableId::TextScriptKeyNameBank)).message(od::port::TextBytes(p).byte(2)); continue; }
                if (op == 0x23 && ret == 0) { ret = p + 3; p = 0x8b1738; continue; }
                if (op == 0x44 && ret == 0) { ret = p + 3; p = od::port::MessageBank(od::port::bankRedirect(od::stateAddress(od::TableId::TextScriptPlaceNameBank))).message(win.place); continue; }
                if (op == 0x0a) { // fb 0a c (FUN_00559c70): colour c & 0x7f (> 7: 0), c & 0x80 dim
                    const uint8_t c2 = rd(p + 2);
                    prevCi = uint8_t(ci);
                    ci = (c2 & 0x7f) > 7 ? 0 : (c2 & 0x7f);
                    col = (c2 & 0x80) ? dim : white;
                    p += 3;
                    continue;
                }
                if (op == 0x0b) { // fb 0b (FUN_00559cc0): back to the previous colour
                    ci = prevCi & 0x7f;
                    col = (prevCi & 0x80) ? dim : white;
                    p += 2;
                    continue;
                }
                if (op == 0x2e) {
                    if (runEnds()) break;
                    const uint8_t k = od::port::TextBytes(p).byte(2);
                    if ((k & 0xc0) == 0xc0) // OpenDash's texts (game/status_options): to the 12 px cell k & 0x3f, a space further
                        cx = std::max(cx + 4.0f, float(k & 0x3f) * 12.0f);
                    else
                        cx += float(int8_t(k));
                    p += 3;
                    continue;
                }
            }
            if (c < 0xd0 && pk) { // language pack font (proportional)
                if (runGlyphs.empty()) {
                    runX = cx;
                    runY = cy;
                }
                runGlyphs.push_back({c, uint8_t(ci & 7), col});
                if (typed) runShown = runGlyphs.size();
                cx += ps1Advance(c);
                ++p;
                continue;
            }
            // an op that moves no glyph (a wait, ...) keeps the run; past `end` it ends the measuring (a page break)
            const bool skip = c == 0xfb && rd(p + 1) != 0x0f;
            if ((!skip || !typed) && runEnds()) break;
            if (c < 0xd0) {
                glyph(cv, 0, c, x0 + cx, y0 + cy, 12.0f, s, o, col);
                cx += 12.0f;
                ++p;
            } else if (c < 0xe0) {
                glyph(cv, 0, (c - 0xd0) << 8 | od::port::TextBytes(p).byte(1), x0 + cx, y0 + cy, 12.0f, s, o, col);
                cx += 12.0f;
                p += 2;
            } else if (c < 0xfb) {
                glyph(cv, 1, (c - 0xe0) << 8 | od::port::TextBytes(p).byte(1), x0 + cx, y0 + cy, 24.0f, s, o, col);
                cx += 24.0f;
                p += 2;
                big = true;
            } else if (c == 0xfc) { // FUN_005597d0: one line down, two after a 24 px glyph
                cx = 0.0f;
                line += big ? 2 : 1;
                big = false;
                cy = float(od::port::messageLineY(win, line));
                ++p;
            } else if (c == 0xfe) { // glyph + n furigana glyphs (5 px, 6 px higher, at x + k - 0xb)
                const od::port::TextBytes t(p);
                const uint8_t g0 = t.byte(1);
                const bool two = g0 >= 0xd0;
                glyph(cv, 0, two ? (g0 - 0xd0) << 8 | t.byte(2) : g0, x0 + cx, y0 + cy, 12.0f, s, o, col);
                const uint32_t q = p + 1 + (two ? 2 : 1);
                const od::port::TextBytes ruby(q);
                // FUN_005598f0: the ruby from the x after the glyph (+12) + k - 11, 6 px higher; only with the
                // furigana option (0xa6d1ca) and not in a 0x80000000-only window
                const uint32_t wf2 = win.flags;
                const uint8_t n = ((wf2 & 0x40000000) || !(wf2 & 0x80000000u)) && od::port::gameOptions().ruby ? ruby.byte(0) : 0;
                float rx = x0 + cx + 12.0f + float(ruby.signedByte(1)) - 11.0f;
                for (uint32_t i = 0; i < n; ++i) {
                    const uint8_t r = ruby.byte(2 + i);
                    if (r != 0x20) glyph(cv, 0, r, rx, y0 + cy - 6.0f, 6.0f, s, o, col);
                    rx += (r == 0x20 || (r > 0xad && r < 0xb7)) ? 4.0f : 5.0f;
                }
                cx += 12.0f;
                p = q + 2 + ruby.byte(0);
            } else if (c == 0xfb && od::port::TextBytes(p).byte(1) == 0x0f) {
                // option marker (FUN_00559de0 / SLUS 0x8004c850): the hand's 12 px slot; the option the choice
                // points at (before the choice: flags & 8) bright, the others 0x404040; 0x800000 windows: all
                // bright, a bullet (PC 0x2e / PS1 0x10) at x - 8, y + 6; the colour index +0x21 = +0x20 on the
                // pointed one, else 6 in 0x800000 windows (bullet and text: SLUS 0x8004c850, PC FUN_00559de0)
                const uint32_t f = win.flags;
                const bool on = od::port::TextBytes(p).byte(2) == ((f >> 8) & 0xf) && ((f & 0x20) || (f & 8));
                cx += 12.0f;
                col = (on || (f & 0x800000)) ? white : dim;
                ci = (on || !(f & 0x800000)) ? (win.baseColour & 7) : 6;
                if (f & 0x800000) {
                    if (pk) glyphPs1(cv, 0x10, x0 + cx - 8.0f, y0 + cy + 6.0f, s, o, white, nullptr, ci);
                    else glyph(cv, 0, 0x2e, x0 + cx - 8.0f, y0 + cy + 6.0f, 12.0f, s, o, white);
                }
                p += 3;
            } else {
                const uint32_t len = c == 0xfb ? od::port::messageOpLength(od::port::TextBytes(p)) : 0;
                if (len == 0) break;
                p += len;
            }
        }
        flush();
        if (textOn) cv.popClip();
        // the hand while a choice is up (fb 39 / fb 10; FUN_00559160 / SLUS 0x8004bbe4): at x0 + kx - 2 + the bob
        // (0x8b1f3c[(frame >> 1) & 15]), y0 + ky; +0x20 picks the style (PC glyph 1 / 0; PS1 font cells u 0xc0 /
        // 0x9c, v 0x90)
        const int16_t kx = win.handX, ky = win.handY;
        if ((kx || ky) && (win.flags & 0x20)) {
            const float hx = x0 + float(kx) - 2.0f + float(handBob[(od::port::gameState().frame >> 1) & 15]), hy = y0 + float(ky);
            const bool plain = win.baseColour == 0;
            // its CLUT: text colour 0 (plain) / 1 (SLUS 0x8004bc34: gp + 0x2ec / 0x2ee)
            if (pk) glyphPs1(cv, plain ? 12 * 20 + 16 : 12 * 20 + 13, hx, hy, s, o, white, nullptr, plain ? 0 : 1);
            else glyph(cv, 0, plain ? 1 : 0, hx, hy, 12.0f, s, o, white);
        }
    }
}

void MessageUi::label(ui::Canvas& cv, const char* t, float x, float y, float s, ui::Vec2 o, ui::Color col) const {
    const Ps1Font* f = labelFont.tex ? &labelFont : &ps1;
    std::vector<RunGlyph> gs;
    for (const uint8_t g : od::lang::ps1Encode(t)) gs.push_back({g, 0, col});
    run(&cv, gs, gs.size(), x, y, s, o, *f);
}

void MessageUi::bankText(ui::Canvas& cv, uint32_t bank, int id, float x, float y, float s, ui::Vec2 o,
                  ui::Color col, float* width) const {
    if (!(usePs1 && ps1.tex)) {
        if (!width) drawString(cv, bank, id, x, y, s, o, col);
        return;
    }
    const od::port::MessageBank strings(bank);
    if (!strings.entryMapped(id)) return;
    uint32_t p = strings.message(id);
    std::vector<RunGlyph> gs;
    for (int guard = 0; guard < 256 && od::port::textMapped(p, 4); ++guard) {
        const od::port::TextBytes t(p);
        const uint8_t c = t.byte(0);
        if (c == 0xff || c == 0xfc || (c == 0xfb && t.byte(1) == 0x1f)) break;
        if (c >= 0xd0) {
            const uint32_t len = c == 0xfb ? od::port::messageOpLength(od::port::TextBytes(p)) : (c < 0xfb ? 2u : 0u);
            if (len == 0) break;
            p += len;
            continue;
        }
        // the pack's PS1 font, laid out as the window text
        gs.push_back({c, 0, col});
        ++p;
    }
    const float w = run(width ? nullptr : &cv, gs, gs.size(), x, y, s, o, ps1);
    if (width) *width = w;
}

void MessageUi::menuText(ui::Canvas& cv, const od::port::ScreenPrim& p, float s, ui::Vec2 o) const {
    auto ch = [](uint32_t v) { return std::min<uint32_t>(v * 2, 255); };
    const ui::Color col = ui::rgba(ch(p.colour & 0xff), ch((p.colour >> 8) & 0xff), ch((p.colour >> 16) & 0xff), 255);
    if (p.kind == od::port::ScreenPrim::Glyph) { // sampled from p.u texels (of the 24-texel cell) on
        const Font& F = font[p.font];
        const int g = p.code;
        if (hdText && hdTex && g > 0x20 && g < 0x7f && hd.has(uint32_t(g))) {
            // HD text: the PC font's ASCII glyph (digits of the counters, ...) from the TrueType font, centred in
            // its cell, the capitals 3 / 4 of the cell high
            const float size = float(p.size >> 1), cap = size * 0.75f;
            const int px = int(std::lround(hd.emForCapHeight(cap * s)));
            const ui::Font::Glyph q = hd.glyph(uint32_t(g), px, 0);
            if (q.ok) {
                const float pen = std::floor(o.x + (p.gx + size * 0.5f) * s - (q.x0 + q.x1) * 0.5f + 0.5f);
                const float base = std::floor(o.y + (p.gy + size * 0.5f + cap * 0.5f) * s + 0.5f);
                cv.image(hdTex, ui::Vec2(pen + q.x0, base + q.y0), ui::Vec2(pen + q.x1, base + q.y1),
                         ui::Vec2(q.u0, q.v0), ui::Vec2(q.u1, q.v1), col);
            }
            return;
        }
        if (!F.tex || g >= F.count) return;
        const float size = float(p.size >> 1), du = float(p.u) * float(F.cell) / 24.0f;
        const float u0 = (float((g % F.cols) * F.cell) + du) / float(F.texW), v0 = float((g / F.cols) * F.cell) / float(F.texH);
        const float u1 = u0 + float(F.cell) / float(F.texW), v1 = v0 + float(F.cell) / float(F.texH);
        const float iu = 0.5f / float(F.texW), iv = 0.5f / float(F.texH); // half a texel in (see glyphPs1)
        cv.image(F.tex, ui::Vec2(o.x + p.gx * s, o.y + p.gy * s),
                     ui::Vec2(o.x + (p.gx + size) * s, o.y + (p.gy + size) * s), ui::Vec2(u0 + iu, v0 + iv),
                     ui::Vec2(u1 - iu, v1 - iv), col);
        return;
    }
    if (p.bank == 0) { // an OpenDash string: PC glyphs (12 px cells) or the Latin font
        if (p.pc) {
            float x = p.x;
            for (const uint16_t* c = p.pc; *c; ++c, x += 12.0f) glyph(cv, 0, *c, x, p.y, 12.0f, s, o, col);
        } else if (p.utf8) {
            label(cv, p.utf8, p.x, p.y, s, o, col);
        }
        return;
    }
    if (!(p.lang && usePs1 && ps1.tex)) {
        drawString(cv, p.bank, p.id, p.x, p.y, s, o, col);
        return;
    }
    float w = 0.0f;
    bankText(cv, p.bank, p.id, 0, 0, s, o, col, &w);
    float x = p.x;
    if (p.align == 1 || p.align == 3) x = float(p.ax) - 1.0f + (120.0f - w) * 0.5f;
    else if (p.align == 2) x = float(p.ax) + 119.0f - w;
    bankText(cv, p.bank, p.id, x, p.y, s, o, col);
}

void MessageUi::drawString(ui::Canvas& cv, uint32_t bank, int id, float x, float y, float s, ui::Vec2 o,
                    ui::Color col) const {
    const od::port::MessageBank strings(bank);
    if (!strings.entryMapped(id)) return;
    uint32_t p = strings.message(id); // the system banks' strings have no page header
    float cx = 0.0f, cy = 0.0f;
    for (int guard = 0; guard < 256 && od::port::textMapped(p, 4); ++guard) {
        const od::port::TextBytes t(p);
        const uint8_t c = t.byte(0);
        if (c == 0xff || (c == 0xfb && t.byte(1) == 0x1f)) break; // as FUN_005578c0: fb 1f ends a string
        if (c < 0xd0) {
            glyph(cv, 0, c, x + cx, y + cy, 12.0f, s, o, col);
            cx += 12.0f;
            ++p;
        } else if (c < 0xe0) {
            glyph(cv, 0, (c - 0xd0) << 8 | t.byte(1), x + cx, y + cy, 12.0f, s, o, col);
            cx += 12.0f;
            p += 2;
        } else if (c < 0xfb) {
            p += 2;
        } else if (c == 0xfc) {
            cx = 0.0f;
            cy += 16.0f;
            ++p;
        } else if (c == 0xfe) {
            const uint8_t g0 = t.byte(1);
            const bool two = g0 >= 0xd0;
            glyph(cv, 0, two ? (g0 - 0xd0) << 8 | t.byte(2) : g0, x + cx, y + cy, 12.0f, s, o, col);
            const uint32_t q = p + 1 + (two ? 2 : 1);
            cx += 12.0f;
            p = q + 2 + od::port::TextBytes(q).byte(0);
        } else {
            const uint32_t len = c == 0xfb ? od::port::messageOpLength(od::port::TextBytes(p)) : 0;
            if (len == 0) break;
            p += len;
        }
    }
}

} // namespace od::app
