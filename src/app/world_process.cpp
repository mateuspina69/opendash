// A world process (see world_process.hpp).
#include "app/world_process.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "app/game_files.hpp"
#include "app/log_file.hpp"
#include "app/play_session.hpp"
#include "app/room_mesh.hpp"
#include "content/registry.hpp"
#include "engine/config.hpp"
#include "engine/dat.hpp"
#include "engine/data_source.hpp"
#include "engine/lang.hpp"
#include "engine/pc_data_source.hpp"
#include "engine/ps1_data_source.hpp"
#include "game/demo_playback.hpp"
#include "game/display.hpp"
#include "game/title.hpp"
#include "game/world_digest.hpp"

namespace od::app {

namespace {

constexpr uint64_t kTickNs = 1'000'000'000ull / 30;
constexpr uint64_t kStatsEvery = 150;

struct WorldArgs {
    int stage = -1, sub = 0;
    uint64_t ticks = 0; // 0: until the process is stopped
    bool unpaced = false;
    std::string storySave;
    std::vector<uint16_t> pad; // the PS1 pad bits of each tick (past the end: none)
};

// "<ticks> <hex PS1 pad bits>; ..."
std::vector<uint16_t> parsePad(const std::string& text) {
    std::vector<uint16_t> pad;
    std::stringstream all(text);
    for (std::string step; std::getline(all, step, ';');) {
        std::istringstream ss(step);
        int ticks = 0;
        unsigned bits = 0;
        if (!(ss >> std::dec >> ticks >> std::hex >> bits)) throw std::invalid_argument("--world-pad: " + step);
        pad.insert(pad.end(), size_t(std::max(ticks, 0)), uint16_t(bits));
    }
    return pad;
}

WorldArgs parseArgs(int argc, char** argv) {
    WorldArgs w;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--world" && i + 2 < argc) {
            w.stage = std::stoi(argv[++i], nullptr, 16);
            w.sub = std::stoi(argv[++i]);
        } else if (a == "--world-ticks" && more) {
            w.ticks = std::stoull(argv[++i]);
        } else if (a == "--world-unpaced") {
            w.unpaced = true;
        } else if (a == "--world-pad" && more) {
            w.pad = parsePad(argv[++i]);
        } else if (a == "--story-save" && more) {
            w.storySave = argv[++i];
        }
    }
    if (w.stage < 0) throw std::invalid_argument("--world <stage> <sub>");
    return w;
}

std::unique_ptr<od::DataSource> openData(od::Config& cfg, PlayerView& view) {
    if (cfg.langDir.empty()) {
        const char* base = SDL_GetBasePath();
        cfg.langDir = std::filesystem::path(base ? base : "") / "lang";
    }
    if (!cfg.disc.empty()) view.disc = std::make_shared<const od::DiscImage>(cfg.disc);
    if (cfg.data != "disc") return std::make_unique<od::PcDataSource>(cfg.datDir, cfg.exePath);
    if (!view.disc) throw std::runtime_error("data=disc needs the disc image (disc= in opendash.ini)");
    return std::make_unique<od::Ps1DataSource>(view.disc);
}

// the room a world holds: what the game's logic reads of it; the triangles and pages are the renderer's
void holdRoom(RoomMesh& held, int stage, int sub) {
    RoomMesh mesh = loadRoom(stage, sub);
    mesh.tris.clear();
    mesh.slices.clear();
    held = std::move(mesh);
}

int run(const WorldArgs& w) {
    od::Config cfg = od::loadConfig();
    if (!ensureGameFiles(cfg, false)) return 1;
    auto view = std::make_unique<PlayerView>();
    const std::unique_ptr<od::DataSource> data = openData(cfg, *view);
    od::bindDataSource(data.get());

    view->stageSubs = stageSubRooms();
    view->modern = false; // no orbit camera drives the game's camera here: the game's own camera follows
    view->init(cfg);
    view->audioInit(cfg, false);
    view->langDir = cfg.langDir;
    view->restoredVoices = cfg.restoredVoices;
    view->setLanguage(od::lang::parseLanguage(cfg.lang));
    if (!w.storySave.empty()) {
        view->storySave = od::readFile(w.storySave);
        if (view->storySave.size() != od::port::kSaveSize || od::port::saveChecksumBad(view->storySave))
            throw std::runtime_error("--story-save: bad size / checksum");
    }

    RoomMesh current;
    view->room = &current;
    view->loadRoom = [&](int st, int sb) { holdRoom(current, st, sb); };
    view->afterPad = [] { od::port::demoPadFrame(); };
    view->gameOver = [&] { od::port::gameOverModeFrame(view->fadeTask); };
    view->afterFrame = [&] { od::port::softResetFrame(view->fadeTask); };
    holdRoom(current, w.stage, w.sub);

    // the boot's display state, which no title sets here (App::leaveTitle)
    od::port::display().mode = 0;
    od::port::display().maskTicks = 1;
    od::port::display().pace = 2;
    if (!view->enterPlay(current)) {
        SDL_Log("world: %s", view->playError.c_str());
        return 1;
    }
    SDL_Log("world: ST%02X sub %d running, %u live actors", w.stage, w.sub, od::port::worldDigest().liveActors);

    uint64_t next = SDL_GetTicksNS(), statsNs = 0, statsMaxNs = 0, tick = 0;
    while (view->play && (w.ticks == 0 || tick < w.ticks)) {
        const uint64_t at = SDL_GetTicksNS();
        const uint16_t pad = tick < w.pad.size() ? w.pad[size_t(tick)] : uint16_t(0);
        // the directions as the stick the logic reads when no stick is held, as the windowed game hands them
        const float lx = float(((pad & 0x20) != 0) - ((pad & 0x80) != 0));
        const float ly = float(((pad & 0x40) != 0) - ((pad & 0x10) != 0));
        view->playTick(pad, 128, 128, lx, ly);
        if (view->assetsDirty) {
            view->assetsDirty = false;
            g_inStageSwitch = true;
            holdRoom(current, current.stage, current.sub);
            g_inStageSwitch = false;
        }
        const uint64_t cost = SDL_GetTicksNS() - at;
        statsNs += cost;
        statsMaxNs = std::max(statsMaxNs, cost);
        if (++tick % kStatsEvery == 0) {
            SDL_Log("world: tick %llu, tick cost avg %.1f us, max %.1f us, %u live actors", (unsigned long long)tick,
                    double(statsNs) / double(kStatsEvery) * 1e-3, double(statsMaxNs) * 1e-3,
                    od::port::worldDigest().liveActors);
            statsNs = statsMaxNs = 0;
        }
        if (!w.unpaced) {
            next += kTickNs;
            const uint64_t after = SDL_GetTicksNS();
            if (next > after) SDL_DelayNS(next - after);
            else next = after; // late: no burst to catch up, the tick rate falls
        }
    }
    const int rc = view->playError.empty() ? 0 : 1;
    if (rc != 0) SDL_Log("world: stopped at tick %llu: %s", (unsigned long long)tick, view->playError.c_str());
    else SDL_Log("world: ended after %llu ticks", (unsigned long long)tick);
    od::port::setSoundBackend(nullptr);
    view->mixer.reset();
    return rc;
}

} // namespace

bool worldProcessRequested(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--world") return true;
    return false;
}

int runWorldProcess(int argc, char** argv) {
    int rc = 1;
    try {
        const WorldArgs w = parseArgs(argc, argv);
        char log[48];
        std::snprintf(log, sizeof log, "opendash-world-ST%02X-%d.log", w.stage, w.sub);
        startLogFile(log);
        od::content::registerAll(); // the game code dispatches into the content through its registry
        if (!SDL_Init(0)) throw std::runtime_error(SDL_GetError());
        if (const char* base = SDL_GetBasePath()) od::setConfigDir(base);
        rc = run(w);
    } catch (const std::exception& e) {
        SDL_Log("world failed: %s", e.what());
    }
    SDL_Quit();
    return rc;
}

} // namespace od::app
