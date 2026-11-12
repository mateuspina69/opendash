#include "app/title_screen.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <span>

#pragma warning(push, 0)
#include <stb_image.h> // the implementation is compiled in engine/texture_pack.cpp
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>
#pragma warning(pop)

#include "app/music_choice.hpp"
#include "app/ui/vector_ui.hpp"
#include "engine/dat.hpp"
#include "engine/data_source.hpp"
#include "engine/file_data.hpp"
#include "game/audio_logic.hpp"
#include "game/demo_playback.hpp"
#include "game/message.hpp"
#include "game/player/player_model.hpp"
#include "game/fade.hpp"
#include "game/display.hpp"
#include "game/game_state.hpp"
#include "game/save_slots.hpp"
#include "game/stage_files.hpp"
#include "game/stage_state.hpp"
#include "game/title.hpp"


namespace od::app {

void TitleScreen::start(uint8_t mode) {
    tasks = {};
    od::port::MainTaskState& mainBlock = od::port::mainTask();
    mainBlock.clearBlockBytes();
    mainBlock.mode = mode;
    if (mode >= 2) { // a dev start past the boot: the boot's defaults (mode 0) and the save scan (the logo)
        mainBlock.mode = uint8_t(od::port::MainMode::Boot);
        od::port::TitleTasks boot;
        od::port::titleTaskFrame(boot, host, nullptr);
        mainBlock.mode = mode;
        mainBlock.step = 0;
        const uint32_t saves = host.scanSaves ? host.scanSaves() : 0;
        od::port::saveSlotBits().present = saves;
        od::port::GameState& game = od::port::gameState();
        game.savesExist = uint8_t(saves != 0);
        od::port::datLoadQueue().clear();
        od::port::fileLoads().loading = 0;
        if (mode == uint8_t(od::port::MainMode::ToGame)) { // what the logo's last state (0x543120 sub 8) leaves for
                                                           // mode 4: the attract in ST02 sub 0
            game.loadSave = 0;
            game.stage = 2;
            game.clearRoomToNextStory();

        }
        applyDat("INIT.DAT"); // the boot's VRAM: INIT, LOGO, then GAME (the attract's mode 4 loads it)
        applyDat("LOGO.DAT");
        if (mode != 4) applyDat("GAME.DAT");
    }
    // no music is playing: the sound-fade bits the title waits for (Maybe_SoundFadesDone 0x5d5900)
    od::port::soundMarkFadesDone(od::port::kAllFades);
    active = true;
    inGame = false;
    error.clear();
}

void TitleScreen::startDemo() {
    if (od::port::gameState().frontEnd != od::port::kFrontEndDemo) return;
    char nm[24];
    std::snprintf(nm, sizeof nm, "ST%02XT.DAT", unsigned(od::port::gameState().stage));
    try {
        const od::GameFile f = od::dataSource().gameFile(nm);
        const uint32_t at = od::port::stage().spareBuffer ? od::port::kSpareBufferB : od::port::kSpareBufferA;
        for (const od::FilePart& sg : f.parts)
            if (sg.kind == od::FilePart::Kind::Geometry) {
                od::loadedFiles().map(at, sg.bytes);
                od::port::demoStart(at);
                SDL_Log("title: demo: %s pad recording, %u bytes", nm, unsigned(od::port::dataRecord<od::port::DemoRecording>(at, od::port::DemoRecording::kBytes).bytes));
                return;
            }
        SDL_Log("title: demo: %s has no pad recording", nm);
    } catch (const std::exception& e) {
        SDL_Log("title: demo: %s", e.what());
    }
}

void TitleScreen::drawDemo(od::Renderer& r, ui::Canvas& cv, float width, float height) {
    if (!demoShown) return;
    const float s = height / 240.0f;
    const ui::Vec2 o((width - 320.0f * s) * 0.5f, 0.0f);
    const od::TextureHandle t = page(r, 0x0d, 0x7e50);
    if (!t) return;
    cv.image(t, ui::Vec2(o.x + 0xc8 * s, o.y + 0xb8 * s), ui::Vec2(o.x + (0xc8 + 0x60) * s, o.y + (0xb8 + 0x28) * s),
                 ui::Vec2(0x30 / 256.0f, 0xc0 / 256.0f), ui::Vec2((0x30 + 0x60) / 256.0f, (0xc0 + 0x28) / 256.0f),
                 ui::rgba(255, 255, 255, 128));
}

std::filesystem::path TitleScreen::slotPath(uint32_t slot) const {
    char nm[24];
    std::snprintf(nm, sizeof nm, "DASH2-%02u.DAT", slot);
    if (std::filesystem::is_regular_file(savesDir / nm)) return savesDir / nm;
    return gameDir / nm;
}

void TitleScreen::bindHost(od::port::FadeTask* fade) {
    host.fade = fade;
    host.scanSaves = [this] {
        uint32_t mask = 0;
        for (uint32_t i = 0; i < 32; ++i)
            if (std::filesystem::is_regular_file(slotPath(i))) mask |= 1u << i;
        return mask;
    };
    host.readSave = [this](uint32_t slot, uint8_t* out, uint32_t size) {
        const auto p = slotPath(slot);
        if (!std::filesystem::is_regular_file(p)) return -1;
        try {
            const auto f = od::readFile(p); // read only
            std::memcpy(out, f.data(), std::min<size_t>(size, f.size()));
            return 1;
        } catch (const std::exception&) {
            return -1;
        }
    };
}

void TitleScreen::applyDat(const std::string& name) {
    try {
        mv.applyDat(name, od::dataSource().gameFile(name));
        dropTextures();
    } catch (const std::exception& e) {
        SDL_Log("title: %s: %s", name.c_str(), e.what());
        error = e.what();
    }
}

void TitleScreen::dropTextures() {
    for (auto& [k, t] : tex) stale.push_back(t);
    tex.clear();
}

void TitleScreen::processDats() {
    od::port::DatLoadQueue& queue = od::port::datLoadQueue();
    if (queue.requests[0].kind == 0) return;
    for (size_t q = 0; q < queue.requests.size() && queue.requests[q].kind != 0; ++q) {
        const std::string name = od::datFileName(queue.requests[q].fileId);
        try {
            od::GameFile f = od::dataSource().gameFile(name);
            int mp3 = 0;
            od::GameFile original; // the original music's file, with the extended music off (app/music_choice)
            for (const od::FilePart& sg : od::music_choice::musicOf(name, f, original).parts)
                if (sg.kind == od::FilePart::Kind::Mp3Music || sg.kind == od::FilePart::Kind::Ps1Music)
                    od::port::loadMp3Slot(std::span<const uint8_t>(sg.bytes), uint32_t(mp3++), sg.arg);
            for (const od::FilePart& sg : f.parts) {
                const std::span<const uint8_t> d(sg.bytes);
                if (sg.kind == od::FilePart::Kind::PcMessages || sg.kind == od::FilePart::Kind::Ps1Messages)
                    od::loadedFiles().map(od::port::kMessageBank, d.first(std::min<size_t>(d.size(), 0x4000)));
            }
            mv.applyDat(name, std::move(f));
            dropTextures();
            SDL_Log("title: dat load %s", name.c_str());
        } catch (const std::exception& e) {
            SDL_Log("title: dat load %s failed: %s", name.c_str(), e.what());
            error = e.what();
        }
    }
    queue.clear();
    od::port::fileLoads().loading = 0;
}

od::TextureHandle TitleScreen::page(od::Renderer& r, uint16_t tpage, uint16_t clut) {
    const uint32_t key = uint32_t(tpage & 0x1ff) << 16 | clut;
    if (const auto it = tex.find(key); it != tex.end()) return it->second;
    const auto rgba = mv.vram.decode(uint16_t(tpage & 0x1ff), clut);
    std::vector<uint32_t> px(1024 * 1024);
    for (size_t y = 0; y < 1024; ++y)
        for (size_t x = 0; x < 1024; ++x) std::memcpy(&px[y * 1024 + x], &rgba[((y / 4) * 256 + x / 4) * 4], 4);
    const od::TextureHandle t = r.createTexture(1024, 1024, px.data());
    tex[key] = t;
    return t;
}

int TitleScreen::rowAt(float width, float height, float x, float y) const {
    const bool hi = od::port::display().mode == od::port::kTitleDisplayMode; // as draw: the frame's size and place
    const float sw = hi ? 640.0f : 320.0f, sh = hi ? 480.0f : 240.0f;
    const float s = std::min(height / sh, width / sw);
    const float px = (x - (width - sw * s) * 0.5f) / s, py = (y - (height - sh * s) * 0.5f) / s;
    int row = -1;
    for (const od::port::ScreenPrim& p : prims)
        if (p.row >= 0 && px >= p.x && px < p.x + p.w && py >= p.y && py < p.y + p.h) row = p.row;
    return row;
}

// drawLogo's frame: its white F rect over the 640 x 480 screen first, then its three sprites (544 x 96 at 0x30, 0xc0)
bool TitleScreen::drawLogoArt(od::Renderer& r, ui::Canvas& cv, float width, float height) {
    if (od::port::mainTask().mode != uint8_t(od::port::MainMode::Logo) || prims.size() != 4) return false;
    const od::port::ScreenPrim& bg = prims[0];
    if (bg.kind != od::port::ScreenPrim::Flat || bg.x != 0 || bg.y != 0 || bg.w != 0x280 || bg.h != 0x1e0) return false;
    LogoArt& a = logoArt;
    if (!a.tried) {
        a.tried = true;
        // the official logo (PNG) first, else the vector one
        try {
            const auto png = assetsDir() / "ui" / "capcom_logo.png";
            std::error_code ec;
            if (std::filesystem::is_regular_file(png, ec)) {
                const std::vector<uint8_t> bytes = od::readFile(png);
                int w = 0, h = 0, comp = 0;
                if (stbi_uc* px = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &comp, 4)) {
                    a.png = r.createTexture(w, h, reinterpret_cast<const uint32_t*>(px));
                    a.pngW = w;
                    a.pngH = h;
                    stbi_image_free(px);
                }
            }
        } catch (const std::exception& e) {
            SDL_Log("title: logo png: %s", e.what());
        }
        if (!a.png) {
            a.svg = std::make_unique<ui::Svg>();
            const auto path = assetsDir() / "ui" / "capcom_logo.svg";
            if (!a.svg->load(path)) {
                SDL_Log("title: no logo picture %s: the pixel art stays", path.string().c_str());
                a.svg.reset();
            }
        }
    }
    if (!a.svg && !a.png) return false;
    const float s = std::min(height / 480.0f, width / 640.0f);
    const ui::Vec2 o((width - 640.0f * s) * 0.5f, (height - 480.0f * s) * 0.5f);
    if (a.png) {
        const uint32_t c = bg.colour;
        cv.rectFilled(ui::Vec2(0, 0), ui::Vec2(width, height), ui::rgba(c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff, 255));
        const uint32_t v = uint32_t(std::clamp(int(od::port::mainTask().brightness) * 2, 0, 255));
        // fitted in the sprites' 544 x 96 at (0x30, 0xc0), its aspect kept, centred there
        const float k = std::min(544.0f / float(a.pngW), 96.0f / float(a.pngH));
        const float w = float(a.pngW) * k, h = float(a.pngH) * k;
        const float x0 = 0x30 + (544.0f - w) * 0.5f, y0 = 0xc0 + (96.0f - h) * 0.5f;
        cv.image(a.png, ui::Vec2(o.x + x0 * s, o.y + y0 * s), ui::Vec2(o.x + (x0 + w) * s, o.y + (y0 + h) * s),
                 ui::Vec2(0.0f, 0.0f), ui::Vec2(1.0f, 1.0f), ui::rgba(v, v, v, 255));
        return true;
    }
    if (!a.tex || a.scale != s) {
        if (a.tex) r.destroyTexture(a.tex);
        const std::vector<uint32_t> px = a.svg->rasterize(s, a.w, a.h);
        a.tex = px.empty() ? 0 : r.createTexture(a.w, a.h, px.data());
        a.scale = s;
    }
    // the fade: the rect's colour as drawLogo made it, the sprites' modulation brightness / 0x80
    const uint32_t c = bg.colour;
    cv.rectFilled(ui::Vec2(0, 0), ui::Vec2(width, height), ui::rgba(c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff, 255));
    const uint32_t v = uint32_t(std::clamp(int(od::port::mainTask().brightness) * 2, 0, 255));
    if (a.tex)
        cv.image(a.tex, ui::Vec2(o.x + 0x30 * s, o.y + 0xc0 * s), ui::Vec2(o.x + 0x30 * s + float(a.w), o.y + 0xc0 * s + float(a.h)),
                 ui::Vec2(0.0f, 0.0f), ui::Vec2(1.0f, 1.0f), ui::rgba(v, v, v, 255));
    return true;
}

uint64_t TitleScreen::spriteHash(const od::port::ScreenPrim& p) const {
    uint64_t h = 1469598103934665603ull;
    auto add = [&](uint8_t b) { h = (h ^ b) * 1099511628211ull; };
    const int px = (p.tpage & 0xf) * 64, py = ((p.tpage >> 4) & 1) * 256;
    for (int y = 0; y < p.h; ++y)
        for (int x = 0; x < p.w; ++x) {
            const int u = (p.u + x) & 0xff, v = (p.v + y) & 0xff;
            add(uint8_t((mv.vram.pixel(px + u / 4, py + v) >> ((u & 3) * 4)) & 0xf));
        }
    const int cx = (p.clut & 0x3f) * 16, cy = (p.clut >> 6) & 0x1ff;
    for (int i = 0; i < 16; ++i) {
        const uint16_t c = mv.vram.pixel(cx + i, cy);
        add(uint8_t(c & 0xff));
        add(uint8_t(c >> 8));
    }
    return h;
}

TitleScreen::HdImage* TitleScreen::hdImage(od::Renderer& r, const std::string& file, int w, int h) {
    HdImage& m = hdImages[file];
    if (!m.tried) {
        m.tried = true;
        const auto path = assetsDir() / "ui" / "title" / file;
        std::ifstream f(path, std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        int comp = 0;
        if (stbi_uc* px = bytes.empty() ? nullptr
                                        : stbi_load_from_memory(bytes.data(), int(bytes.size()), &m.sw, &m.sh, &comp, 4)) {
            m.src.assign(px, px + size_t(m.sw) * size_t(m.sh) * 4);
            stbi_image_free(px);
        } else {
            SDL_Log("title: no picture %s: the pixel art stays", path.string().c_str());
        }
    }
    if (m.src.empty()) return nullptr;
    if (w <= 0 || h <= 0) return &m; // only the master (its size)
    if (!m.tex || m.w != w || m.h != h) {
        if (m.tex) r.destroyTexture(m.tex);
        std::vector<uint32_t> px(size_t(w) * size_t(h));
        stbir_resize_uint8_srgb(m.src.data(), m.sw, m.sh, 0, reinterpret_cast<unsigned char*>(px.data()), w, h, 0,
                                STBIR_RGBA);
        m.tex = r.createTexture(w, h, px.data());
        m.w = w;
        m.h = h;
    }
    return &m;
}

std::vector<TitleScreen::HdDraw> TitleScreen::hdTitle(od::Renderer& r, float s, ui::Vec2 o,
                                                      std::vector<od::port::ScreenPrim>& rest) {
    using SP = od::port::ScreenPrim;
    std::vector<HdDraw> out;
    rest.clear();
    rest.reserve(prims.size());
    static const char* dumpDir = std::getenv("OD_DUMP_TITLE");
    // dev (with OD_DUMP_TITLE): OD_DUMP_DIFFICULTY=1 counts every difficulty as cleared, so the difficulty list (its
    // sprites) can be reached from a fresh save folder
    static const bool allDifficulties = dumpDir && std::getenv("OD_DUMP_DIFFICULTY");
    if (allDifficulties) od::port::gameState().bestDifficulty = 3;
    // the logo: its sprites' box (the PC's 448 x 224 at 0x50, the PS1 layout's 544 x 240 at 0x30)
    int lx0 = 1 << 20, ly0 = 1 << 20, lx1 = -(1 << 20), ly1 = -(1 << 20);
    uint32_t lc = 0x80;
    for (const SP& p : prims)
        if (p.kind == SP::Sprite && p.clut == 0x7dc0 && (p.tpage & 0x1f) >= 0x1a && (p.tpage & 0x1f) <= 0x1e) {
            lx0 = std::min<int>(lx0, p.x);
            ly0 = std::min<int>(ly0, p.y);
            lx1 = std::max<int>(lx1, p.x + p.w);
            ly1 = std::max<int>(ly1, p.y + p.h);
            lc = p.colour & 0xff;
        }
    bool logo = false;
    if (lx1 > lx0) {
        // the logo of the game's name in that language (its "OPEN DASH EDITION" drawn into the picture): Rockman
        // Dash 2 in Japanese (the whole logo), Korean and Chinese (without the Japanese subtitle), else Mega Man Legends 2
        const std::string name = uiLang == "jp" ? "rd2" : (uiLang == "ko" || uiLang == "zh" || uiLang == "zhhk") ? "rd2s" : "mml2";
        if (HdImage* L = hdImage(r, "logo_" + name + ".png", 0, 0)) { // the master's size first (the fit's aspect)
            // fitted to the sprites' box, centred (its last kBottom units left free: the menu's first row is under it)
            constexpr float kBottom = 12.0f;
            const float bw = float(lx1 - lx0), bh = float(ly1 - ly0) - kBottom;
            const float k = std::min(bw / float(L->sw), bh / float(L->sh));
            const float lw = float(L->sw) * k, lh = float(L->sh) * k;
            const float cx = float(lx0 + lx1) * 0.5f, top = float(ly0) + (bh - lh) * 0.5f;
            const uint32_t v = std::min<uint32_t>(lc * 2, 255);
            const ui::Vec2 la(o.x + (cx - lw * 0.5f) * s, o.y + top * s);
            const int Lw = int(std::lround(lw * s)), Lh = int(std::lround(lh * s));
            if ((L = hdImage(r, "logo_" + name + ".png", Lw, Lh))) {
                out.push_back({L->tex, la, ui::Vec2(la.x + float(Lw), la.y + float(Lh)), ui::rgba(v, v, v, 255)});
                logo = true;
            }
        }
    }
    // the menu's rows closer than their 40 px (five or six rows: Extras, Quit): their words a little smaller, about
    // each row's middle, so they keep apart (the rows' boxes -- the cursor, the mouse -- stay as the game set them)
    float rowK = 1.0f;
    {
        std::vector<int> ys;
        for (const SP& p : prims)
            if (p.kind == SP::Sprite && p.row >= 0 && p.row < 16 && p.w == 0xd8 && p.h == 0x28) ys.push_back(p.y);
        std::sort(ys.begin(), ys.end());
        for (size_t i = 1; i < ys.size(); ++i)
            if (ys[i] > ys[i - 1]) rowK = std::min(rowK, float(ys[i] - ys[i - 1]) * 1.05f / 40.0f);
    }
    auto rowFit = [&](HdDraw d, const SP& p) {
        if (rowK >= 1.0f || p.row < 0 || p.row >= 16 || p.w != 0xd8 || p.h != 0x28) return d;
        const ui::Vec2 c(o.x + (float(p.x) + float(p.w) * 0.5f) * s, o.y + (float(p.y) + float(p.h) * 0.5f) * s);
        d.a = ui::Vec2(c.x + (d.a.x - c.x) * rowK, c.y + (d.a.y - c.y) * rowK);
        d.b = ui::Vec2(c.x + (d.b.x - c.x) * rowK, c.y + (d.b.y - c.y) * rowK);
        return d;
    };
    for (const SP& p : prims) {
        if (p.kind == SP::Sprite && p.clut == 0x7dc0 && (p.tpage & 0x1f) >= 0x1a && (p.tpage & 0x1f) <= 0x1e) {
            if (!logo) rest.push_back(p);
            continue;
        }
        if (p.label == od::port::kLblTitleExtras || p.label == od::port::kLblTitleQuit) { // OpenDash's rows: their
            HdDraw d;                                                                       // words' pictures
            if (extrasWord(r, p, s, o, d)) out.push_back(rowFit(d, p));
            continue;
        }
        // a 4-bit sprite with a vector picture of its pixels
        if (p.kind == SP::Sprite && !p.half && !p.grab && p.model < 0 && !p.hitOnly && ((p.tpage >> 7) & 3) == 0 &&
            p.w > 0 && p.h > 0) {
            const uint64_t pix = spriteHash(p);
            // the language's own picture first (assets/ui/title/<language>/: the same sprite -- the Japanese art --
            // shows each language's word), then the one for every language
            uint64_t key = pix;
            for (const char ch : uiLang) key = (key ^ uint8_t(ch)) * 1099511628211ull;
            HdSprite& h = hdSprites[key];
            if (!h.tried) {
                h.tried = true;
                char nm[40];
                std::snprintf(nm, sizeof nm, "sprite_%016llx", static_cast<unsigned long long>(pix));
                const auto dir = assetsDir() / "ui" / "title";
                for (const auto& path : {dir / uiLang / (std::string(nm) + ".svg"), dir / (std::string(nm) + ".svg")}) {
                    std::error_code ec;
                    if (!std::filesystem::exists(path, ec)) continue;
                    h.svg = std::make_unique<ui::Svg>();
                    if (h.svg->load(path)) break;
                    h.svg.reset();
                }
                if (!h.svg && dumpDir) { // the tracer's input: w h, the palette, then the rows of indices
                    if (FILE* f = std::fopen((std::string(dumpDir) + "/" + nm + ".txt").c_str(), "w")) {
                        std::fprintf(f, "%d %d %04x %04x %d %d\n", p.w, p.h, unsigned(p.tpage), unsigned(p.clut), p.x, p.y);
                        const int cx = (p.clut & 0x3f) * 16, cy = (p.clut >> 6) & 0x1ff;
                        for (int i = 0; i < 16; ++i) std::fprintf(f, "%04x ", unsigned(mv.vram.pixel(cx + i, cy)));
                        std::fprintf(f, "\n");
                        const int bx = (p.tpage & 0xf) * 64, by = ((p.tpage >> 4) & 1) * 256;
                        for (int y = 0; y < p.h; ++y) {
                            for (int x = 0; x < p.w; ++x) {
                                const int u = (p.u + x) & 0xff, v = (p.v + y) & 0xff;
                                std::fprintf(f, "%x", unsigned((mv.vram.pixel(bx + u / 4, by + v) >> ((u & 3) * 4)) & 0xf));
                            }
                            std::fprintf(f, "\n");
                        }
                        std::fclose(f);
                    }
                }
            }
            if (h.svg) {
                if (!h.tex || h.scale != s) {
                    if (h.tex) r.destroyTexture(h.tex);
                    const std::vector<uint32_t> px = h.svg->rasterize(s, h.w, h.h);
                    h.tex = px.empty() ? 0 : r.createTexture(h.w, h.h, px.data());
                    h.scale = s;
                }
                if (h.tex) {
                    // the picture's margin: its document is the sprite grown by (w - sprite w) / 2 on each side
                    const float mx = (h.svg->width() - float(p.w)) * 0.5f, my = (h.svg->height() - float(p.h)) * 0.5f;
                    const ui::Vec2 a(o.x + (float(p.x) - mx) * s, o.y + (float(p.y) - my) * s);
                    const uint32_t v = std::min<uint32_t>((p.colour & 0xff) * 2, 255);
                    out.push_back(rowFit({h.tex, a, ui::Vec2(a.x + float(h.w), a.y + float(h.h)), ui::rgba(v, v, v, 255)}, p));
                    continue;
                }
            }
        }
        rest.push_back(p);
    }
    return out;
}

void TitleScreen::draw(od::Renderer& r, ui::Canvas& cv, float width, float height,
              const std::function<void(const od::port::ScreenPrim&, float, ui::Vec2)>& textAt) {
    for (od::TextureHandle t : stale) r.destroyTexture(t);
    stale.clear();
    if (drawLogoArt(r, cv, width, height)) return;
    cv.rectFilled(ui::Vec2(0, 0), ui::Vec2(width, height), ui::rgba(0, 0, 0, 255));
    const bool hi = od::port::display().mode == od::port::kTitleDisplayMode;
    const float sw = hi ? 640.0f : 320.0f, sh = hi ? 480.0f : 240.0f;
    const float s = std::min(height / sh, width / sw);
    const ui::Vec2 o((width - sw * s) * 0.5f, (height - sh * s) * 0.5f);
    const auto text = [&](const od::port::ScreenPrim& p) { textAt(p, s, o); };
    if (hi && painter.art.enabled && od::port::mainTask().mode != uint8_t(od::port::MainMode::Logo)) {
        std::vector<od::port::ScreenPrim> rest;
        const std::vector<HdDraw> hd = hdTitle(r, s, o, rest);
        painter.draw(r, mv, cv, rest, s, o, text, sw, sh, o.x / s); // widescreen: a Clip may open past the frame
        for (const HdDraw& d : hd) cv.image(d.tex, d.a, d.b, ui::Vec2(0.0f, 0.0f), ui::Vec2(1.0f, 1.0f), d.c);
        return;
    }
    // the pixel art: the Extras row has no sprite of its own, its word's picture is drawn over the painter's pass
    std::vector<od::port::ScreenPrim> rest;
    std::vector<HdDraw> extras;
    rest.reserve(prims.size());
    for (const od::port::ScreenPrim& p : prims) {
        HdDraw d;
        if (p.label != od::port::kLblTitleExtras && p.label != od::port::kLblTitleQuit) rest.push_back(p);
        else if (extrasWord(r, p, s, o, d)) extras.push_back(d);
    }
    painter.draw(r, mv, cv, rest, s, o, text, sw, sh, o.x / s); // widescreen: a Clip may open past the frame
    for (const HdDraw& d : extras) cv.image(d.tex, d.a, d.b, ui::Vec2(0.0f, 0.0f), ui::Vec2(1.0f, 1.0f), d.c);
}

bool TitleScreen::extrasWord(od::Renderer& r, const od::port::ScreenPrim& p, float s, ui::Vec2 o, HdDraw& out) {
    // assets/ui/title/<language>/extras_sel.svg (the cursor's row) / extras_nor.svg, else the English one; the Quit
    // row's sair_sel.svg / sair_nor.svg
    const bool quit = p.label == od::port::kLblTitleQuit;
    const char* st = quit ? (p.labelArg ? "sair_sel" : "sair_nor") : (p.labelArg ? "extras_sel" : "extras_nor");
    uint64_t key = 1469598103934665603ull;
    for (const char ch : std::string(st) + "/" + uiLang) key = (key ^ uint8_t(ch)) * 1099511628211ull;
    HdSprite& h = hdSprites[key];
    if (!h.tried) {
        h.tried = true;
        const auto dir = assetsDir() / "ui" / "title";
        for (const auto& path : {dir / uiLang / (std::string(st) + ".svg"), dir / "en" / (std::string(st) + ".svg")}) {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) continue;
            h.svg = std::make_unique<ui::Svg>();
            if (h.svg->load(path)) break;
            h.svg.reset();
        }
    }
    if (!h.svg) return false;
    if (!h.tex || h.scale != s) {
        if (h.tex) r.destroyTexture(h.tex);
        const std::vector<uint32_t> px = h.svg->rasterize(s, h.w, h.h);
        h.tex = px.empty() ? 0 : r.createTexture(h.w, h.h, px.data());
        h.scale = s;
    }
    if (!h.tex) return false;
    // centred on the row's 216 x 40 box, as the other rows' pictures
    const float mx = (h.svg->width() - float(p.w)) * 0.5f, my = (h.svg->height() - float(p.h)) * 0.5f;
    const ui::Vec2 a(o.x + (float(p.x) - mx) * s, o.y + (float(p.y) - my) * s);
    const uint32_t v = std::min<uint32_t>((p.colour & 0xff) * 2, 255);
    out = {h.tex, a, ui::Vec2(a.x + float(h.w), a.y + float(h.h)), ui::rgba(v, v, v, 255)};
    return true;
}

} // namespace od::app
