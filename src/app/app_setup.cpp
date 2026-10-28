#include "app/app.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include "app/game_files.hpp"
#include "app/menu_vram.hpp"
#include "engine/dat.hpp"
#include "engine/exe_coverage.hpp"
#include "game/vectors.hpp"
#include "engine/lang.hpp"
#include "engine/room.hpp"
#include "engine/screen_prim.hpp"
#include "game/status_options.hpp"
#include "game/message.hpp"
#include "game/player/player_input.hpp"
#include "game/player/precise_pose.hpp"
#include "game/save_image.hpp"
#include "game/demo_playback.hpp"
#include "game/title.hpp"
#include "game/display.hpp"
#include "game/fade.hpp"
#include "game/stage_files.hpp"
#include "game/walker.hpp"
#include "render/render_logic.hpp"
#include <imgui_impl_sdl3.h>
#include "game/player/player_state.hpp"
#include "game/game_state.hpp"
#include "game/story_flags.hpp"


namespace od::app {

namespace {

// the video row's anti-aliasing choices (aa=), in its order
constexpr od::AaBackend kAaBackends[] = {od::AaBackend::Auto, od::AaBackend::None, od::AaBackend::Dlss,
                                        od::AaBackend::Fsr,  od::AaBackend::Xess, od::AaBackend::Nis,
                                        od::AaBackend::Msaa, od::AaBackend::Ssaa};
int aaIndex(od::AaBackend b) {
    const auto it = std::find(std::begin(kAaBackends), std::end(kAaBackends), b);
    return it == std::end(kAaBackends) ? 0 : int(it - std::begin(kAaBackends));
}

// Every ST##.DAT the exe knows (stage table size) with a room block, and its sub-room count.
std::vector<StageInfo> scanStages() {
    std::vector<StageInfo> out;
    for (int n = 0; n < od::kStageCount; ++n) {
        char name[16];
        std::snprintf(name, sizeof name, "ST%02X.DAT", n);
        if (!od::dataSource().hasFile(name)) continue;
        try {
            const od::GameFile st = od::dataSource().gameFile(name);
            const int subs = stageRoomCount(n, od::roomGrids(od::roomBlock(st)).size());
            if (subs > 0) out.push_back({n, subs});
        } catch (const std::exception&) {
        }
    }
    return out;
}

} // namespace

void App::parseArgs(int argc, char** argv) {
    { // before the data source's first read of the exe (engine/exe_coverage): the flag, else OPENDASH_EXE_COVERAGE
        std::string cov;
        for (int i = 1; i + 1 < argc; ++i)
            if (std::string(argv[i]) == "--exe-coverage") cov = argv[i + 1];
        if (cov.empty())
            if (const char* e = std::getenv("OPENDASH_EXE_COVERAGE")) cov = e;
        if (!cov.empty()) od::exe_coverage::enable(cov);
    }
    for (int i = 1; i < argc; ++i) // flags without a value (the loop below only reads "--name value" pairs)
        if (std::string(argv[i]) == "--motion-selftest") motionSelfTest = true;
        else if (std::string(argv[i]) == "--resize-test") resizeTest = true;
        else if (std::string(argv[i]) == "--skip-title") skipTitle = true;
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--stage") {
            stage = std::stoi(argv[++i], nullptr, 16);
            stageGiven = true;
        }
        else if (a == "--title-mode") titleModeArg = std::stoi(argv[++i]);
        else if (a == "--sub") sub = std::stoi(argv[++i]);
        else if (a == "--shot") shotPath = argv[++i];
        else if (a == "--aa") aaArg = argv[++i];
        else if (a == "--lang") langArg = argv[++i];
        else if (a == "--voice-test") voiceTestArg = argv[++i];
        else if (a == "--aa-quality") aaQualityArg = argv[++i];
        else if (a == "--vsync") vsyncArg = std::stoi(argv[++i]) != 0;
        else if (a == "--view") {
            static const char* kViews[] = {"final", "color", "motion", "normal", "depth", "reactive"};
            const std::string v = argv[++i];
            for (int k = 0; k < 6; ++k)
                if (v == kViews[k]) debugView = k;
        }
        else if (a == "--shot-every") shotEvery = std::max(0, std::stoi(argv[++i]));
        else if (a == "--shot-from") shotFrom = std::stoull(argv[++i]);
        else if (a == "--shot-frames") shotFrames = std::max(1, std::stoi(argv[++i])); // frames before the shot
        else if (a == "--menu") forceMenu = std::stoi(argv[++i]) != 0;
        else if (a == "--tick") startTick = size_t(std::stoul(argv[++i])); // first viewer animation tick
        else if (a == "--sky2d") sky2d = std::stoi(argv[++i]) != 0;              // original 2D sky
        else if (a == "--nofog") noFog = std::stoi(argv[++i]) != 0;              // fog off (comparison)
        else if (a == "--fog") fogArg = argv[++i];                                // remaster | classic | off
        else if (a == "--weather") weatherArg = argv[++i];                        // remaster | classic
        else if (a == "--play") startPlay = std::stoi(argv[++i]) != 0;            // start in play mode
        else if (a == "--load") loadArg = argv[++i];
        else if (a == "--fps-log") fpsLog = true;
        else if (a == "--newgame") newGameArg = std::stoi(argv[++i]);
        else if (a == "--newgame-stage" && i + 2 < argc) { // dev: "--newgame-stage 3a 0" (with --newgame D)
            newGameStageArg = std::stoi(argv[++i], nullptr, 16);
            newGameSubArg = std::stoi(argv[++i]);
        }
        else if (a == "--savegame") saveGameArg = std::stoi(argv[++i]);
        else if (a == "--audio") audioArg = std::stoi(argv[++i]) != 0;
        else if (a == "--audio-device") audioDeviceArg = std::stoi(argv[++i]) != 0;
        else if (a == "--audio-log") audioLogArg = std::stoi(argv[++i]) != 0;
        else if (a == "--orbit") startOrbit = std::stof(argv[++i]);
        else if (a == "--orbit-spin") orbitSpin = std::stof(argv[++i]);
        else if (a == "--orbit-tilt") orbitTilt = std::stof(argv[++i]); // dev: pitch rate (the camera aim evidence)
        else if (a == "--doortest") doorTestArg = std::stoi(argv[++i]);
        else if (a == "--hd-text") messageUi.hdText = std::stoi(argv[++i]) != 0; // 0 = the PS1 bitmap font
        else if (a == "--hd-art") hdArtArg = std::stoi(argv[++i]) != 0; // 0 = the containers' pixel art
        else if (a == "--menu-snappy") od::port::setMenuSnappy(std::stoi(argv[++i]) != 0); // 0 = the PS1 cursor
        else if (a == "--classic") classicArg = std::stoi(argv[++i]) != 0; // the original tank controls (scripted runs)
        else if (a == "--precise-poses") od::precise::setEnabled(std::stoi(argv[++i]) != 0); // 0 = the PS1 rounded poses
        else if (a == "--flag") flagArgs.push_back(uint32_t(std::stoul(argv[++i], nullptr, 0)));
        else if (a == "--clear-flag") flagClearArgs.push_back(uint32_t(std::stoul(argv[++i], nullptr, 0)));
        else if (a == "--story-save") storySaveArg = argv[++i]; // free play: story state of DASH2-NN.DAT (none = story 0)
        else if (a == "--sun" && i + 2 < argc) {
            sunAzimuth = std::stof(argv[++i]);
            sunElevation = std::stof(argv[++i]);
            sunOn = true;
        }
        else if (a == "--sun-strength") sunStrength = std::stof(argv[++i]);
        else if (a == "--shot-orbit") shotOrbit = std::stof(argv[++i]); // dev: side views of effects (evidence)
        else if (a == "--shot-zoom") shotZoom = std::max(1.0f, std::stof(argv[++i])); // dev: --shot field of view / k
        else if (a == "--yaw") teleportYawArg = std::stoi(argv[++i]);
        else if (a == "--teleport" && i + 3 < argc) {
            teleportArg = true;
            for (int& v : teleportPosArg) v = std::stoi(argv[++i]);
        }
        else if (a == "--playscript") { // "<ticks> <btn+btn|->; ...", implies --play
            startPlay = true;
            parsePlayScript(argv[++i]);
        }
        else if (a == "--playscript-file") { // the same syntax from a file (';' or new lines, '#' comments)
            startPlay = true;
            std::ifstream f(argv[++i]);
            std::string text;
            for (std::string line; std::getline(f, line);) text += line.substr(0, line.find('#')) + ";";
            if (!f.eof() && text.empty()) SDL_Log("pt error: cannot read --playscript-file %s", argv[i]);
            parsePlayScript(text);
        }
        else if (a == "--savefile") saveFileArg = argv[++i]; // at the end of --playscript: save to this path
        else if (a == "--god") godArg = std::stoi(argv[++i]) != 0; // test-only: life refilled every tick
        else if (a == "--ghost-mirror") { // dev: ghost slot 0 = the local player, delayed + offset
            if (i + 3 >= argc) {
                SDL_Log("--ghost-mirror needs <delay ticks> <dx> <dz>: ignored");
                continue; // the next words are read as flags
            }
            MirrorSettings m;
            m.delayTicks = std::max(0, std::stoi(argv[++i]));
            m.dx = std::stoi(argv[++i]);
            m.dz = std::stoi(argv[++i]);
            mirrorArg = m;
        }
        else if (a == "--ghost-look") { // dev: the mirrored ghost's look (Megaman)
            if (i + 3 >= argc) {
                SDL_Log("--ghost-look needs <costume> <variant> <weapon>: ignored");
                continue; // the next words are read as flags
            }
            od::port::PlayerLook l;
            l.costume = uint8_t(std::stoi(argv[++i]));
            l.costumeVariant = uint8_t(std::stoi(argv[++i]));
            l.specialWeapon = int8_t(std::stoi(argv[++i]));
            ghostLookArg = l;
        }
        else if (a == "--net") {
            const std::string mode = argv[++i];
            if (mode == "off") netFlags.mode = NetMode::Off;
            else if (mode == "host") netFlags.mode = NetMode::Host;
            else if (mode == "join") netFlags.mode = NetMode::Join;
            else {
                SDL_Log("unknown --net mode %s (netplay off)", mode.c_str());
                netFlags.mode = NetMode::Off;
            }
        }
        else if (a == "--host") { // netplay: host a session on [port] (app/netplay, docs/netplay.md)
            netFlags.mode = NetMode::Host;
            if (i + 1 < argc && argv[i + 1][0] != '-') netFlags.port = std::stoi(argv[++i]);
        }
        else if (a == "--join" && i + 1 < argc) { // netplay: join the host at ip:port
            netFlags.mode = NetMode::Join;
            netFlags.join = argv[++i];
        }
        else if (a == "--net-name" && i + 1 < argc) netFlags.name = argv[++i];
        else if (a == "--net-key" && i + 1 < argc) netFlags.key = argv[++i];
        else if (a == "--net-sim") { // test instrument: <latency ms> <jitter ms> <loss %> on this end's datagrams
            if (i + 3 >= argc) {
                SDL_Log("--net-sim needs <latency ms> <jitter ms> <loss %%>: ignored");
                continue;
            }
            od::net::LinkSim sim;
            sim.latencyMs = std::max(0, std::stoi(argv[++i]));
            sim.jitterMs = std::max(0, std::stoi(argv[++i]));
            sim.lossPercent = std::clamp(std::stod(argv[++i]), 0.0, 100.0);
            netFlags.sim = sim;
        }
        else if (a == "--pt-speed") ptSpeed = std::max(1, std::stoi(argv[++i])); // scripted: logic ticks per frame
        else if (a == "--model-cull") g_modelsBothSides = std::stoi(argv[++i]) == 0;
        else if (a == "--cullsign") cullSign = std::stof(argv[++i]); // debug: -1 shows back faces
        else if (a == "--window" && i + 1 < argc) { // WxH: the window's size (e.g. the widescreen checks)
            if (std::sscanf(argv[++i], "%dx%d", &windowArgW, &windowArgH) != 2) windowArgW = windowArgH = 0;
        }
        else if (a == "--cam" && i + 5 < argc) {
            for (float& f : camArg) f = std::stof(argv[++i]);
            camGiven = true;
        }
    }
}

int App::init() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    if (const char* base = SDL_GetBasePath()) od::setConfigDir(base); // opendash.ini next to the executable
    cfg = od::loadConfig();
    // scripted / automated runs (--shot, --shot-every, --playscript, --doortest, the self-tests) render into a
    // hidden window and quit by themselves: nothing pops up, takes the focus or stays open; only plain interactive
    // launches show a window
    scripted = !shotPath.empty() || !playScript.empty() || doorTestArg >= 0 || motionSelfTest ||
                          resizeTest || !voiceTestArg.empty();
    // the safety net: a scripted run that has not ended after this many frames quits anyway
    scriptedFrameLimit = uint64_t(playScript.size()) * 4 + uint64_t(shotFrames) + 18000;
    // the player's copy of the game: asked for (a folder picker) when it is not where opendash.ini says
    if (!ensureGameFiles(cfg, !scripted)) return 1;
    // the display settings saved by the options page; --window WxH and the scripted runs: a window of that size /
    // the default. Created hidden, shown once in its mode (no window flashing at the default size first)
    video = videoFromConfig(cfg, SDL_GetPrimaryDisplay());
    if (windowArgW > 0 && windowArgH > 0) video.window = {windowArgW, windowArgH, 0};
    else if (scripted) video.window = kDefaultWindow; // not the player's saved window: the shots keep one size
    if (windowArgW > 0 || scripted) video.display = DisplayMode::Windowed;
    if (video.display != DisplayMode::Windowed) lastFullscreen = video.display;
#ifdef _WIN32
    constexpr SDL_WindowFlags kRendererWindow = 0; // D3D12 takes the HWND
#else
    constexpr SDL_WindowFlags kRendererWindow = SDL_WINDOW_VULKAN;
#endif
    window = SDL_CreateWindow("OpenDash (M1)", video.window.w, video.window.h,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | kRendererWindow);
    video = videoApply(window, video);
    if (!scripted) SDL_ShowWindow(window);
    SDL_GetWindowSizeInPixels(window, &width, &height);
    renderer = od::createRenderer(window, width, height);
    if (!renderer) {
        SDL_Log("renderer init failed");
        if (!scripted) // a windowed app has no console: say why it closes (the details are in opendash.log)
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenDash",
#ifdef _WIN32
                                     "Direct3D 12 could not start on this graphics card (Shader Model 6.0 "
                                     "required). Update the graphics driver; the details are in opendash.log next "
                                     "to opendash.exe.",
#else
                                     "Vulkan 1.3 could not start on this graphics card. Update the graphics "
                                     "driver (Mesa / the vendor's); the details are in opendash.log next to opendash.",
#endif
                                     window);
        return 1;
    }

    cam.cullSign = cullSign;
    bindMenuVramIo();
    fogMode = noFog ? od::FogMode::Off : od::FogMode::Remaster; // F1 Visual; the room's fog, if it has one
    ptView = &playerView;
    classicControls = classicArg; // F1: the original tank controls + the game's follow camera
    try {
        if (cfg.langDir.empty()) { // the language packs: lang/ next to the executable unless lang_dir= says otherwise
            const char* base = SDL_GetBasePath();
            cfg.langDir = std::filesystem::path(base ? base : "") / "lang";
        }
        if (!cfg.disc.empty()) // the PS1 disc image: the English pack (engine/lang_pack), the data with data=disc
            playerView.disc = std::make_shared<const od::DiscImage>(cfg.disc);
        if (cfg.data == "disc") {
            if (!playerView.disc) throw std::runtime_error("data=disc needs the disc image (disc= in opendash.ini)");
            data = std::make_unique<od::Ps1DataSource>(playerView.disc);
            SDL_Log("data: the PS1 disc %s", cfg.disc.string().c_str());
        } else {
            data = std::make_unique<od::PcDataSource>(cfg.datDir, cfg.exePath);
        }
        od::bindDataSource(data.get());
        { // netplay and the ghost mirror (app/netplay): the session runs between game ticks only
            if (mirrorArg && ghostLookArg) mirrorArg->look = ghostLookArg;
            NetSettings net;
            try {
                net = resolveNetSettings(cfg, netFlags);
            } catch (const std::invalid_argument& e) {
                SDL_Log("netplay settings: %s (netplay off)", e.what());
            }
            netplay.start(net, mirrorArg, cfg.datDir, cfg.exePath, cfg.data == "disc" ? playerView.disc.get() : nullptr);
        }
        stages = scanStages();
        for (const auto& si : stages) playerView.stageSubs.emplace_back(si.stage, si.subs);
        playerView.init(cfg);
        playerView.audioLog = audioLogArg;
        playerView.modern = !classicControls;
        playerView.logLock = !playScript.empty();
        playerView.logSkateCam = !playScript.empty();
        playerView.audioInit(cfg, audioArg && (!scripted || audioDeviceArg)); // scripted runs: silent (no device)
        playerView.langDir = cfg.langDir;
        playerView.restoredVoices = cfg.restoredVoices;
        playerView.setLanguage(od::lang::parseLanguage(langArg.empty() ? cfg.lang : langArg));
        {
            const std::string fm = fogArg.empty() ? cfg.fog : fogArg;
            if (!noFog) fogMode = fm == "off" ? od::FogMode::Off : fm == "classic" ? od::FogMode::Classic
                                                                                  : od::FogMode::Remaster;
            fogDistance = std::clamp(cfg.fogDistance, 0.25f, 4.0f);
            fogSource = cfg.fogSource == "ps1" ? FogSource::Ps1 : FogSource::OpenDash;
            waterRemaster = cfg.water != "classic";
            weatherRemaster = (weatherArg.empty() ? cfg.weather : weatherArg) != "classic";
            dissolve = cfg.dissolve;
        }
        if (const auto c = voiceTestArg.find(':'); c != std::string::npos && playerView.mixer) {
            auto& mx = *playerView.mixer;
            const std::string file = voiceTestArg.substr(0, c);
            const uint32_t track = uint32_t(std::stoul(voiceTestArg.substr(c + 1)));
            const auto data = mx.readVoiceTrack(file, track);
            const uint32_t h = mx.createStream(data, 0);
            mx.streamVolume(h, 127);
            mx.streamPlay(h);
            mx.takePeak();
            mx.advance(1.0);
            const auto pk = mx.takePeak();
            SDL_Log("voice-test %s track %u: %zu bytes (%.4s), playing %d, peak %.3f %.3f", file.c_str(), track,
                    data.size(), data.size() >= 4 ? reinterpret_cast<const char*>(data.data()) : "", int(mx.streamPlaying(h)),
                    pk[0], pk[1]);
            mx.destroyStream(h);
        }
    } catch (const std::exception& e) {
        SDL_Log("config failed: %s", e.what());
        if (!scripted) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenDash", e.what(), window);
        return 1;
    }
    playerView.room = &current; // transitions load rooms through main's reload (game/transition room setup)
    playerView.loadRoom = [&](int st, int sb) { reload(st, sb, true); };
    reload(stage, sub);
    // saves: the game's DASH2-00..02.DAT (next to dash2.exe; only ever read) and OpenDash's own (saves/ next to
    // opendash.exe; "Salvar" writes there, in the game's format)
    savesDir = std::filesystem::path(SDL_GetBasePath() ? SDL_GetBasePath() : "") / "saves";
    gameDir = cfg.exePath.parent_path();
    playerView.savesDir = savesDir;
    refreshSaves();
    setStorySave(storySaveArg.empty() ? cfg.storySave : storySaveArg);
    if (!loadArg.empty()) { // --load N or path
        leaveTitle(); // the display mode / pace the title would have set
        std::filesystem::path p = loadArg;
        if (loadArg.size() <= 2 && std::isdigit(static_cast<unsigned char>(loadArg[0]))) {
            char nm[24];
            std::snprintf(nm, sizeof nm, "DASH2-%02d.DAT", std::stoi(loadArg));
            p = gameDir / nm;
        }
        loadSave(p);
        if (teleportArg && playerView.play) { // --teleport / --yaw after the load (oracle comparisons)
            od::port::Player& pl = od::port::player();
            od::port::Fixed16* const pos[3] = {&pl.pos.x, &pl.pos.y, &pl.pos.z};
            od::port::Fixed16* const prev[3] = {&pl.prevPos.x, &pl.prevPos.y, &pl.prevPos.z};
            for (int k = 0; k < 3; ++k) {
                *pos[k] = int32_t(teleportPosArg[k] * 65536);
                *prev[k] = int32_t(teleportPosArg[k] * 65536);
            }
            if (teleportYawArg >= 0) {
                pl.rotation.y = int16_t(teleportYawArg & 0xfff);
                pl.prevRotation.y = int16_t(teleportYawArg & 0xfff);
            }
            SDL_Log("teleport after load: %d %d %d yaw %d", teleportPosArg[0], teleportPosArg[1], teleportPosArg[2],
                    teleportYawArg);
        }
        playerView.orbit(startOrbit, 0.0f);
    } else if (newGameArg >= 0) {
        leaveTitle(); // the display mode / pace the title would have set
        playerView.newGameStage = uint8_t(newGameStageArg);
        playerView.newGameSub = uint8_t(newGameSubArg);
        playerView.startGame(nullptr, uint8_t(newGameArg));
        playerView.orbit(startOrbit, 0.0f);
    } else if (startPlay && titleModeArg < 0) {
        leaveTitle(); // the display mode / pace the title would have set
        playerView.doorTest = doorTestArg;
        playerView.teleport = teleportArg;
        playerView.teleportYaw = teleportYawArg;
        std::copy(std::begin(teleportPosArg), std::end(teleportPosArg), playerView.teleportPos);
        playerView.enterPlay(current);
        playerView.orbit(startOrbit, 0.0f);
    }
    applyFlags();
    // dev starts (--stage + --story-save + story flags): the game would have loaded the room for that story
    // state -- when its files differ from the ones loaded, the room is loaded again and play restarted with them
    if (startPlay && titleModeArg < 0 && loadArg.empty() && newGameArg < 0 && playerView.play &&
        !flagArgs.empty()) {
        std::string g, t;
        if (od::port::stageDatFiles(current.stage, g, t) && g != current.roomFileName) {
            try {
                RoomMesh mesh = loadRoom(current.stage, current.sub);
                seeThrough.roomLoaded(mesh.tris, mesh.semiStart[3] ? mesh.semiStart[0] : mesh.tris.size());
                if (!renderer->upload(mesh.tris, mesh.slices, mesh.sliceCount, mesh.semiStart)) SDL_Log("upload failed");
                current = std::move(mesh);
                playerView.enterPlay(current);
                applyFlags();
            } catch (const std::exception& e) {
                SDL_Log("story-state reload failed: %s", e.what());
            }
        }
    }
    titleScreen.mv.fontDat = "INIT.DAT";
    titleScreen.gameDir = gameDir;
    titleScreen.savesDir = savesDir;
    titleScreen.bindHost(&playerView.fadeTask);
    if (playerView.ok && !playerView.play && !motionSelfTest &&
        (titleModeArg >= 0 || (!skipTitle && loadArg.empty() && newGameArg < 0 && !startPlay && !stageGiven))) {
        titleScreen.start(uint8_t(std::clamp(titleModeArg, 0, 5)));
        SDL_Log("title: main task from mode %d", std::clamp(titleModeArg, 0, 5));
    }
    if (camGiven) {
        for (int c = 0; c < 3; ++c) cam.eye[c] = camArg[c];
        cam.yaw = camArg[3];
        cam.pitch = camArg[4];
    }
    renderer->setDebugView(od::DebugView(debugView));
    applySun();
    {
        const std::string b = aaArg.empty() ? cfg.aa : aaArg, q = aaQualityArg.empty() ? cfg.aaQuality : aaQualityArg;
        if (!od::parseAaBackend(b, aaBackend)) SDL_Log("aa: unknown backend '%s', using auto", b.c_str());
        if (!od::parseAaQuality(q, aaQuality)) SDL_Log("aa: unknown quality '%s', using native", q.c_str());
        if (motionSelfTest) aaBackend = od::AaBackend::None; // the self-test reads raw scene targets
        // scripted / headless runs (--shot, --playscript, --doortest): no upscaler at all unless --aa is given -- the
        // DLSS init (NGX) makes the NVIDIA driver start nvngx_update.exe for every process
        if (aaArg.empty() && (!shotPath.empty() || !playScript.empty() || doorTestArg >= 0)) aaBackend = od::AaBackend::None;
        renderer->setAntiAliasing(aaBackend, aaQuality);
    }
    vsync = vsyncArg >= 0 ? vsyncArg != 0 : cfg.vsync;
    renderer->setVsync(vsync);
    { // the options page (game/status_options page 6: the status menu and the title's options) on OpenDash's settings
        od::port::OptionsHost& h = od::port::optionsHost();
        h.language = [&] { return int(playerView.language); };
        h.languageCount = [] { return 3; };
        h.setLanguage = [&](int l) {
            playerView.setLanguage(od::lang::Language(l));
            od::saveConfigValue("lang", od::lang::code(playerView.language));
        };
        h.rumble = [&] {
            if (!pad) return false;
            return SDL_GetBooleanProperty(SDL_GetGamepadProperties(pad), SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
        };
        h.padPressed = [&] {
            for (int n = 1; n <= 10; ++n)
                if (padButtonDown(pad, n)) return n;
            return 0;
        };
        h.keyPressed = [] { // FUN_0040a090: the lowest DirectInput code down with a name
            const bool* k = SDL_GetKeyboardState(nullptr);
            int best = 0;
            for (const auto& [dik, sc] : kDikKeys)
                if (k[sc] && (best == 0 || dik < best)) best = dik;
            return best;
        };
        h.mousePressed = [] {
            const SDL_MouseButtonFlags b = SDL_GetMouseState(nullptr, nullptr);
            if (b & SDL_BUTTON_LMASK) return 1;
            if (b & SDL_BUTTON_RMASK) return 2;
            return 0;
        };
        h.bindingsChanged = [&] {
            od::saveConfigValue("keys", bindingsText(od::port::inputBindings().keys));
            od::saveConfigValue("pad", bindingsText(od::port::inputBindings().pad));
            od::saveConfigValue("mouse", bindingsText(od::port::inputBindings().mouse));
        };
        // the video row: OpenDash's anti-aliasing (aa=), the resolution and the display mode (app/video_mode)
        h.aaCount = [] { return int(std::size(kAaBackends)); };
        h.aaName = [](int i) { return std::string(od::aaBackendName(kAaBackends[size_t(std::clamp(i, 0, int(std::size(kAaBackends)) - 1))])); };
        h.video = [&] { return currentVideo(); };
        h.videoModes = [&](DisplayMode mode) { return videoModes(window, mode); };
        h.videoApply = [&](const Video& v) { setVideo(v); };
        h.returnToTitle = [&] { // as the held start + select does (afterFrame): the title resumes after the fade
            od::port::softReset(playerView.fadeTask);
            softReset = true;
        };
        h.fade = [&](uint32_t type, uint8_t param) { od::port::fadeStart(playerView.fadeTask, type, param); };
        // the bindings saved by the key configuration (the exe's config load 0x54a824 after the defaults)
        bindingsLoad(od::port::inputBindings().keys, cfg.keys);
        bindingsLoad(od::port::inputBindings().pad, cfg.pad);
        bindingsLoad(od::port::inputBindings().mouse, cfg.mouse)
;
    }
    if (motionSelfTest) { // static scene, camera moved 40 units sideways and turned 0.05 rad
        od::Camera b = cam;
        b.eye[0] += 40.0f;
        b.yaw += 0.05f;
        float err = 0.0f, still = 0.0f;
        const bool ran = renderer->motionSelfTest(cam, b, err, still);
        const bool ok = ran && err <= 0.5f && still <= 0.01f;
        SDL_Log("motion selftest: %s, max err %.4f px, static max %.4f px", ok ? "ok" : "FAILED", err, still);
        renderer.reset();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return ok ? 0 : 1;
    }

    // Dear ImGui: stage / sub-room browser (F1) -- a development tool, not part of the original game
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // no imgui.ini next to the exe
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOther(window); // the renderer backend is ours (imgui_nvrhi)
    renderer->imguiInit();
    showMenu = (shotPath.empty() && !titleScreen.active) || forceMenu;
    playerView.afterPad = [&] { titleScreen.demoShown = od::port::demoPadFrame(); };
    playerView.gameOver = [&] {
        if (od::port::gameOverModeFrame(playerView.fadeTask)) softReset = true;
    };
    playerView.afterFrame = [&] {
        if (od::port::softResetFrame(playerView.fadeTask)) softReset = true;
    };
    animTick = startTick;
    skyDome = !sky2d;
    browseStage = stage;

    last = SDL_GetTicksNS();
    titleAt = last;
    return -1;
}

void App::reload(int newStage, int newSub, bool transition) {
    // a new room from the browser (F1, PgUp / PgDn, [ ]): out of play first -- the character's state kept for the
    // next F2 (leavePlay) before the room load writes over the game's memory
    if (!transition) {
        playerView.leavePlay();
        playerView.audioSilence();
    }
    try {
        RoomMesh mesh = loadRoom(newStage, newSub);
        seeThrough.roomLoaded(mesh.tris, mesh.semiStart[3] ? mesh.semiStart[0] : mesh.tris.size());
        if (!renderer->upload(mesh.tris, mesh.slices, mesh.sliceCount, mesh.semiStart)) SDL_Log("upload failed");
        {
            std::vector<od::DynamicObject> dyn;
            dyn.push_back({od::objectId(od::kKindPlayer, 0), playerView.build(mesh)});
            renderer->uploadDynamic(dyn);
            dynPrev = dynCur = dyn; // frames before the next tick must not redraw the old room's actors
        }
        playerView.snap = true; // object ids restart in the new room: no blending with the old one
        renderer->resetHistory();
        if (mesh.bgCleared) renderer->setClearColor(0.0f, 0.0f, 0.0f); // SetSkyGradient(0): isbg, black
        else renderer->setClearColor(mesh.clear[0], mesh.clear[1], mesh.clear[2]);
        renderer->setFog(fogFor(mesh, fogMode, fogDistance, fogSource));
        const float ext = std::max(mesh.hi[0] - mesh.lo[0], mesh.hi[2] - mesh.lo[2]);
        cam.eye[0] = (mesh.lo[0] + mesh.hi[0]) * 0.5f;
        cam.eye[1] = mesh.hi[1] + ext * 0.25f;
        cam.eye[2] = mesh.lo[2] - ext * 0.1f;
        cam.yaw = 0.0f;
        cam.pitch = -0.5f;
        stage = newStage;
        sub = newSub;
        mesh.camTris.clear(); // opaque surfaces stay on the CPU for the orbit camera's wall test
        for (size_t i = 0; i < std::min<size_t>(mesh.semiStart[0], mesh.tris.size()); ++i)
            mesh.camTris.push_back({mesh.tris[i].pos[0], mesh.tris[i].pos[1], mesh.tris[i].pos[2]});
        mesh.tris.clear();   // uploaded; keep only bounds and counters
        mesh.slices.clear();
        current = std::move(mesh);
        loadError.clear();
    } catch (const std::exception& e) {
        loadError = e.what();
        SDL_Log("load ST%02X sub %d failed: %s (set OPENDASH_DAT_DIR / OPENDASH_EXE or opendash.ini)",
                newStage, newSub, e.what());
    }
}

void App::refreshSaves() {
    saveList.clear();
    auto add = [&](const std::filesystem::path& p, bool own) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(p, ec)) return;
        SaveEntry e{p, {}, own};
        try {
            const auto b = od::readFile(p);
            e.valid = b.size() == od::port::kSaveSize && !od::port::saveChecksumBad(b);
            char buf[160];
            if (e.valid) {
                const uint32_t t = od::rd32(b.data() + 0x138); // play time, frames (60 / s)
                e.records = od::rd16(b.data() + 0x134);
                std::snprintf(buf, sizeof buf, "%s %s  ST%02X/%d  HP %d/%d  %u:%02u:%02u", own ? "opendash" : "jogo",
                              p.filename().string().c_str(), b[0x128], b[0x129], od::rd16(b.data() + 0x290),
                              od::rd16(b.data() + 0x292), t / 216000, t / 3600 % 60, t / 60 % 60);
            } else {
                std::snprintf(buf, sizeof buf, "%s %s  (invalido)", own ? "opendash" : "jogo",
                              p.filename().string().c_str());
            }
            e.label = buf;
        } catch (const std::exception&) {
            e.label = p.filename().string() + " (erro de leitura)";
        }
        saveList.push_back(std::move(e));
    };
    for (int n = 0; n < 3; ++n) {
        char nm[24];
        std::snprintf(nm, sizeof nm, "DASH2-%02d.DAT", n);
        add(gameDir / nm, false);
    }
    std::error_code ec;
    if (std::filesystem::is_directory(savesDir, ec))
        for (const auto& de : std::filesystem::directory_iterator(savesDir, ec))
            if (de.path().extension() == ".DAT") add(de.path(), true);
}

void App::setStorySave(const std::string& spec) {
    playerView.storySave.clear();
    playerView.sessionSave.clear(); // a story save chosen: F2 uses it, not the last session
    storySavePath.clear();
    storySaveLabel = "nenhum (historia 0)";
    std::string sp = spec;
    bool fallback = false;
    if (sp.empty()) {
        sp = "0";
        fallback = true; // the default: only when that save exists
    }
    if (sp == "none" || sp == "nenhum" || sp == "-1") return;
    std::filesystem::path p = sp;
    if (sp.size() <= 2 && std::all_of(sp.begin(), sp.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        char nm[24];
        std::snprintf(nm, sizeof nm, "DASH2-%02d.DAT", std::stoi(sp));
        p = gameDir / nm;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec)) {
        if (!fallback) SDL_Log("story save %s: not found (story 0)", p.string().c_str());
        return;
    }
    try {
        auto b = od::readFile(p); // read only
        if (b.size() != od::port::kSaveSize || od::port::saveChecksumBad(b))
            throw std::runtime_error("bad size / checksum");
        char buf[96];
        std::snprintf(buf, sizeof buf, "%s (historia %u)", p.filename().string().c_str(), unsigned(b[0x12c]));
        storySaveLabel = buf;
        storySavePath = p;
        playerView.storySave = std::move(b);
        SDL_Log("free play story: %s", p.string().c_str());
    } catch (const std::exception& e) {
        SDL_Log("story save %s: %s (story 0)", p.string().c_str(), e.what());
    }
}

void App::loadSave(const std::filesystem::path& p) {
    try {
        const auto b = od::readFile(p); // read-only
        if (playerView.startGame(&b, 0)) playerView.saveMessage = "carregado: " + p.filename().string();
    } catch (const std::exception& e) {
        playerView.playError = e.what();
    }
}

void App::applyFlags() {
    for (const uint32_t f : flagArgs) // --flag (FUN_00510e10)
        if (playerView.play && (f >> 3) < od::port::kStoryFlagBytes + od::port::kExtraFlagBytes) {
            od::port::setStoryFlag(f);
            SDL_Log("flag 0x%x set", f);
        }
    for (const uint32_t f : flagClearArgs) // --clear-flag (dev: start with a door unlocked)
        if (playerView.play && (f >> 3) < od::port::kStoryFlagBytes + od::port::kExtraFlagBytes) {
            od::port::clearStoryFlag(f);
            SDL_Log("flag 0x%x cleared", f);
        }
}

void App::leaveTitle() {
    if (titleScreen.active || titleScreen.inGame) {
        titleScreen.active = false;
        titleScreen.inGame = false;
        titleScreen.tasks = {};
        playerView.audioSilence();
    }
    // also with no title running (the command line's --load / --newgame / --play): the boot's pace byte is 0, which
    // the pacer runs as 1 (60 / s) and which never advances the soft reset's hold counter (FUN_005410b0)
    od::port::display().mode = 0;
    od::port::display().maskTicks = 1;
    od::port::display().pace = 2;
}

Video App::currentVideo() {
    Video v = video;
    v.aa = aaIndex(aaBackend);
    if (v.display == DisplayMode::Windowed) v.window = windowPixels(window); // resized by hand
    return v;
}

void App::setVideo(const Video& v) {
    const Video cur = currentVideo();
    if (v.display != cur.display || v.window != cur.window || v.exclusive != cur.exclusive) {
        video = videoApply(window, v);
        if (video.display != DisplayMode::Windowed) lastFullscreen = video.display;
        if (!scripted) videoSave(video);
    }
    if (v.aa != cur.aa) {
        aaBackend = kAaBackends[size_t(std::clamp(v.aa, 0, int(std::size(kAaBackends)) - 1))];
        renderer->setAntiAliasing(aaBackend, aaQuality); // an upscaler the GPU lacks falls back as aa= does
        if (!scripted) od::saveConfigValue("aa", od::aaBackendName(aaBackend));
    }
}

void App::applySun() {
    od::SunLight sun;
    sun.enabled = sunOn;
    const float az = sunAzimuth * 3.14159265f / 180.0f, el = sunElevation * 3.14159265f / 180.0f;
    sun.dir[0] = std::cos(el) * std::sin(az);
    sun.dir[1] = std::sin(el);
    sun.dir[2] = std::cos(el) * std::cos(az);
    sun.strength = sunStrength;
    renderer->setSun(sun);
}

void App::stepStage(int dir) { // PageUp/PageDown: neighbour stage, sub-room 0
    for (size_t i = 0; i < stages.size(); ++i)
        if (stages[i].stage == stage) {
            const size_t j = (i + stages.size() + size_t(dir > 0 ? 1 : stages.size() - 1)) % stages.size();
            reload(stages[j].stage, 0);
            return;
        }
    if (!stages.empty()) reload(stages[0].stage, 0);
}

int App::subCount(int st) {
    for (const auto& si : stages)
        if (si.stage == st) return si.subs;
    return 1;
}

void App::titleStep(uint16_t ps1) {
    for (; paceFrames > 0 && titleScreen.active; --paceFrames) { // the frames it runs are not the game loop's
        od::port::padUpdate(od::port::pad0(), ps1, 0x80, 0x80);
        std::vector<od::port::ScreenPrim> prims;
        ++od::port::gameState().frame; // 0x54a4dc: the frame counter (u16), before the task's frame
        try {
            g_menuVram = &titleScreen.mv; // the options page's VRAM writes (game/status_screen)
            od::port::setTitlePs1Layout(titleScreen.mv.titlePs1);
            od::port::titleTaskFrame(titleScreen.tasks, titleScreen.host, &prims);
            od::port::messageTick();
        } catch (const std::exception& e) {
            SDL_Log("title: %s -- back to the stage viewer", e.what());
            titleScreen.error = e.what();
            titleScreen.active = false;
            return;
        }
        titleScreen.prims = std::move(prims);
        titleScreen.processDats();
        od::port::fadeTick(playerView.fadeTask);
        od::port::displayMaskTick();
        od::port::display().drawBuffer ^= 1u;
        playerView.audioTick();
        ++titleScreen.frames;
        if (!titleScreen.tasks.switched) continue;
        od::port::titleTaskCommit(titleScreen.tasks);
        if (titleScreen.tasks.running != od::port::TaskEntry::GameLoop) continue;
        titleScreen.active = false;
        titleScreen.inGame = true;
        titleScreen.gameMode = od::port::mainTask().modeAndStep();
        const od::port::GameState& game = od::port::gameState();
        SDL_Log("title: game loop: ST%02X sub %d, load flag %d, entry %d, story %d, difficulty %d", int(game.stage),
                int(game.room), int(game.loadSave), int(game.entryMode), int(game.story),
                int(game.difficulty));
        if (playerView.startGameFromState()) {
            titleScreen.startDemo(); // FUN_00542460 at the stage load (0xa6d18e == 2: the demo)
        } else {
            SDL_Log("title: the game loop could not start (%s) -- back to the title", playerView.playError.c_str());
            (void)0;
            titleScreen.inGame = false;
            titleScreen.active = true;
            titleScreen.tasks = {};
            od::port::mainTask().enterMode(od::port::MainMode::ToTitle);
        }
        --paceFrames; // this frame was the title task's
        return;
    }
}

void App::titleResume() {
    if (softReset) {
        softReset = false;
        titleScreen.inGame = true;
    }
    if (!titleScreen.inGame || od::port::mainTask().modeAndStep() == titleScreen.gameMode) return;
    titleScreen.demoShown = false;
    playerView.sessionSave.clear(); // the title: the next game is the one it starts / loads
    SDL_Log("title: back to the main task, mode %d", int(od::port::mainTask().mode));
    playerView.audioSilence();
    playerView.play = false;
    titleScreen.inGame = false;
    titleScreen.active = true;
    titleScreen.tasks = {};
    od::port::mainTask().step = 0;
}

} // namespace od::app
