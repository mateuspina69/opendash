#include "app/title_screen.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <span>

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
            for (const od::FilePart& sg : f.parts) {
                const std::span<const uint8_t> d(sg.bytes);
                if (sg.kind == od::FilePart::Kind::Mp3Music) od::port::loadMp3Slot(d, uint32_t(mp3++), sg.arg);
                else if (sg.kind == od::FilePart::Kind::PcMessages)
                    od::loadedFiles().map(od::port::kMessageBank, d.first(std::min<size_t>(d.size(), 0x4000)));
            }
            mv.applyDat(name, std::move(f));
            dropTextures();
            SDL_Log("title: dat load %s", name.c_str());
        } catch (const std::exception& e) {
            SDL_Log("title: dat load %s failed: %s", name.c_str(), e.what());
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

void TitleScreen::draw(od::Renderer& r, ui::Canvas& cv, float width, float height,
              const std::function<void(const od::port::ScreenPrim&, float, ui::Vec2)>& textAt) {
    for (od::TextureHandle t : stale) r.destroyTexture(t);
    stale.clear();
    cv.rectFilled(ui::Vec2(0, 0), ui::Vec2(width, height), ui::rgba(0, 0, 0, 255));
    const bool hi = od::port::display().mode == od::port::kTitleDisplayMode;
    const float sw = hi ? 640.0f : 320.0f, sh = hi ? 480.0f : 240.0f;
    const float s = std::min(height / sh, width / sw);
    const ui::Vec2 o((width - sw * s) * 0.5f, (height - sh * s) * 0.5f);
    const auto text = [&](const od::port::ScreenPrim& p) { textAt(p, s, o); };
    painter.draw(r, mv, cv, prims, s, o, text, sw, sh, o.x / s); // widescreen: a Clip may open past the frame
}

} // namespace od::app
