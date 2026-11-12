#include "app/ui/vector_ui.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <initializer_list>
#include <string>
#include <utility>

#include <stb_image.h>

#include "app/ui/prompts.hpp"

namespace od::app {

using SP = od::port::ScreenPrim;

std::filesystem::path assetsDir() {
    const char* base = SDL_GetBasePath();
    return std::filesystem::path(base ? base : "") / "assets";
}

namespace {

const char* fileOf(uint16_t vec) {
    switch (vec) {
    case od::port::kVecStatusPanel: return "status.svg";
    case od::port::kVecOptJunction: return "options_junction.svg";
    case od::port::kVecDualShock: return "dualshock.svg";
    case od::port::kVecLanguage: return "language.svg";
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
    // Japanese: the Latin subsets lack the kana and kanji, both styles from the full M+ (Medium; no full ExtraBold);
    // Korean / Chinese (OpenDash's packs): Noto Sans KR / SC Medium, both styles
    // (Russian: the Cyrillic of Noto Sans KR)
    const std::string set = lang_ == "jp" || lang_ == "ko" || lang_ == "zh" ? lang_ : lang_ == "ru" ? std::string("ko") : lang_ == "zhhk" ? std::string("zhhk") : std::string();
    if (set != fontSet_ && (bold_.tried || regular_.tried)) {
        for (UiText* t : {&bold_, &regular_}) {
            if (t->tex) r.destroyTexture(t->tex);
            t->tex = 0;
            t->tried = false;
        }
    }
    fontSet_ = set;
    const auto dir = assetsDir() / "fonts";
    // (Hong Kong: Noto Sans HK when installed, else the Simplified one, which has the Traditional characters too)
    const auto hk = std::filesystem::exists(dir / "NotoSansHK-Medium.otf") ? dir / "NotoSansHK-Medium.otf" : dir / "NotoSansSC-Medium.otf";
    const auto full = set == "ko"   ? dir / "NotoSansKR-Medium.otf"
                      : set == "zhhk" ? hk
                      : set == "zh" ? dir / "NotoSansSC-Medium.otf"
                                    : dir / "MPLUSRounded1c-Medium.ttf";
    if (!bold_.tried) bold_.load(r, !set.empty() ? full : dir / "MPLUSRounded1c-ExtraBold-Latin.ttf");
    if (!regular_.tried) regular_.load(r, !set.empty() ? full : dir / "MPLUSRounded1c-Medium-Latin.ttf");
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
    // (Doni Arts' HD style, Herbert: a clean ring -- the dark disc, the mint ring, the dark centre)
    std::string r = "<circle cx=\"" + num(cx) + "\" cy=\"" + num(cy) + "\" r=\"4.6\" fill=\"#33475a\"/>";
    r += "<circle cx=\"" + num(cx) + "\" cy=\"" + num(cy) + "\" r=\"2.9\" fill=\"#2a3a48\" stroke=\"#80e8b8\" stroke-width=\"1.3\"/>";
    return r;
}

// a polygon with its corners cut by c (an octagon on a rect)
std::string cutRect(float x0, float y0, float x1, float y1, float c, const char* fill) {
    return pathOf({{x0 + c, y0}, {x1 - c, y0}, {x1, y0 + c}, {x1, y1 - c}, {x1 - c, y1}, {x0 + c, y1}, {x0, y1 - c},
                   {x0, y0 + c}},
                  fill);
}
// an octagonal badge (the zenny's Z, the clock) centred on (cx, cy)
std::string badge(float cx, float cy, bool clock) {
    const float r = 5.2f, k = r * 0.4142f;
    std::string b = "<path d=\"M" + num(cx - k) + " " + num(cy - r) + " L" + num(cx + k) + " " + num(cy - r) + " L" +
                    num(cx + r) + " " + num(cy - k) + " L" + num(cx + r) + " " + num(cy + k) + " L" + num(cx + k) + " " +
                    num(cy + r) + " L" + num(cx - k) + " " + num(cy + r) + " L" + num(cx - r) + " " + num(cy + k) + " L" +
                    num(cx - r) + " " + num(cy - k) + " Z\" fill=\"" + (clock ? "#c8a4e8" : "#e8c888") +
                    "\" stroke=\"#33475a\" stroke-width=\"0.9\"/>";
    if (clock) {
        b += "<path d=\"M" + num(cx - 0.4f) + " " + num(cy - 2.8f) + " L" + num(cx - 0.4f) + " " + num(cy + 1.0f) + " L" +
             num(cx + 2.4f) + " " + num(cy - 0.6f) + "\" fill=\"none\" stroke=\"#33475a\" stroke-width=\"1.1\" "
             "stroke-linejoin=\"round\" stroke-linecap=\"round\"/>";
    } else {
        b += "<path d=\"M" + num(cx - 2.2f) + " " + num(cy - 2.4f) + " L" + num(cx + 2.2f) + " " + num(cy - 2.4f) + " L" +
             num(cx - 2.2f) + " " + num(cy + 2.4f) + " L" + num(cx + 2.2f) + " " + num(cy + 2.4f) +
             "\" fill=\"none\" stroke=\"#33475a\" stroke-width=\"1.1\" stroke-linejoin=\"round\"/>";
    }
    return b;
}
// a brass tick of the green panels' rails (x0 .. x1, its top y; 4.4 tall, lit on its top edge)
std::string tick(float x0, float x1, float y) {
    return pathOf({{x0, y}, {x1, y}, {x1, y + 4.4f}, {x0, y + 4.4f}}, "#c09a6a") +
           pathOf({{x0, y}, {x1, y}, {x1, y + 0.9f}, {x0, y + 0.9f}}, "#dcc08a");
}

// OpenDash (Herbert: Doni Arts' HD menu, doni-arts-megaman-l2-hd-mockup-v2-16x9.jpg): the status screen's panels
// traced from the mockup -- drawn over the game's layout at 4.5x (x 240 + 4.5 x, y 4.5 y on its 1920 x 1080), so its
// shapes land where the game puts the rows, the zenny, the time, the area, the weapon and the gauge. grown: the rows'
// box 16 px taller (the sixth row)
std::string statusSvgBody(bool grown) {
    const char* dark = "#33475a";
    const char* green = "#4a9070";
    const char* light = "#b2eaa8";
    std::string b;
    // the rows' panel (left)
    const float lb = 135.5f + (grown ? 16.0f : 0.0f);
    b += cutRect(15.6f, 32.2f, 80.0f, lb, 5.0f, dark);
    b += cutRect(18.0f, 34.6f, 77.6f, lb - 2.4f, 3.8f, green);
    b += pathOf({{30.0f, 37.6f}, {74.6f, 37.6f}, {75.6f, 38.6f}, {75.6f, lb - 6.5f}, {73.6f, lb - 4.8f}, {21.0f, lb - 4.8f},
                 {21.0f, lb - 30.0f}, {26.4f, lb - 30.0f}, {26.4f, 41.2f}},
                light);
    for (float y = 42.2f; y + 4.4f <= lb - 8.0f; y += 7.9f) b += tick(68.9f, 73.3f, y);
    // the gauge's panel (right)
    b += cutRect(239.3f, 32.2f, 303.8f, 161.1f, 5.0f, dark);
    b += cutRect(241.7f, 34.6f, 301.4f, 158.7f, 3.8f, green);
    b += pathOf({{243.8f, 37.8f}, {294.0f, 37.8f}, {296.5f, 40.3f}, {296.5f, 43.0f}, {292.0f, 44.6f}, {292.0f, 150.0f},
                 {296.5f, 151.6f}, {296.5f, 154.4f}, {294.0f, 156.9f}, {243.8f, 156.9f}},
                light);
    for (float y = 42.2f; y + 4.4f <= 152.0f; y += 7.9f) b += tick(246.0f, 250.4f, y);
    for (const float y : {41.0f, 91.0f, 148.0f}) // the brass arrows on the inner rail
        b += pathOf({{289.4f, y}, {292.0f, y + 2.0f}, {289.4f, y + 4.0f}}, "#c09a6a");
    // (Herbert: the panel's purpose) the life gauge's tube -- the game draws its cells at x 260 .. 268, from y 117 up
    // 8 a bar (up to 10) --, the orb at its foot, and the column of the 4 status slots (its icons at x 272, y 64 + 16 k)
    b += cutRect(255.0f, 39.5f, 274.0f, 43.5f, 1.2f, dark);  // the cap
    b += cutRect(256.5f, 41.0f, 271.5f, 128.0f, 1.5f, dark); // the tube (10 bars: the top cell at y 45)
    b += pathOf({{258.6f, 43.4f}, {269.4f, 43.4f}, {269.4f, 125.6f}, {258.6f, 125.6f}}, "#26313d"); // its inside
    for (float y = 45.0f; y < 125.0f; y += 8.0f) // the seams between the game's cells (7 tall, 8 apart)
        b += pathOf({{258.6f, y + 7.0f}, {269.4f, y + 7.0f}, {269.4f, y + 8.0f}, {258.6f, y + 8.0f}}, "#1c242e");
    b += pathOf({{260.5f, 127.5f}, {268.5f, 127.5f}, {268.5f, 131.0f}, {260.5f, 131.0f}}, dark); // the neck
    b += "<circle cx=\"264.5\" cy=\"140\" r=\"10.6\" fill=\"#50566e\" stroke=\"#33475a\" stroke-width=\"1.4\"/>";
    b += "<circle cx=\"264.5\" cy=\"140\" r=\"7.2\" fill=\"#2a8a68\" stroke=\"#33475a\" stroke-width=\"0.8\"/>";
    b += "<circle cx=\"264.5\" cy=\"140\" r=\"5.0\" fill=\"#c8e070\"/>";
    b += "<circle cx=\"264.5\" cy=\"140\" r=\"2.6\" fill=\"none\" stroke=\"#2a8a68\" stroke-width=\"1.2\"/>";
    b += "<path d=\"M264.5 135 V145 M259.5 140 H269.5\" stroke=\"#2a8a68\" stroke-width=\"0.8\"/>";
    for (const float bx : {255.2f, 273.8f}) b += "<circle cx=\"" + num(bx) + "\" cy=\"134.5\" r=\"1.3\" fill=\"#33475a\"/>";
    b += cutRect(271.0f, 61.5f, 289.0f, 131.0f, 2.0f, "#50566e"); // the slots' column
    for (int k = 0; k < 4; ++k) {
        const float y = 64.0f + 16.0f * float(k);
        b += cutRect(272.4f, y + 0.4f, 287.6f, y + 15.0f, 1.6f, dark);
    }
    // the area's plate
    b += cutRect(87.8f, 44.4f, 232.2f, 74.4f, 3.0f, dark);
    // the zenny's and the time's bars: rails, a plate with pointed ends, a badge at each end
    for (const float cy : {87.8f, 112.2f}) {
        b += pathOf({{103.3f, cy - 10.0f}, {216.7f, cy - 10.0f}, {216.7f, cy - 8.4f}, {103.3f, cy - 8.4f}}, dark);
        b += pathOf({{103.3f, cy + 8.4f}, {216.7f, cy + 8.4f}, {216.7f, cy + 10.0f}, {103.3f, cy + 10.0f}}, dark);
        b += pathOf({{110.5f, cy - 8.4f}, {209.5f, cy - 8.4f}, {214.0f, cy}, {209.5f, cy + 8.4f}, {110.5f, cy + 8.4f},
                     {106.0f, cy}},
                    dark);
        const bool clock = cy > 100.0f;
        b += badge(104.0f, cy, clock) + badge(216.0f, cy, clock);
    }
    // the play time's unit tabs (their letters: pictureText)
    for (const float cx : {127.0f, 160.0f, 191.0f}) b += cutRect(cx - 6.0f, 119.4f, cx + 6.0f, 128.0f, 1.2f, dark);
    // the weapon's plate
    b += cutRect(90.0f, 130.0f, 230.0f, 158.9f, 3.0f, dark);
    return b;
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
        // the list frame in Doni Arts' HD style: the dark plate's outline (4.5 cuts, the 1 px lip under the middle and
        // on the right as the game's), its inside as before
        body += pathOf({{4.5f, 0}, {w - 4.5f, 0}, {w, 4.5f}, {w, h - 5.5f}, {w - 4.5f, h - 1}, {w - 8, h - 1},
                        {w - 8, h}, {8, h}, {8, h - 1}, {4.5f, h - 1}, {0, h - 5.5f}, {0, 4.5f}},
                       "#33475a");
        body += pathOf({{5.4f, 2}, {w - 5.4f, 2}, {w - 2, 5.4f}, {w - 2, h - 6.4f}, {w - 5.4f, h - 3}, {5.4f, h - 3},
                        {2, h - 6.4f}, {2, 5.4f}},
                       "#383040");
    } else if (style == 2) {
        // the green frame (the options panel, the left lists) in Doni Arts' HD style (Herbert): the dark outline with
        // 5-unit cuts (rows 1 .. h - 2, the 2 px lip under the middle, x 16 .. w - 16, where the options' junction
        // meets it), the green band, the light inside where the game's was (x 11 .. w - 9, y 5 .. h - 5) with a deeper
        // green strip down its left, the brass ticks down the right band
        body += pathOf({{5, 1}, {w - 5, 1}, {w, 6}, {w, h - 6}, {w - 5, h - 1}, {w - 16, h - 1}, {w - 16, h}, {16, h},
                        {16, h - 1}, {5, h - 1}, {0, h - 6}, {0, 6}},
                       "#33475a");
        body += pathOf({{5.8f, 3.2f}, {w - 5.8f, 3.2f}, {w - 2.2f, 6.8f}, {w - 2.2f, h - 6.8f}, {w - 5.8f, h - 3.2f},
                        {5.8f, h - 3.2f}, {2.2f, h - 6.8f}, {2.2f, 6.8f}},
                       "#4a9070");
        body += pathOf({{14, 5}, {w - 10.5f, 5}, {w - 9, 6.5f}, {w - 9, h - 6.5f}, {w - 10.5f, h - 5}, {11, h - 5},
                        {11, 8}},
                       "#b2eaa8");
        body += pathOf({{11, h - 5}, {11, 8}, {14, 5}, {14, h - 5}}, "#8fd49a"); // (the deeper strip)
        for (float t = 7.0f; t + 4.0f <= h - 6.0f; t += 7.9f) { // the brass ticks, lit on their top edge
            body += pathOf({{w - 7.6f, t}, {w - 3.8f, t}, {w - 3.8f, t + 4.0f}, {w - 7.6f, t + 4.0f}}, "#c09a6a");
            body += pathOf({{w - 7.6f, t}, {w - 3.8f, t}, {w - 3.8f, t + 0.9f}, {w - 7.6f, t + 0.9f}}, "#dcc08a");
        }
    } else if (style == 3 || style == 4) {
        body = statusSvgBody(style == 4);
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

void VectorUi::text(od::Renderer& r, ui::Canvas& cv, const std::string& t, float cx, float y, float px, ui::Color c) {
    if (!fonts(r) || t.empty()) return;
    float size = px;
    const float w = regular_.width(t, size), maxW = cx * 1.9f;
    if (w > maxW && w > 0.0f) size *= maxW / w; // (a long caption: smaller, on one line)
    regular_.draw(cv, t, cx - regular_.width(t, size) * 0.5f, y, size, c, std::max(1.0f, size * 0.08f), ui::rgba(8, 8, 16));
}

void VectorUi::textLeft(od::Renderer& r, ui::Canvas& cv, const std::string& t, float x, float y, float px, ui::Color c,
                        float maxW, bool bold) {
    if (!fonts(r) || t.empty()) return;
    UiText& f = bold ? bold_ : regular_;
    float size = px;
    const float w = f.width(t, size);
    if (maxW > 0.0f && w > maxW && w > 0.0f) size *= maxW / w;
    f.draw(cv, t, x, y, size, c, std::max(1.0f, size * 0.08f), ui::rgba(8, 8, 16));
}

bool VectorUi::backdrop(od::Renderer& r, ui::Canvas& cv, float s, ui::Vec2 o, float extra) {
    Bitmap& b = backdrop_;
    if (!b.tried) {
        b.tried = true;
        std::ifstream f(assetsDir() / "ui" / "backdrop_tile.png", std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        int w = 0, h = 0, comp = 0;
        if (stbi_uc* px = bytes.empty() ? nullptr : stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &comp, 4)) {
            // a texel of the opposite edge round it (the tile repeats: the sampler blends across its seams)
            const int W = w + 2, H = h + 2;
            b.px.resize(size_t(W) * size_t(H) * 4);
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    const int sx = (x - 1 + w) % w, sy = (y - 1 + h) % h;
                    std::memcpy(&b.px[(size_t(y) * size_t(W) + size_t(x)) * 4], px + (size_t(sy) * size_t(w) + size_t(sx)) * 4, 4);
                }
            b.w = W;
            b.h = H;
            stbi_image_free(px);
            b.tex = r.createTexture(W, H, reinterpret_cast<const uint32_t*>(b.px.data()));
        }
    }
    if (!b.tex) return false;
    // the tile's sides on the 240-line screen (Doni Arts' diagonal sheet: 243 x 319 px of 1080 lines), the x axis
    // up-right, the y axis down-right; it moves down-right 30 px (of 240 lines) a second
    const float tw = 54.0f * s, th = 71.0f * s, c = 0.70710678f;
    const ui::Vec2 A(tw * c, -tw * c), B(th * c, th * c);
    const float t = float(SDL_GetTicks() % 600000u) / 1000.0f;
    const float along = std::fmod(t * 30.0f * s * 1.41421356f, th); // along B
    const ui::Vec2 origin(o.x + B.x * along / th, o.y + B.y * along / th);
    const float x0 = o.x - extra * s, x1 = o.x + (320.0f + extra) * s, y0 = o.y, y1 = o.y + 240.0f * s;
    float imin = 1e9f, imax = -1e9f, jmin = 1e9f, jmax = -1e9f;
    for (const ui::Vec2 q : {ui::Vec2(x0, y0), ui::Vec2(x1, y0), ui::Vec2(x0, y1), ui::Vec2(x1, y1)}) {
        const float dx = q.x - origin.x, dy = q.y - origin.y;
        const float i = (dx * A.x + dy * A.y) / (tw * tw), j = (dx * B.x + dy * B.y) / (th * th);
        imin = std::min(imin, i), imax = std::max(imax, i), jmin = std::min(jmin, j), jmax = std::max(jmax, j);
    }
    const float iw = 1.0f / float(b.w - 2), ih = 1.0f / float(b.h - 2); // a texel of the tile, as a share of its side
    for (int j = int(std::floor(jmin)) - 1; j <= int(std::ceil(jmax)); ++j)
        for (int i = int(std::floor(imin)) - 1; i <= int(std::ceil(imax)); ++i) {
            // the quad of the bordered texture: one texel past the tile on every side
            const float a0 = float(i) - iw, a1 = float(i + 1) + iw, b0 = float(j) - ih, b1 = float(j + 1) + ih;
            auto at = [&](float a, float bb) { return ui::Vec2(origin.x + A.x * a + B.x * bb, origin.y + A.y * a + B.y * bb); };
            cv.imageQuad(b.tex, at(a0, b0), at(a1, b0), at(a1, b1), at(a0, b1), ui::Vec2(0.0f, 0.0f), ui::Vec2(1.0f, 0.0f),
                         ui::Vec2(1.0f, 1.0f), ui::Vec2(0.0f, 1.0f), ui::rgba(255, 255, 255));
        }
    return true;
}

bool VectorUi::padPicture(od::Renderer& r, ui::Canvas& cv, float s, ui::Vec2 o) {
    static const char* const kFiles[4] = {"dualshock.png", "xbox.png", "steamdeck.png", "switch.png"};
    const int style = buttonStyle();
    Bitmap& b = pads_[style];
    if (!b.tried) {
        b.tried = true;
        std::ifstream f(assetsDir() / "ui" / kFiles[style], std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        int w = 0, h = 0, comp = 0;
        if (stbi_uc* px = bytes.empty() ? nullptr : stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &comp, 4)) {
            b.px.assign(px, px + size_t(w) * size_t(h) * 4);
            b.w = w;
            b.h = h;
            stbi_image_free(px);
        }
    }
    if (b.px.empty()) return style != 0; // another style without its picture: none (not the DualShock)
    // centred in the lists' panel (0x14 .. 0x12c x 0x37 .. 0x97), 0x5c tall
    const float hh = 92.0f, ww = hh * float(b.w) / float(b.h), cx = 160.0f, cy = 103.0f;
    const int tw = std::max(1, int(std::lround(ww * s))), th = std::max(1, int(std::lround(hh * s)));
    if (!b.tex || b.tw != tw || b.th != th) {
        if (b.tex) r.destroyTexture(b.tex);
        // area average with premultiplied alpha (no fringe), straight alpha out
        std::vector<uint32_t> out(size_t(tw) * size_t(th));
        const float fx = float(b.w) / float(tw), fy = float(b.h) / float(th);
        for (int y = 0; y < th; ++y) {
            const int y0 = int(y * fy), y1 = std::max(y0 + 1, std::min(b.h, int((y + 1) * fy)));
            for (int x = 0; x < tw; ++x) {
                const int x0 = int(x * fx), x1 = std::max(x0 + 1, std::min(b.w, int((x + 1) * fx)));
                double acc[4] = {0, 0, 0, 0};
                for (int sy = y0; sy < y1; ++sy)
                    for (int sx = x0; sx < x1; ++sx) {
                        const uint8_t* q = &b.px[(size_t(sy) * size_t(b.w) + size_t(sx)) * 4];
                        const double a = q[3] / 255.0;
                        acc[0] += q[0] * a, acc[1] += q[1] * a, acc[2] += q[2] * a, acc[3] += a;
                    }
                const double n = double((y1 - y0) * (x1 - x0)), a = acc[3] / n;
                uint32_t pr = 0, pg = 0, pb = 0;
                if (acc[3] > 0) {
                    pr = uint32_t(std::lround(acc[0] / acc[3]));
                    pg = uint32_t(std::lround(acc[1] / acc[3]));
                    pb = uint32_t(std::lround(acc[2] / acc[3]));
                }
                out[size_t(y) * size_t(tw) + size_t(x)] = pr | pg << 8 | pb << 16 | uint32_t(std::lround(a * 255.0)) << 24;
            }
        }
        b.tex = r.createTexture(tw, th, out.data());
        b.tw = tw;
        b.th = th;
    }
    if (!b.tex) return false;
    const ui::Vec2 a(std::floor(o.x + (cx - ww * 0.5f) * s + 0.5f), std::floor(o.y + (cy - hh * 0.5f) * s + 0.5f));
    cv.image(b.tex, a, ui::Vec2(a.x + float(tw), a.y + float(th)), ui::Vec2(0.0f, 0.0f), ui::Vec2(1.0f, 1.0f),
             ui::rgba(255, 255, 255));
    return true;
}

VectorUi::Picture* VectorUi::raster(od::Renderer& r, uint16_t vec, float s) {
    Picture& p = pics_[vec];
    if (!p.tried) {
        p.tried = true;
        if (const char* f = fileOf(vec)) {
            p.svg = std::make_unique<ui::Svg>();
            if (!p.svg->load(assetsDir() / "ui" / f)) p.svg.reset();
        }
    }
    if (!p.svg) return nullptr;
    if (!p.tex || std::fabs(p.scale - s) > 1e-4f) {
        if (p.tex) r.destroyTexture(p.tex);
        p.tex = 0;
        const std::vector<uint32_t> px = p.svg->rasterize(s, p.w, p.h);
        if (px.empty()) return nullptr;
        p.tex = r.createTexture(p.w, p.h, px.data());
        p.scale = s;
    }
    return p.tex ? &p : nullptr;
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
            bold_.draw(cv, units[k], o.x + cx[k] * s - bold_.width(units[k], px) * 0.5f, o.y + 126.3f * s, px,
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
        cap *= textScale; // the text size (MessageUi::textScale)
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
        // (Herbert: the HD tube's width) the cell across the tube's inside (x - 0.6 .. x + 8.6), exactly the game's
        // rows (y .. y + h: the part of a last cell too); the life's mint lit on its left, the capacity a dark teal
        const float l = x, r = x + 8.0f; // (as the approved preview: the 8 px cell, the tube's inside round it)
        cv.rectFilled(P(l, y), P(r, y + h), life ? ui::rgba(0x6a, 0xe8, 0xc0) : ui::rgba(0x3a, 0x5a, 0x58));
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
    case od::port::kLblPadPicture: // the DualShock (Herbert's dualshock.png, else dualshock.svg), the panel's veil over it
        if (padPicture(r, cv, s, o)) return true;
        return draw(r, cv, od::port::kVecDualShock, x, y, w, h, s, o, 0x80);
    case od::port::kLblPadButton:
        drawPadCode(cv, bold_, int(p.labelArg), o.x + x * s, o.y + (y + 8.0f) * s, 13.0f * s, buttonStyleDevice());
        return true;
    case od::port::kLblBackdrop: return backdrop(r, cv, s, o, float(p.labelRef));
    case od::port::kLblBackdropRest: return backdrop_.tex != 0; // (drawn with the first tile)
    case od::port::kLblBarrier: { // the green octagon badge, its words (barrier.<arg>) in two lines, white outlined
        const std::string* t = word("barrier." + std::to_string(p.labelArg & 1));
        if (!t) return false;
        const float cx = x + 16.0f, cy = y + 16.0f, r = 15.0f;
        auto oct = [&](float rr, ui::Color c) {
            const float kk = rr * 0.4142f;
            const ui::Vec2 q[8] = {P(cx - kk, cy - rr), P(cx + kk, cy - rr), P(cx + rr, cy - kk), P(cx + rr, cy + kk),
                                   P(cx + kk, cy + rr), P(cx - kk, cy + rr), P(cx - rr, cy + kk), P(cx - rr, cy - kk)};
            cv.convexFillAA(q, 8, c);
        };
        oct(r, ui::rgba(0x2a, 0x5a, 0x48));
        oct(r - 1.6f, ui::rgba(0x58, 0xa8, 0x80));
        oct(r - 3.2f, ui::rgba(0x48, 0x98, 0x70));
        // the words: split at the first space into two lines, each fitted to the badge's width
        std::string a = *t, b;
        if (const size_t sp = a.find(' '); sp != std::string::npos) b = a.substr(sp + 1), a.resize(sp);
        const float maxW = 24.0f * s, cap = 4.6f * s;
        const ui::Color ink = ui::rgba(0x18, 0x30, 0x28), white = ui::rgba(0xf8, 0xf8, 0xf0);
        auto line = [&](const std::string& w, float ly) {
            float px = bold_.em(cap);
            const float ww = bold_.width(w, px);
            if (ww > maxW) px *= maxW / ww;
            bold_.draw(cv, w, o.x + cx * s - bold_.width(w, px) * 0.5f, o.y + ly * s + cap * 0.5f, px, white, 0.9f * s, ink);
        };
        if (b.empty()) line(a, cy);
        else line(a, cy - 3.6f), line(b, cy + 3.6f);
        return true;
    }
    case od::port::kLblVideoIcon: { // the screen row's icons (Herbert): a dark tile with a mint rim, a white drawing
        const float w = 25.0f, h = 25.0f, lw = std::max(1.0f, 1.4f * s);
        const ui::Color ink = ui::rgba(0xf0, 0xf4, 0xf0), soft = ui::rgba(0x80, 0xe8, 0xb8);
        cv.rectFilled(P(x, y), P(x + w, y + h), kDark);
        auto L = [&](float ax, float ay, float bx, float by, ui::Color c) { cv.line(P(x + ax, y + ay), P(x + bx, y + by), c, lw); };
        auto box = [&](float x0, float y0, float x1, float y1, ui::Color c) {
            L(x0, y0, x1, y0, c), L(x1, y0, x1, y1, c), L(x1, y1, x0, y1, c), L(x0, y1, x0, y0, c);
        };
        box(0.5f, 0.5f, w - 0.5f, h - 0.5f, soft);
        switch (p.labelArg & 3) {
        case 0: // the anti-aliasing: a stepped edge (left) beside a smooth one (right)
            for (int k = 0; k < 4; ++k) {
                const float sx = 4.0f + float(k) * 2.5f, sy = 20.0f - float(k) * 3.5f;
                L(sx, sy, sx + 2.5f, sy, soft);
                L(sx + 2.5f, sy, sx + 2.5f, sy - 3.5f, soft);
            }
            L(13.5f, 20.0f, 21.0f, 5.5f, ink);
            break;
        case 1: // the resolution: a screen, its corners' arrows pointing out
            box(6.0f, 8.0f, 19.0f, 16.0f, ink);
            L(10.0f, 19.5f, 15.0f, 19.5f, ink);
            for (const auto& [cx, cy, dx, dy] : {std::array<float, 4>{3.0f, 3.0f, 1, 1}, std::array<float, 4>{22.0f, 3.0f, -1, 1},
                                                std::array<float, 4>{3.0f, 22.0f, 1, -1}, std::array<float, 4>{22.0f, 22.0f, -1, -1}}) {
                L(cx, cy, cx + 3.0f * dx, cy, soft);
                L(cx, cy, cx, cy + 3.0f * dy, soft);
            }
            break;
        default: // the display mode: a window, its title bar and buttons
            box(4.0f, 5.5f, 21.0f, 19.5f, ink);
            L(4.0f, 9.0f, 21.0f, 9.0f, ink);
            for (const float bx : {15.0f, 17.5f, 20.0f}) cv.disc(P(x + bx - 0.5f, y + 7.25f), 0.7f * s, soft);
            break;
        }
        return true;
    }
    case od::port::kLblFlag: { // the flag right-aligned in the box, as high as the box (x the text size), a thin dark edge
        static const char* const kCodes[11] = {"jp", "en", "ptbr", "es", "fr", "it", "de", "ko", "zh", "zhtw", "ru"};
        const int b = int(p.labelArg);
        if (b < 0 || b > 10) return true; // (9: Traditional Chinese, Hong Kong's flag; 10 Russian)
        Picture& f = flags_[b];
        if (!f.tried) {
            f.tried = true;
            f.svg = std::make_unique<ui::Svg>();
            if (!f.svg->load(assetsDir() / "ui" / "flags" / (std::string(kCodes[b]) + ".svg")) || f.svg->height() <= 0.0f)
                f.svg.reset();
        }
        if (!f.svg) return true; // (no flag: only the name)
        const float fh = std::floor(h * textScale * s + 0.5f), sc = fh / f.svg->height();
        if (!f.tex || std::fabs(f.scale - sc) > 1e-4f) {
            if (f.tex) r.destroyTexture(f.tex);
            f.tex = 0;
            const std::vector<uint32_t> px = f.svg->rasterize(sc, f.w, f.h);
            if (px.empty()) return true;
            f.tex = r.createTexture(f.w, f.h, px.data());
            f.scale = sc;
        }
        if (!f.tex) return true;
        const ui::Vec2 a(std::floor(o.x + (x + w) * s - float(f.w) + 0.5f),
                         std::floor(o.y + (y + h * 0.5f) * s - float(f.h) * 0.5f + 0.5f));
        const float e = std::max(1.0f, std::floor(0.5f * s));
        cv.rectFilled(ui::Vec2(a.x - e, a.y - e), ui::Vec2(a.x + float(f.w) + e, a.y + float(f.h) + e),
                      ui::rgba(0x18, 0x18, 0x20));
        cv.image(f.tex, a, ui::Vec2(a.x + float(f.w), a.y + float(f.h)), ui::Vec2(0.0f, 0.0f), ui::Vec2(1.0f, 1.0f),
                 ui::rgba(0xff, 0xff, 0xff));
        return true;
    }
    case od::port::kLblPrompt: {
        const float gy = p.kind == SP::Glyph ? p.gy : y, gx = p.kind == SP::Glyph ? p.gx : x;
        drawPrompt(cv, bold_, uint16_t(1u << (p.labelArg & 0xf)), o.x + gx * s, o.y + (gy + 6.0f) * s, 11.0f * s);
        return true;
    }
    case od::port::kLblMenuBar: {
        // labelArg 1 (OpenDash, the Extras' sound test, Herbert): "circle Play >" and "cross Stop []"
        const bool player = (p.labelArg & 0xff) == 1;
        static const std::string kPlay = "Play", kStop = "Stop";
        const std::string* ok = player ? (word("bar.play") ? word("bar.play") : &kPlay) : word("bar.confirm");
        const std::string* no = player ? (word("bar.stop") ? word("bar.stop") : &kStop) : word("bar.cancel");
        if (!ok || !no) return false;
        const float cap = 7.0f * s * textScale, px = bold_.em(cap), ph = 10.5f * s, gap = 2.5f * s, cy = o.y + (y + 8.0f) * s;
        // the confirm / cancel prompts' widths first (the bar is centred)
        ui::Canvas probe;
        probe.begin(1.0f, 1.0f);
        const float pw0 = drawPrompt(probe, bold_, 0x2000, 0.0f, 0.0f, ph);
        const float pw1 = drawPrompt(probe, bold_, 0x4000, 0.0f, 0.0f, ph);
        const float chev = 4.0f * s;
        const float icon = player ? cap + gap : 0.0f; // the play / stop icon after its word (the capitals' height)
        const float total = chev + gap + pw0 + gap + bold_.width(*ok, px) + icon + gap * 3.0f + pw1 + gap +
                            bold_.width(*no, px) + icon + gap + chev;
        auto playIcon = [&](float at, bool stop) { // at: its left; centred on the capitals
            if (stop) {
                cv.rectFilled(ui::Vec2(at, cy - cap * 0.5f + 0.5f * s), ui::Vec2(at + cap - s, cy + cap * 0.5f - 0.5f * s),
                              kHint);
            } else {
                const ui::Vec2 tri[3] = {ui::Vec2(at, cy - cap * 0.5f), ui::Vec2(at + cap * 0.9f, cy),
                                         ui::Vec2(at, cy + cap * 0.5f)};
                cv.convexFillAA(tri, 3, kHint);
            }
        };
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
        cx += bold_.width(*ok, px);
        if (player) playIcon(cx + gap, false), cx += icon;
        cx += gap * 3.0f;
        cx += drawPrompt(cv, bold_, 0x4000, cx, cy, ph) + gap;
        bold_.draw(cv, *no, cx, cy + cap * 0.5f, px, kHint, 0.75f * s, kTextEdge);
        cx += bold_.width(*no, px);
        if (player) playIcon(cx + gap, true), cx += icon;
        cx += gap;
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
        // OpenDash (Herbert): the language row leads with a globe, so the row is found in a language one cannot read
        if (w <= 1.0f) return true; // (a row with no art of its own: its caption is a text one, drawn apart)
        if (std::strcmp(key, "row.6.e0c0") == 0) {
            if (Picture* g = raster(r, od::port::kVecLanguage, s)) {
                const float cap = std::min(8.0f, h * 0.55f) * textScale, icon = 10.0f, gap = 2.5f, maxW = w - 6.0f - icon - gap;
                float px = bold_.em(cap * s);
                const float tw0 = bold_.width(*t, px);
                if (tw0 > maxW * s) px *= maxW * s / tw0;
                const float tw = bold_.width(*t, px) / s, total = icon + gap + tw, left = x + (w - total) * 0.5f;
                const ui::Vec2 a(std::floor(o.x + left * s + 0.5f), std::floor(o.y + (y + (h - icon) * 0.5f) * s + 0.5f));
                cv.image(g->tex, a, ui::Vec2(a.x + float(g->w), a.y + float(g->h)), ui::Vec2(0.0f, 0.0f),
                         ui::Vec2(1.0f, 1.0f), tc);
                const float capPx = cap * s * (px / bold_.em(cap * s));
                bold_.draw(cv, *t, o.x + (left + icon + gap) * s, o.y + (y + h * 0.5f) * s + capPx * 0.5f, px, tc);
                return true;
            }
        }
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
    case od::port::kLblOptText: { // OpenDash's rows' values: as the game's value boxes, the text its own
        if (!p.utf8 || !*p.utf8) return true;
        const int kind = std::clamp(int(p.labelArg), 0, 2);
        static const ui::Color kBox[3] = {ui::rgba(0x40, 0x68, 0x60), ui::rgba(0x78, 0x50, 0x60), ui::rgba(0x30, 0x80, 0x48)};
        static const ui::Color kText[3] = {ui::rgba(0x88, 0x90, 0x98), ui::rgba(0xf0, 0xe0, 0xe0), ui::rgba(0xf0, 0xf0, 0xe8)};
        cv.rectFilled(P(x + 1.0f, y + 2.0f), P(x + w - 1.0f, y + h - 1.0f), kBox[kind]);
        caption(kind == 0 ? regular_ : bold_, p.utf8, x + w * 0.5f, y + 2.0f + (h - 3.0f) * 0.5f, std::min(7.5f, h * 0.55f),
                kText[kind], 0.0f, w - 4.0f);
        return true;
    }
    case od::port::kLblPageTab: { // L <previous page>  /  <next page> R (the pages 3 map .. 6 options wrap)
        const int page = p.labelArg >> 8, right = p.labelArg & 1;
        const int other = right ? (page >= 6 ? 3 : page + 1) : (page <= 3 ? 6 : page - 1);
        const std::string* t = word("pagename." + std::to_string(other));
        if (!t) return false;
        const float cap = 7.5f * s * textScale, px = bold_.em(cap), ph = 10.0f * s, gap = 2.0f * s, cy = o.y + (y + 8.0f) * s;
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
