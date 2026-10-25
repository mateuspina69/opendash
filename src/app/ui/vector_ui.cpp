#include "app/ui/vector_ui.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <string>
#include <utility>

#include "app/ui/prompts.hpp"

namespace od::app {

using SP = od::port::ScreenPrim;

std::filesystem::path assetsDir() {
    const char* base = SDL_GetBasePath();
    return std::filesystem::path(base ? base : "").parent_path().parent_path().parent_path() / "assets";
}

namespace {

const char* fileOf(uint16_t vec) {
    switch (vec) {
    case od::port::kVecStatusPanel: return "status.svg";
    case od::port::kVecOptJunction: return "options_junction.svg";
    default: return nullptr;
    }
}

// the palette of the sub-screens' art
constexpr ui::Color kDark = ui::rgba(0x30, 0x48, 0x58), kTealText = ui::rgba(0x48, 0xd0, 0xd0),
                    kTextEdge = ui::rgba(0x18, 0x38, 0x48), kRing = ui::rgba(0x80, 0xe8, 0xb8),
                    kRow = ui::rgba(0x50, 0x58, 0x70), kRowCursor = ui::rgba(0x30, 0x10, 0x18),
                    kRowText = ui::rgba(0xf0, 0xf0, 0xe8), kRowTextCursor = ui::rgba(0x48, 0x40, 0x40),
                    kBar = ui::rgba(0x1a, 0x22, 0x2c), kBarText = ui::rgba(0xc8, 0xcc, 0xd0),
                    kBarTextDim = ui::rgba(0x70, 0x74, 0x78), kHint = ui::rgba(0xb8, 0xf0, 0xff);

} // namespace

void VectorUi::setLanguage(const std::string& code) {
    if (code == lang_) return;
    lang_ = code;
    words_.clear();
    std::ifstream f(assetsDir() / "ui" / "text" / (code + ".txt"));
    for (std::string line; std::getline(f, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq != std::string::npos) words_[line.substr(0, eq)] = line.substr(eq + 1);
    }
}

const std::string* VectorUi::word(const std::string& key) const {
    const auto it = words_.find(key);
    if (it != words_.end()) return &it->second;
    if (missing_.insert(lang_ + ":" + key).second) // the words still to write (assets/ui/text)
        SDL_Log("vector ui: no words for %s (%s): the pixel art stays", key.c_str(), lang_.c_str());
    return nullptr;
}

bool VectorUi::fonts(od::Renderer& r) {
    if (!bold_.tried) bold_.load(r, assetsDir() / "fonts" / "MPLUSRounded1c-ExtraBold-Latin.ttf");
    if (!regular_.tried) regular_.load(r, assetsDir() / "fonts" / "MPLUSRounded1c-Medium-Latin.ttf");
    return bold_.ready() && regular_.ready();
}

void VectorUi::flush(od::Renderer& r) {
    bold_.flush(r);
    regular_.flush(r);
}

namespace {

std::string num(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.3f", double(v));
    std::string t = b;
    while (!t.empty() && t.back() == '0') t.pop_back();
    if (!t.empty() && t.back() == '.') t.pop_back();
    return t;
}

std::string pathOf(std::initializer_list<std::pair<float, float>> pts, const char* fill) {
    std::string d = "<path d=\"";
    char op = 'M';
    for (const auto& [px, py] : pts) {
        d += op + num(px) + " " + num(py) + " ";
        op = 'L';
    }
    return d + "Z\" fill=\"" + fill + "\"/>";
}

// a corner ring of the frames (the status screen's too): the dark disc, the mint ring, its darker diagonals
std::string ringSvg(float cx, float cy) {
    std::string r = "<circle cx=\"" + num(cx) + "\" cy=\"" + num(cy) + "\" r=\"5\" fill=\"#304858\"/>";
    r += "<circle cx=\"" + num(cx) + "\" cy=\"" + num(cy) + "\" r=\"2.5\" fill=\"none\" stroke=\"#80e8b8\" stroke-width=\"1\"/>";
    for (const float a : {45.0f, 135.0f, 225.0f, 315.0f}) {
        const float a0 = (a - 18.0f) * 3.14159265f / 180.0f, a1 = (a + 18.0f) * 3.14159265f / 180.0f;
        r += "<path d=\"M" + num(cx + 2.5f * std::cos(a0)) + " " + num(cy + 2.5f * std::sin(a0)) + " A2.5 2.5 0 0 1 " +
             num(cx + 2.5f * std::cos(a1)) + " " + num(cy + 2.5f * std::sin(a1)) +
             "\" fill=\"none\" stroke=\"#709858\" stroke-width=\"1\"/>";
    }
    return r;
}

} // namespace

// The 9-slice frames (FUN_005e8130) as SVG at the call's size, transcribed from their pieces' pixel art (1-pixel
// staircases: the line through the pixels' centres; straight edges where the pixels' are):
// style 0 -- the help frame: a rect inset 4 / 6, its outline 2 px, a ring on each corner (centre = corner + 2);
// style 1 -- the list frame: the outline 2 px, 3.5 cuts at the top and the bottom corners, 1 px thick on the right
//   between the corner pieces (rows 8 .. h - 9), its bottom 2 px thick between x 8 .. w - 8 (a lip under the middle);
//   the inside's cuts 2.5.
std::string VectorUi::frameSvg(int style, float w, float h) {
    std::string body;
    if (style == 0) {
        const float l = 4.0f, t = 6.0f, r = w - 4.0f, b = h - 6.0f;
        body += pathOf({{l, t}, {r, t}, {r, b}, {l, b}}, "#304858");
        body += pathOf({{l + 2, t + 2}, {r - 2, t + 2}, {r - 2, b - 2}, {l + 2, b - 2}}, "#383040");
        for (const auto& [cx, cy] : {std::pair{l + 2, t + 2}, std::pair{r - 2, t + 2}, std::pair{l + 2, b - 2},
                                     std::pair{r - 2, b - 2}})
            body += ringSvg(cx, cy);
    } else if (style == 1) {
        body += pathOf({{3.5f, 0}, {w - 3.5f, 0}, {w, 3.5f}, {w, h - 4.5f}, {w - 3.5f, h - 1}, {w - 8, h - 1},
                        {w - 8, h}, {8, h}, {8, h - 1}, {3.5f, h - 1}, {0, h - 4.5f}, {0, 3.5f}},
                       "#304858");
        body += pathOf({{4.5f, 2}, {w - 4.5f, 2}, {w - 2, 4.5f}, {w - 2, 8}, {w - 1, 8}, {w - 1, h - 8}, {w - 2, h - 8},
                        {w - 2, h - 5.5f}, {w - 4.5f, h - 3}, {w - 8, h - 3}, {w - 8, h - 2}, {8, h - 2}, {8, h - 3},
                        {4.5f, h - 3}, {2, h - 5.5f}, {2, 4.5f}},
                       "#383040");
    } else if (style == 2) {
        // the green frame (the options panel, the left lists): rows 1 .. h - 2 (a 2 px lip under the middle,
        // x 16 .. w - 16); the outline 2 px with 3.5 cuts; the band (its shade line in the cuts) 8 px on the left, 3
        // on the right at the corners, 6 between them (rows 16 .. h - 17: the stretched piece as the game shows it);
        // the inner border bumping out on the right at both corners (the brass arrows in the bumps), the inside one
        // row deeper under the middle
        body += pathOf({{3.5f, 1}, {w - 3.5f, 1}, {w, 4.5f}, {w, h - 4.5f}, {w - 3.5f, h - 1}, {w - 16, h - 1},
                        {w - 16, h}, {16, h}, {16, h - 1}, {3.5f, h - 1}, {0, h - 4.5f}, {0, 4.5f}},
                       "#304858");
        body += pathOf({{4.5f, 3}, {w - 4.5f, 3}, {w - 2, 5.5f}, {w - 2, h - 5.5f}, {w - 4.5f, h - 3},
                        {w - 16, h - 3}, {w - 16, h - 2},
                        {16, h - 2}, {16, h - 3}, {4.5f, h - 3}, {2, h - 5.5f}, {2, 5.5f}},
                       "#489070");
        body += pathOf({{4.5f, 3}, {5.5f, 3}, {2, 6.5f}, {2, 5.5f}}, "#007858");
        body += pathOf({{w - 4.5f, 3}, {w - 5.5f, 3}, {w - 2, 6.5f}, {w - 2, 5.5f}}, "#007858");
        body += pathOf({{w - 2, h - 5.5f}, {w - 2, h - 6.5f}, {w - 5.5f, h - 3}, {w - 4.5f, h - 3}}, "#007858");
        body += pathOf({{2, h - 5.5f}, {2, h - 6.5f}, {5.5f, h - 3}, {4.5f, h - 3}}, "#007858");
        body += pathOf({{13.5f, 4}, {w - 8.5f, 4}, {w - 4, 8.5f}, {w - 4, 11.5f}, {w - 8.5f, 16}, {w - 8, 16},
                        {w - 8, h - 16}, {w - 8.5f, h - 16}, {w - 4, h - 11.5f}, {w - 4, h - 8.5f}, {w - 8.5f, h - 4},
                        {w - 16, h - 4}, {w - 16, h - 3}, {16, h - 3}, {16, h - 4}, {13.5f, h - 4}, {10, h - 7.5f},
                        {10, h - 16}, {11, h - 16}, {11, 16}, {10, 16}, {10, 7.5f}},
                       "#68a078");
        body += pathOf({{13.5f, 5}, {w - 8.5f, 5}, {w - 5, 8.5f}, {w - 5, 11.5f}, {w - 9.5f, 16}, {w - 9, 16},
                        {w - 9, h - 16}, {w - 9.5f, h - 16}, {w - 5, h - 11.5f}, {w - 5, h - 8.5f}, {w - 8.5f, h - 5},
                        {w - 16, h - 5}, {w - 16, h - 4}, {16, h - 4}, {16, h - 5}, {13.5f, h - 5}, {11, h - 7.5f},
                        {11, h - 16}, {12, h - 16}, {12, 16}, {11, 16}, {11, 7.5f}},
                       "#b0e8a8");
        for (const float t : {7.0f, h - 13.0f}) { // the brass arrows (rows t .. t + 5), lit on both slants
            body += pathOf({{w - 10, t}, {w - 8.5f, t}, {w - 7, t + 1.5f}, {w - 7, t + 4.5f}, {w - 8.5f, t + 6},
                            {w - 10, t + 6}},
                           "#b09868");
            body += pathOf({{w - 9.5f, t}, {w - 8.5f, t}, {w - 7, t + 1.5f}, {w - 7, t + 2.5f}}, "#b0d068");
            body += pathOf({{w - 7, t + 3.5f}, {w - 7, t + 4.5f}, {w - 8.5f, t + 6}, {w - 9.5f, t + 6}}, "#b0d068");
        }
    } else {
        return {};
    }
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + num(w) + "\" height=\"" + num(h) + "\" viewBox=\"0 0 " +
           num(w) + " " + num(h) + "\">" + body + "</svg>";
}

bool VectorUi::frame(od::Renderer& r, ui::Canvas& cv, int style, float x, float y, float w, float h, float s,
                     ui::Vec2 o) {
    char key[64];
    std::snprintf(key, sizeof key, "%d:%gx%g", style, double(w), double(h));
    Frame& fr = frames_[key];
    if (!fr.tried) {
        fr.tried = true;
        const std::string svg = frameSvg(style, w, h);
        if (!svg.empty()) {
            fr.svg = std::make_unique<ui::Svg>();
            if (!fr.svg->parse(svg)) fr.svg.reset();
        }
    }
    if (!fr.svg) return false;
    if (!fr.tex || std::fabs(fr.scale - s) > 1e-4f) {
        if (fr.tex) r.destroyTexture(fr.tex);
        fr.tex = 0;
        const std::vector<uint32_t> px = fr.svg->rasterize(s, fr.w, fr.h);
        if (px.empty()) return false;
        fr.tex = r.createTexture(fr.w, fr.h, px.data());
        fr.scale = s;
        if (!fr.tex) return false;
    }
    const ui::Vec2 a(std::floor(o.x + x * s + 0.5f), std::floor(o.y + y * s + 0.5f));
    cv.image(fr.tex, a, ui::Vec2(a.x + float(fr.w), a.y + float(fr.h)), ui::Vec2(0.0f, 0.0f), ui::Vec2(1.0f, 1.0f),
             ui::rgba(255, 255, 255));
    return true;
}

bool VectorUi::draw(od::Renderer& r, ui::Canvas& cv, uint16_t vec, float x, float y, float w, float h, float s,
                    ui::Vec2 o, uint32_t tint) {
    if (!enabled) return false;
    std::string itemFile;
    if (vec >= od::port::kVecItemIcon) itemFile = "icons/item_" + std::to_string(vec - od::port::kVecItemIcon) + ".svg";
    if (vec >= od::port::kVecFrame0 && vec <= od::port::kVecFrame2)
        return frame(r, cv, vec - od::port::kVecFrame0, x, y, w, h, s, o);
    Picture& p = pics_[vec];
    if (!p.tried) {
        p.tried = true;
        const char* f = itemFile.empty() ? fileOf(vec) : itemFile.c_str();
        if (f) {
            p.svg = std::make_unique<ui::Svg>();
            const auto path = assetsDir() / "ui" / f;
            if (!p.svg->load(path)) {
                SDL_Log("vector ui: cannot read %s (the pixel art stays; first at %g, %g)", path.string().c_str(),
                        double(x), double(y));
                p.svg.reset();
            }
        }
    }
    if (!p.svg) return false;
    // rasterized at the window's pixels per screen pixel: drawn 1:1, no resampling
    if (!p.tex || std::fabs(p.scale - s) > 1e-4f) {
        if (p.tex) r.destroyTexture(p.tex);
        p.tex = 0;
        const std::vector<uint32_t> px = p.svg->rasterize(s, p.w, p.h);
        if (px.empty()) return false;
        p.tex = r.createTexture(p.w, p.h, px.data());
        p.scale = s;
        if (!p.tex) return false;
    }
    const ui::Vec2 a(std::floor(o.x + x * s + 0.5f), std::floor(o.y + y * s + 0.5f));
    const uint32_t lv = std::min<uint32_t>(tint * 2u, 255u);
    cv.image(p.tex, a, ui::Vec2(a.x + float(p.w), a.y + float(p.h)), ui::Vec2(0.0f, 0.0f), ui::Vec2(1.0f, 1.0f),
             ui::rgba(lv, lv, lv));
    if (fonts(r)) pictureText(cv, vec, s, o);
    return true;
}

void VectorUi::pictureText(ui::Canvas& cv, uint16_t vec, float s, ui::Vec2 o) {
    if (vec == od::port::kVecStatusPanel) { // the play time's units on their tabs
        const float px = regular_.em(4.2f * s);
        const char* units[3] = {"h", "m", "s"};
        const float cx[3] = {127.0f, 160.0f, 191.0f};
        for (int k = 0; k < 3; ++k)
            regular_.draw(cv, units[k], o.x + cx[k] * s - regular_.width(units[k], px) * 0.5f, o.y + 126.3f * s, px,
                          ui::rgba(0xf8, 0xf8, 0xf8));
    }
}

bool VectorUi::label(od::Renderer& r, ui::Canvas& cv, const SP& p, float s, ui::Vec2 o) {
    if (!enabled || !fonts(r)) return false;
    auto P = [&](float x, float y) { return ui::Vec2(o.x + x * s, o.y + y * s); };
    // a caption in `font` with capitals cap px high (screen px), centred on cx, its capitals centred on cy; shrunk to
    // fit maxW (screen px) when given
    auto caption = [&](UiText& font, const std::string& t, float cx, float cy, float cap, ui::Color col, float edge,
                       float maxW = 0.0f) {
        float px = font.em(cap * s);
        if (maxW > 0.0f) {
            const float w = font.width(t, px);
            if (w > maxW * s) px *= maxW * s / w;
        }
        const float capPx = cap * s * (px / font.em(cap * s));
        font.draw(cv, t, o.x + cx * s - font.width(t, px) * 0.5f, o.y + cy * s + capPx * 0.5f, px, col, edge * s,
                  kTextEdge);
    };
    const float x = p.x, y = p.y, w = p.w, h = p.h;
    switch (p.label) {
    case od::port::kLblStatusTitle: {
        const std::string* t = word("status.title");
        if (!t) return false;
        for (const float rx : {x + 4.5f, x + w - 4.5f}) {
            cv.disc(P(rx, y + 8.0f), 3.6f * s, kDark);
            cv.disc(P(rx, y + 8.0f), 2.5f * s, kRing);
            cv.disc(P(rx, y + 8.0f), 1.5f * s, kDark);
        }
        caption(bold_, *t, x + w * 0.5f, y + 8.0f, 9.0f, kTealText, 0.75f, w - 20.0f);
        return true;
    }
    case od::port::kLblStatusLocation: {
        const std::string* t = word("status.location");
        if (!t) return false;
        caption(bold_, *t, x + w * 0.5f, y + 8.0f, 7.0f, kTealText, 0.75f, w + 8.0f);
        return true;
    }
    case od::port::kLblStatusRow: {
        const std::string* t = word("status.row." + std::to_string(p.labelArg & 0xf));
        if (!t) return false;
        const bool cur = (p.labelArg & 0x10) != 0;
        cv.rectFilled(P(x, y), P(x + w, y + h), cur ? kRowCursor : kRow);
        caption(bold_, *t, x + w * 0.5f, y + h * 0.5f, 8.0f, cur ? kRowTextCursor : kRowText, 0.0f, w - 6.0f);
        return true;
    }
    case od::port::kLblGaugeCell: {
        const bool life = p.labelArg == 1;
        // the cell sprite: its dark walls (x + 1, x + 6), the life's mint between them
        cv.rectFilled(P(x + 1.0f, y), P(x + 7.0f, y + h), kDark);
        cv.rectFilled(P(x + 2.0f, y), P(x + 6.0f, y + h), life ? ui::rgba(0x80, 0xe8, 0xb8) : ui::rgba(0x48, 0x90, 0x70));
        return true;
    }
    case od::port::kLblWeaponSwap:
    case od::port::kLblWeaponLifter: {
        const bool swap = p.label == od::port::kLblWeaponSwap;
        const std::string* t = word(swap ? "status.weapon" : "status.lifter");
        if (!t) return false;
        const ui::Color tc = (p.labelArg & 1) ? kBarTextDim : kBarText;
        cv.rectFilled(P(x, y + 1.0f), P(x + w, y + h - 1.0f), kBar);
        if (swap) {
            caption(regular_, *t, x + 16.0f + (w - 18.0f) * 0.5f, y + 8.0f, 7.0f, tc, 0.0f, w - 18.0f);
        } else { // the double arrow, then the word
            const float ay = y + 8.0f, ax0 = x + 2.0f, ax1 = x + 13.0f;
            cv.line(P(ax0 + 1.0f, ay), P(ax1 - 1.0f, ay), tc, 1.2f * s);
            for (const float d : {1.0f, -1.0f}) {
                const float tip = d > 0 ? ax0 : ax1;
                const ui::Vec2 pts[3] = {P(tip, ay), P(tip + d * 3.2f, ay - 2.6f), P(tip + d * 3.2f, ay + 2.6f)};
                cv.convexFillAA(pts, 3, tc);
            }
            caption(regular_, *t, x + 15.0f + (w - 16.0f) * 0.5f, y + 8.0f, 7.0f, tc, 0.0f, w - 16.0f);
        }
        return true;
    }
    case od::port::kLblPrompt: {
        const float gy = p.kind == SP::Glyph ? p.gy : y, gx = p.kind == SP::Glyph ? p.gx : x;
        drawPrompt(cv, bold_, uint16_t(1u << (p.labelArg & 0xf)), o.x + gx * s, o.y + (gy + 6.0f) * s, 11.0f * s);
        return true;
    }
    case od::port::kLblMenuBar: {
        const std::string* ok = word("bar.confirm");
        const std::string* no = word("bar.cancel");
        if (!ok || !no) return false;
        const float cap = 7.0f * s, px = bold_.em(cap), ph = 10.5f * s, gap = 2.5f * s, cy = o.y + (y + 8.0f) * s;
        // the confirm / cancel prompts' widths first (the bar is centred)
        ui::Canvas probe;
        probe.begin(1.0f, 1.0f);
        const float pw0 = drawPrompt(probe, bold_, 0x2000, 0.0f, 0.0f, ph);
        const float pw1 = drawPrompt(probe, bold_, 0x4000, 0.0f, 0.0f, ph);
        const float chev = 4.0f * s;
        const float total = chev + gap + pw0 + gap + bold_.width(*ok, px) + gap * 3.0f + pw1 + gap +
                            bold_.width(*no, px) + gap + chev;
        float cx = o.x + (x + w * 0.5f) * s - total * 0.5f;
        auto chevron = [&](float at, float dir) {
            cv.line(ui::Vec2(at + (dir > 0 ? chev : 0.0f), cy - 3.5f * s), ui::Vec2(at + (dir > 0 ? 0.0f : chev), cy),
                    kHint, 1.3f * s);
            cv.line(ui::Vec2(at + (dir > 0 ? 0.0f : chev), cy), ui::Vec2(at + (dir > 0 ? chev : 0.0f), cy + 3.5f * s),
                    kHint, 1.3f * s);
        };
        chevron(cx, 1.0f);
        cx += chev + gap;
        cx += drawPrompt(cv, bold_, 0x2000, cx, cy, ph) + gap;
        bold_.draw(cv, *ok, cx, cy + cap * 0.5f, px, kHint, 0.75f * s, kTextEdge);
        cx += bold_.width(*ok, px) + gap * 3.0f;
        cx += drawPrompt(cv, bold_, 0x4000, cx, cy, ph) + gap;
        bold_.draw(cv, *no, cx, cy + cap * 0.5f, px, kHint, 0.75f * s, kTextEdge);
        cx += bold_.width(*no, px) + gap;
        chevron(cx, -1.0f);
        return true;
    }
    case od::port::kLblPageTitle: {
        const std::string* t = word("page." + std::to_string(p.labelArg) + ".title");
        if (!t) return false;
        for (const float rx : {x + 4.5f, x + w - 4.5f}) {
            cv.disc(P(rx, y + 8.0f), 3.6f * s, kDark);
            cv.disc(P(rx, y + 8.0f), 2.5f * s, kRing);
            cv.disc(P(rx, y + 8.0f), 1.5f * s, kDark);
        }
        caption(bold_, *t, x + w * 0.5f, y + 8.0f, 9.0f, kTealText, 0.75f, w - 20.0f);
        return true;
    }
    case od::port::kLblPageRow: {
        char key[48];
        std::snprintf(key, sizeof key, "row.%u.%04x", p.labelRef >> 16, p.labelRef & 0xffff);
        const std::string* t = word(key);
        if (!t && missingAt_.insert(key).second) SDL_Log("vector ui: %s is the caption at %d, %d", key, p.x, p.y);
        if (!t) return false;
        const int state = p.labelArg >> 8; // 0 other, 1 the cursor's, 2 chosen (a deeper level), 3 another there
        const ui::Color bg = state == 1 || state == 2 ? kRowCursor : kRow;
        const ui::Color tc = state == 1   ? kRowTextCursor
                             : state == 2 ? ui::rgba(0xf0, 0xd8, 0xd8)
                             : state == 3 ? ui::rgba(0xa8, 0xa8, 0xb0)
                                          : kRowText;
        cv.rectFilled(P(x, y), P(x + w, y + h), bg);
        caption(bold_, *t, x + w * 0.5f, y + h * 0.5f, std::min(8.0f, h * 0.55f), tc, 0.0f, w - 6.0f);
        return true;
    }
    case od::port::kLblNone2: return true; // drawn by another label
    // the list's scroll bar (SUBSCN01's art, tpage 7 clut 0x7f53): one column 8 px wide -- the outline (#181818) 1 px
    // with 45-degree cuts at both ends (the art's 1-px staircases), the grey (#808080) rails 1 px, between them the
    // black channel 4 px, its corners at the end pieces #585058; the end pieces' dark-blue (#283850) arrows; the thumb
    // a green (#98c050) bar 2 px wide in the channel's middle
    case od::port::kLblScrollCap: {
        auto Q = [&](float dx, float dy) { return P(x + dx, y + dy); };
        const float r2 = 1.41421356f; // the outline's 1 px measured square to the cuts
        const ui::Color ink = ui::rgba(0x18, 0x18, 0x18), grey = ui::rgba(0x80, 0x80, 0x80),
                        arrow = ui::rgba(0x28, 0x38, 0x50), dim = ui::rgba(0x58, 0x50, 0x58);
        if (p.labelArg == 0) { // the top piece: rows 4 .. 15 (15 = the channel's top)
            const ui::Vec2 out[6] = {Q(2, 4), Q(6, 4), Q(8, 6), Q(8, 16), Q(0, 16), Q(0, 6)};
            const ui::Vec2 in[6] = {Q(1 + r2, 5), Q(7 - r2, 5), Q(7, 5 + r2), Q(7, 16), Q(1, 16), Q(1, 5 + r2)};
            const ui::Vec2 tri[3] = {Q(4, 9), Q(6, 11), Q(2, 11)};
            cv.convexFillAA(out, 6, ink, 0.0f);
            cv.convexFillAA(in, 6, grey, 0.0f);
            cv.convexFillAA(tri, 3, arrow, 0.0f);
            cv.rectFilled(Q(2, 15), Q(6, 16), ink);
            cv.rectFilled(Q(2, 15), Q(3, 16), dim);
            cv.rectFilled(Q(5, 15), Q(6, 16), dim);
        } else { // the bottom piece: rows 0 .. 11 (0 = the channel's bottom)
            const ui::Vec2 out[6] = {Q(0, 0), Q(8, 0), Q(8, 10), Q(6, 12), Q(2, 12), Q(0, 10)};
            const ui::Vec2 in[6] = {Q(1, 0), Q(7, 0), Q(7, 11 - r2), Q(7 - r2, 11), Q(1 + r2, 11), Q(1, 11 - r2)};
            const ui::Vec2 tri[3] = {Q(2, 5), Q(6, 5), Q(4, 7)};
            cv.convexFillAA(out, 6, ink, 0.0f);
            cv.convexFillAA(in, 6, grey, 0.0f);
            cv.convexFillAA(tri, 3, arrow, 0.0f);
            cv.rectFilled(Q(2, 0), Q(6, 1), ink);
            cv.rectFilled(Q(2, 0), Q(3, 1), dim);
            cv.rectFilled(Q(5, 0), Q(6, 1), dim);
        }
        return true;
    }
    case od::port::kLblScrollTrack: {
        const float tx = p.qx[0], ty = p.qy[0], th = float(p.qy[2] - p.qy[0]);
        cv.rectFilled(P(tx, ty), P(tx + 8, ty + th), ui::rgba(0x18, 0x18, 0x18));
        cv.rectFilled(P(tx + 1, ty), P(tx + 2, ty + th), ui::rgba(0x80, 0x80, 0x80));
        cv.rectFilled(P(tx + 6, ty), P(tx + 7, ty + th), ui::rgba(0x80, 0x80, 0x80));
        return true;
    }
    case od::port::kLblScrollThumb: { // the quad is 7 px over the texels 0xf8 .. 0xff: the bar at its px 3 .. 4
        const float tx = p.qx[0], ty = p.qy[0], th = float(p.qy[2] - p.qy[0]);
        if (th > 0) cv.rectFilled(P(tx + 3, ty), P(tx + 5, ty + th), ui::rgba(0x98, 0xc0, 0x50));
        return true;
    }
    case od::port::kLblScrollLit: { // the end piece's arrow in cyan (#68b0b8)
        auto Q = [&](float dx, float dy) { return P(x + dx, y + dy); };
        const ui::Vec2 up[3] = {Q(4, 1), Q(6, 3), Q(2, 3)}, down[3] = {Q(2, 5), Q(6, 5), Q(4, 7)};
        cv.convexFillAA(p.labelArg == 0 ? up : down, 3, ui::rgba(0x68, 0xb0, 0xb8), 0.0f);
        return true;
    }
    case od::port::kLblEquipHead: { // small white capitals outlined dark over the box's top edge; the body's caption
                                    // leads with its mark (a box with a cross: part of that caption's art)
        const std::string* t = word("equip.head." + std::to_string(p.labelArg & ~1u));
        if (!t) return false;
        float tx = x;
        const ui::Color mark = ui::rgba(0xc8, 0xc8, 0xc0), ink = ui::rgba(0x28, 0x20, 0x30);
        if ((p.labelArg & ~1u) == 2) {
            cv.rectFilled(P(tx, y + 1.0f), P(tx + 7.0f, y + 8.0f), ink);
            cv.rectFilled(P(tx + 1.0f, y + 2.0f), P(tx + 6.0f, y + 7.0f), mark);
            cv.rectFilled(P(tx + 2.0f, y + 3.0f), P(tx + 5.0f, y + 6.0f), ink);
            cv.line(P(tx + 2.0f, y + 3.0f), P(tx + 5.0f, y + 6.0f), mark, 0.9f * s);
            cv.line(P(tx + 5.0f, y + 3.0f), P(tx + 2.0f, y + 6.0f), mark, 0.9f * s);
            tx += 8.0f;
        }
        const float cap = 5.0f * s, px = bold_.em(cap);
        bold_.draw(cv, *t, o.x + tx * s, o.y + (y + 4.5f) * s + cap * 0.5f, px, ui::rgba(0xf8, 0xf8, 0xf8), 0.9f * s, ink);
        return true;
    }
    case od::port::kLblFrameArrow: { // the inner border bumping out by 3 (45-degree ends) round a brass arrow
        auto Q = [&](float dx, float dy) { return P(x + dx, y + dy); };
        const ui::Vec2 fillQ[4] = {Q(7, 2.5f), Q(10, 5.5f), Q(10, 10.5f), Q(7, 13.5f)};
        cv.convexFillAA(fillQ, 4, ui::rgba(0xb0, 0xe8, 0xa8), 0.0f);
        const ui::Color edge = ui::rgba(0x68, 0xa0, 0x78);
        const ui::Vec2 e0[4] = {Q(7, 2.5f), Q(8, 2.5f), Q(11, 5.5f), Q(10, 5.5f)};
        const ui::Vec2 e1[4] = {Q(10, 5.5f), Q(11, 5.5f), Q(11, 10.5f), Q(10, 10.5f)};
        const ui::Vec2 e2[4] = {Q(10, 10.5f), Q(11, 10.5f), Q(8, 13.5f), Q(7, 13.5f)};
        cv.convexFillAA(e0, 4, edge, 0.0f);
        cv.convexFillAA(e1, 4, edge, 0.0f);
        cv.convexFillAA(e2, 4, edge, 0.0f);
        const float ax = 6.0f, t = 5.0f; // the arrow: rows t .. t + 5, lit on both slants
        const ui::Vec2 g[6] = {Q(ax, t), Q(ax + 1.5f, t), Q(ax + 3, t + 1.5f), Q(ax + 3, t + 4.5f), Q(ax + 1.5f, t + 6),
                               Q(ax, t + 6)};
        cv.convexFillAA(g, 6, ui::rgba(0xb0, 0x98, 0x68), 0.0f);
        const ui::Vec2 h0[4] = {Q(ax + 0.5f, t), Q(ax + 1.5f, t), Q(ax + 3, t + 1.5f), Q(ax + 3, t + 2.5f)};
        const ui::Vec2 h1[4] = {Q(ax + 3, t + 3.5f), Q(ax + 3, t + 4.5f), Q(ax + 1.5f, t + 6), Q(ax + 0.5f, t + 6)};
        cv.convexFillAA(h0, 4, ui::rgba(0xb0, 0xd0, 0x68), 0.0f);
        cv.convexFillAA(h1, 4, ui::rgba(0xb0, 0xd0, 0x68), 0.0f);
        return true;
    }
    case od::port::kLblVolBox: // the box (1 px in at the left / top / bottom)
        cv.rectFilled(P(x + 1.0f, y + 1.0f), P(x + w, y + h - 1.0f), ui::rgba(0x40, 0x68, 0x60));
        return true;
    case od::port::kLblVolLevel: { // 16 bars of 2 px every 3 px rising a row each other bar, a shaded cap on each,
                                   // up to the sprite's width (the level)
        const float bx = x, by = y - 1.0f; // the box's sprite
        for (int k = 0; k < 16; ++k) {
            const float x0 = bx + 8.0f + 3.0f * float(k);
            if (x0 + 2.0f > bx + w) break;
            const float cap = by + 11.0f - float(k / 2);
            cv.rectFilled(P(x0, cap + 1.0f), P(x0 + 2.0f, by + 13.0f), ui::rgba(0x88, 0x90, 0x98));
            cv.rectFilled(P(x0, cap), P(x0 + 1.0f, cap + 1.0f), ui::rgba(0x58, 0x70, 0x70));
            cv.rectFilled(P(x0 + 1.0f, cap), P(x0 + 2.0f, cap + 1.0f), ui::rgba(0x70, 0x80, 0x80));
        }
        return true;
    }
    case od::port::kLblOptRail: { // the rail: 3 px light, a darker top line starting 7 px in (the left cap's cut)
        const float ry = float(p.qy[0]);
        cv.rectFilled(P(69.0f, ry + 1.0f), P(287.0f, ry + 4.0f), ui::rgba(0xb0, 0xd0, 0x68));
        cv.rectFilled(P(76.0f, ry), P(287.0f, ry + 1.0f), ui::rgba(0x84, 0xa8, 0x70));
        return true;
    }
    case od::port::kLblOptValue: { // a value box: the chosen one mauve, the others teal, the labels green
        char key[32];
        std::snprintf(key, sizeof key, "opt.%04x", p.labelRef & 0xffff);
        const std::string* t = word(key);
        if (!t) {
            if (missingAt_.insert(key).second) SDL_Log("vector ui: %s is the caption at %d, %d", key, p.x, p.y);
            return false;
        }
        const int kind = p.labelArg;
        static const ui::Color kBox[3] = {ui::rgba(0x40, 0x68, 0x60), ui::rgba(0x78, 0x50, 0x60), ui::rgba(0x30, 0x80, 0x48)};
        static const ui::Color kText[3] = {ui::rgba(0x88, 0x90, 0x98), ui::rgba(0xa0, 0x88, 0x88), ui::rgba(0xf0, 0xf0, 0xe8)};
        // the box inside its sprite: 1 px in at the sides, 2 at the top, 1 at the bottom (the rows keep their gaps)
        cv.rectFilled(P(x + 1.0f, y + 2.0f), P(x + w - 1.0f, y + h - 1.0f), kBox[kind]);
        caption(kind == 2 ? bold_ : regular_, *t, x + w * 0.5f, y + 2.0f + (h - 3.0f) * 0.5f, std::min(7.5f, h * 0.55f),
                kText[kind], 0.0f, w - 4.0f);
        return true;
    }
    case od::port::kLblPageTab: { // L <previous page>  /  <next page> R (the pages 3 map .. 6 options wrap)
        const int page = p.labelArg >> 8, right = p.labelArg & 1;
        const int other = right ? (page >= 6 ? 3 : page + 1) : (page <= 3 ? 6 : page - 1);
        const std::string* t = word("pagename." + std::to_string(other));
        if (!t) return false;
        const float cap = 7.5f * s, px = bold_.em(cap), ph = 10.0f * s, gap = 2.0f * s, cy = o.y + (y + 8.0f) * s;
        const float tw = bold_.width(*t, px);
        ui::Canvas probe;
        probe.begin(1.0f, 1.0f);
        const uint16_t bit = right ? 0x800 : 0x400;
        const float pw = drawPrompt(probe, bold_, bit, 0.0f, 0.0f, ph);
        float cx = right ? o.x + (x + w) * s - pw - gap - tw : o.x + x * s;
        if (!right) {
            cx += drawPrompt(cv, bold_, bit, cx, cy, ph) + gap;
            bold_.draw(cv, *t, cx, cy + cap * 0.5f, px, kTealText, 0.75f * s, kTextEdge);
        } else {
            bold_.draw(cv, *t, cx, cy + cap * 0.5f, px, kTealText, 0.75f * s, kTextEdge);
            drawPrompt(cv, bold_, bit, cx + tw + gap, cy, ph);
        }
        return true;
    }
    default: return false;
    }
}

} // namespace od::app
