// The OpenDash host: the window, the renderer, the stage viewer, the play session (PlayerView), the title flow and
// the scripted runs. main() runs App::run: parseArgs, init, then frame() until the window closes (or a scripted run
// ends), then shutdown.
//   app/main.cpp          main, run, the frame (events, clock, the fixed-step loop), shutdown
//   app/app_setup.cpp     the command line, init (window, renderer, game, saves, title, options page), room reloads
//   app/app_frame.cpp     the simulation step, the camera, the scene upload, the 2D overlays, the present / shots
//   app/playthrough.cpp   --playscript: its steps, the playthrough waits / timeline, the dev bots
//   app/debug_ui.cpp      the F1 window (stage browser, play, saves, video / audio settings)
#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "app/ghost_view.hpp"
#include "app/input.hpp"
#include "app/netplay.hpp"
#include "app/play_session.hpp"
#include "app/room_mesh.hpp"
#include "app/game_pacer.hpp"
#include "app/title_screen.hpp"
#include "app/video_mode.hpp"
#include "app/ui/hud_ui.hpp"
#include "app/ui/message_ui.hpp"
#include "app/ui/prim_painter.hpp"
#include "app/ui/vector_ui.hpp"
#include "engine/config.hpp"
#include "engine/pc_data_source.hpp"
#include "engine/ps1_data_source.hpp"
#include "render/renderer.hpp"
#include "render/ui_canvas.hpp"

namespace od::app {

constexpr uint64_t kSimStepNs = 1'000'000'000ull / 30; // game logic runs at 30 Hz
// --playscript wait markers (no real pad state has every bit set): see parsePlayScript / ptNextPad
constexpr uint16_t kPtCalm = 0xffff, kPtMash = 0xfffe;
constexpr int kPtWaitLimit = 30000;
// "talk N *": the class word of every live pool-0x20 actor in turn, and the bot frames one gets before the next
constexpr uint32_t kTalkEach = 0xffffffffu;
constexpr size_t kTalkEachTicks = 600;

struct StageInfo {
    int stage;
    int subs; // valid sub-grids = sub-rooms
};

struct App {
    // ---- the command line (parseArgs) ----
    int stage = 0x08, sub = 0;
    std::string shotPath;
    int shotFrames = 5;
    int shotEvery = 0; // --shot-every N: in play, also a numbered shot every N sim ticks (<shot>_tNNNN.bmp; scripted)
    uint64_t shotFrom = 0; // --shot-from T: the --shot-every shots start at sim tick T (frame-by-frame windows)
    float cullSign = 1.0f;
    bool forceMenu = false;
    size_t startTick = 0;
    bool sky2d = false;
    bool noFog = false;
    std::string fogArg; // --fog: over opendash.ini fog=
    // experimental ray-traced sun shadows: --sun <azimuth deg> <elevation deg> (0 0 = off), F1 "Sol"
    float sunAzimuth = 45.0f, sunElevation = 50.0f, sunStrength = 0.45f;
    bool sunOn = false;
    float shotZoom = 1.0f;
    float shotOrbit = 0.0f; // --shot-orbit A: dev, the --shot-every shots' camera turned A radians around the player
    bool startPlay = false;
    std::string loadArg;   // --load N (the game's DASH2-0N.DAT, read-only) or a file path: start from that save
    int newGameStageArg = 0x39, newGameSubArg = 0; // --newgame-stage SS sub (dev): the room a new game starts in
    int newGameArg = -1;   // --newgame D: start a new game with difficulty D (title menu: 1 unless the game was cleared)
    bool skipTitle = false; // --skip-title: the stage viewer at start (the development start before the title port)
    int windowArgW = 0, windowArgH = 0; // --window WxH
    int titleModeArg = -1;  // --title-mode M: start the main task at mode M (0 boot = default, 2 title, 4 attract)
    bool stageGiven = false;
    int saveGameArg = -1;  // --savegame N: at the end of --playscript, save to OpenDash's saves/DASH2-0N.DAT
    bool audioArg = true, audioLogArg = false;
    int doorTestArg = -1;    // --doortest k (see PlayerView::doorTest)
    bool hdArtArg = true;           // --hd-art 0: the containers' pixel art (F1 "paineis HD")
    bool classicArg = false;        // --classic 1: tank controls + the game's camera (as F1 "controles classicos")
    std::string storySaveArg;       // --story-save N|path|none (see PlayerView::storySave; read-only)
    std::vector<uint32_t> flagArgs;  // --flag N: event flags set after the start (dev: e.g. 0x6ff opens a shop)
    std::vector<uint32_t> flagClearArgs; // --clear-flag N: event flags cleared after the start (dev: 0x711 opens ST1E's door)
    bool teleportArg = false; // --teleport x y z (see PlayerView::teleport)
    int teleportYawArg = -1;  // --yaw (with --teleport)
    int teleportPosArg[3]{};
    float startOrbit = 0.0f; // --orbit: radians added to the orbit camera yaw at play start (scripted tests)
    float orbitSpin = 0.0f;  // --orbit-spin r: orbit input of r radians / s every frame (a mouse held moving; scripted)
    float orbitTilt = 0.0f;  // --orbit-tilt r: the same for the pitch (dev: the camera aim evidence)
    std::vector<uint16_t> playScript; // scripted PS1 buttons per tick (--playscript)
    size_t playScriptPos = 0;
    size_t talkEachSlot = 0, talkEachTicks = 0; // "talk N *": the pool-0x20 slot talked to, its bot frames
    bool talkEachWasBusy = false;               // its talk ran (the next idle frame moves on)
    // "tp X Y Z YAW" steps of --playscript: the player teleported (position + previous position, yaw) before the
    // script tick at that index
    std::map<size_t, std::array<int, 4>> playScriptTeleports;
    // dev bots (verification runs: the player turned / moved / buttons pressed as a player would; no game state cheats
    // beyond --god):
    // "aim N CLASS... [~HEX] [*]": N ticks of lock-on (R2) + buster taps (square; "*": the special weapon, triangle),
    //   turned toward the nearest live actor of those classes (pools 0x20 / 0x60), walking to it past ~HEX (0x280)
    // "catch N CLASS @ANCHOR ^FACING [!]": guard a point from the anchor toward FACING, lift what comes near (triangle
    //   held) and throw it (lock R2 on one heading for the anchor, triangle)
    // "guard N CLASS @ANCHOR ^FACING [!]": the same point, lock + special weapon at the ones heading for the anchor
    // "!": the anchor's life refilled every tick (as --god does for the player: an ally the bot cannot fully protect)
    struct AimSeg { size_t from, to; std::vector<uint32_t> cls; uint32_t anchor = 0, facing = 0; bool catcher = false,
                    guard = false, protect = false, spray = false, ride = false, route = false, strafe = false, go = false, talk = false, skate = false, walk = false, lift = false;
                    int reach = 0x280;
                    int tgt[3] = {}; std::vector<std::pair<int, int>> rooms; };
    std::vector<AimSeg> playScriptAims;
    bool camGiven = false;
    uint64_t nextEveryShot = 0;
    int debugView = 0;          // --view final|color|motion|normal|depth|reactive (index of od::DebugView)
    bool fpsLog = false;         // --fps-log: log frames per second + average frame time once a second
    bool motionSelfTest = false; // --motion-selftest: render two camera poses, check the motion vectors, exit
    bool resizeTest = false;     // --resize-test: in --shot mode, resize to 640x360, 1x1, back, reset history
    std::string aaArg, aaQualityArg; // --aa / --aa-quality override opendash.ini
    std::string langArg;             // --lang jp|en|ptbr overrides opendash.ini lang=
    bool audioDeviceArg = false;     // --audio-device 1: scripted runs open the real audio device (shutdown test)
    std::string voiceTestArg;        // --voice-test xa10_18.dat:3 -- play one voice-pack track (silent mixer), log it
    float camArg[5]{};
    std::string saveFileArg; // --savefile PATH (playthrough segments: the next one --load's it)
    bool godArg = false;     // --god 1 (playthrough tests only): the player's life 0xa71d90 := max 0xa71d92 before every
    int ptSpeed = 0;         // --pt-speed K: a scripted run advances K logic ticks per frame (0: the --shot / real clock)
    // the ghost mirror (dev / test instrument for the ghost view, app/netplay): --ghost-mirror <delay ticks> <dx> <dz>
    // feeds a ghost slot with the local player's state of `delay` logic ticks ago, moved by (dx, dz) game units;
    // --ghost-look <costume> <variant> <weapon> gives that ghost another look
    std::optional<MirrorSettings> mirrorArg;
    std::optional<od::port::PlayerLook> ghostLookArg;
    NetFlags netFlags; // --host / --join / --net-name / --net-key / --net-sim (over opendash.ini's net keys)
    // ---- the window, the renderer, the room, the game (init) ----
    int width = 0, height = 0;
    // scripted / automated runs (--shot, --shot-every, --playscript, --doortest, the self-tests) render into a
    // hidden window and quit by themselves: nothing pops up, takes the focus or stays open; only plain interactive
    // launches show a window
    bool scripted = false;
    // the safety net: a scripted run that has not ended after this many frames quits anyway
    uint64_t scriptedFrameLimit = 0;
    uint64_t scriptedFrames = 0;
    bool scriptEnded = false;
    bool scriptFailed = false;
    // ---- playthrough segments ----
    uint64_t ptTicks = 0;      // logic ticks of the script so far
    std::string ptLast;        // the last timeline state printed
    uint32_t ptCalmRun = 0, ptWait = 0;
    PlayerView* ptView = nullptr; // set once playerView exists
    SDL_Window* window = nullptr;
    Video video;                                       // the display settings in effect (aa: index into kAaBackends)
    DisplayMode lastFullscreen = DisplayMode::Borderless; // what Alt+Enter goes back to from the window
    std::unique_ptr<od::Renderer> renderer;
    od::Camera cam{};
    od::Camera drawCam{}; // cam in the endless-track travel frame (+ PlayerView::renderOffset): what the renderer gets
    VectorUi vectorUi;  // the vector UI of the 2D screens (shared by the painters)
    // the mouse on the sub-screens (game/menu_pointer MenuPointer): the row under it, a click's press waiting for its tick
    int hoverRow = -1;
    uint16_t clickPress = 0; // PS1 bit: 0x2000 confirm (left), 0x4000 cancel (right)
    int clickDelay = 0;      // sim steps before the press (the cursor first moves onto the clicked row)
    bool subScreenOpen() const;
    bool pointerScreen() const; // a sub-screen or a message window's choice: the mouse is free and picks

    // the cursor list row drawn under window pixel (x, y), -1 = none
    int menuRowAt(float x, float y) const;
    void menuMouse(const SDL_Event& e);
    bool titleMouse(const SDL_Event& e); // the title's menus (TitleScreen::rowAt); true = the event was theirs
    ui::Canvas canvas; // the game's 2D layer this frame (menus, windows, HUD, fades); ImGui = the F1 panels only
    MessageUi messageUi;
    PrimPainter menuPainter; // the status sub-screen's packets (game/status_screen)
    HudUi hudUi;
    od::FogMode fogMode = od::FogMode::Remaster; // F1 Visual; the room's fog, if it has one
    float fogDistance = 1.0f;
    FogSource fogSource = FogSource::OpenDash; // F1 Visual: the fog of a room with no range of its own (the disc's rooms)
    bool waterRemaster = true; // F1 Video: the underwater look (remaster / the PS1 screen wave)
    od::Config cfg;
    // the game's data (DAT/ and dash2.exe read as files: engine/pc_data_source), bound for the whole run (before
    // playerView: it outlives everything that reads it)
    std::unique_ptr<od::DataSource> data; // the PC install's (engine/pc_data_source) or the disc's (ps1_data_source)
    PlayerView playerView;
    GhostView ghosts; // the ghost players (posed after the tick loop, drawn next to the player)
    Netplay netplay;  // the session and the ghosts' sources (app/netplay)
    bool classicControls = false; // F1: the original tank controls + the game's follow camera
    std::vector<StageInfo> stages;
    RoomMesh current;
    std::string loadError;
    std::vector<od::DynamicObject> dynPrev, dynCur; // dynamic geometry (one object per actor) of the last two ticks
    // saves: the game's DASH2-00..02.DAT (next to dash2.exe; only ever read) and OpenDash's own (saves/ next to
    // opendash.exe; "Salvar" writes there, in the game's format)
    std::filesystem::path savesDir, gameDir;
    struct SaveEntry {
        std::filesystem::path path;
        std::string label;
        bool own = false, valid = false;
        uint16_t records = 0; // +0x134 (0xa6d19c, merged with max on load; the title offers difficulties when != 0)
    };
    std::vector<SaveEntry> saveList;
    // free play's story state (F2 / the stage browser): a user save's story + event flags, read only (opendash.ini
    // story_save=, --story-save, F1 "historia do jogo livre"); unset = DASH2-00 when present; nenhum / none = story 0
    std::string storySaveLabel = "nenhum (historia 0)";
    std::filesystem::path storySavePath;
    TitleScreen titleScreen; // the boot / title flow (game/title): the default start
    GamePacer pacer;         // the exe's frame pacer: when the title / game frames run (app/game_pacer)
    int paceFrames = 0;      // this sim step's game frames (title task or the game loop)
    // the title task or a run it started: the steps follow the exe's pacer, not the 30 Hz grid
    bool paced() const { return titleScreen.active || titleScreen.inGame; }
    od::AaBackend aaBackend = od::AaBackend::Auto;
    od::AaQuality aaQuality = od::AaQuality::NativeAA;
    SDL_Gamepad* pad = nullptr; // the first gamepad (opened in the loop)
    bool showMenu = false;
    bool softReset = false;
    size_t animTick = 0, shownTick = 0;
    bool skyDome = true; // sky layers on a sphere (default) or the original 2D screen-space scroll
    int browseStage = 0;

    Input input;
    uint64_t simTicks = 0, accumulator = 0, last = 0, titleAt = 0;
    bool running = true;
    int frames = 0;    // the frames drawn (--shot counts them)
    int fpsFrames = 0; // the frames since the window title's last fps
    bool windowFocused = true, escReleased = false, playCapture = false, captureClick = false; // mouse capture in play mode (see below)
    // this frame (App::frame): its clock, the step's length, the blend of the last two ticks, the camera's source
    uint64_t now = 0;
    float dt = 0.0f, alpha = 0.0f;
    const char* camSource = "orbit";

    int run(int argc, char** argv);

    // ---- app/app_setup.cpp
    void parseArgs(int argc, char** argv);
    // -1: go on; else the process' exit code (SDL / renderer failures, --motion-selftest)
    int init();
    // (re)load a stage / sub-room; the camera restarts above the -Z edge looking in and down
    void reload(int newStage, int newSub, bool transition = false);
    void refreshSaves();
    void setStorySave(const std::string& spec);
    void loadSave(const std::filesystem::path& p);
    void applyFlags();
    // F1 "Novo jogo" / "Carregar save" / F2 / the command line start play outside the title flow: stop the title / its game loop and
    // give the game its display mode back (Maybe_SetDisplayMode(0): mode 0, pace 2 = 30 / s) -- otherwise the
    // title's 640 x 480 pace 1 (60 / s) made the game run at twice the speed
    void leaveTitle();
    // azimuth 0 = +Z, 90 = +X; elevation above the horizon
    void applySun();
    // the options page's video row / Alt+Enter: the display mode and resolution (app/video_mode) when they changed,
    // the anti-aliasing when it changed; persisted unless the run is scripted
    void setVideo(const Video& v);
    Video currentVideo(); // the settings in effect, the window's live size (resized by hand) included
    void stepStage(int dir);
    int subCount(int st);
    // one sim step of the main task / title task (the task's frames by the pace byte 0x6d0005): pad, the task, the
    // windows (FUN_005578a0), the DAT loads, the fade task, the frame flags, the sound update; a switch to the game
    // loop starts PlayerView's play on the state the title left
    void titleStep(uint16_t ps1);
    // the game loop handed the frame back to the main task (*0xa7a000 changed: the end of the attract, the soft
    // reset (start + select held), ...)
    void titleResume();

    // ---- app/main.cpp
    void frame();
    void handleEvents();
    void shutdown();

    // ---- app/app_frame.cpp
    // one step: input, then `gameFrames` frames of the title task / the game loop (paced: 1 per pacer iteration;
    // the viewer's 30 Hz steps: 0, the preview animation clock only)
    void simStep(int gameFrames);
    void updateCamera();
    void updateScene();
    void drawOverlays();
    void present();

    // ---- app/playthrough.cpp --playscript steps: "<ticks> <btn+btn|->", "tp X Y Z YAW", and the playthrough
    // waits: "calm" = no input until field play is calm (kPtCalm), "mash" = tap circle (1 of 10 ticks) until calm
    // the state line: game mode, stage / sub, story, script, request, event, player state, position / 256
    std::string ptState();
    // field play with control: mode 3 / 2, no request, no room script, no event, player state 1 (RuinDriver.step)
    bool ptCalm();
    // the next pad of --playscript: a wait marker holds the script (no input / circle taps) until 4 calm ticks
    uint16_t ptNextPad();
    void parsePlayScript(const std::string& text);
    // the dev bots of the current --playscript tick: the pad bits they add (approach / back / side / buttons)
    void botStep(bool& aimApproach, bool& aimBack, uint16_t& aimSide, uint16_t& aimPad);
    // --playscript: the trace of the player's state, the playthrough timeline, the end of the script (logs, saves)
    void playscriptLog();

    // ---- app/debug_ui.cpp
    void drawDebugMenu();
};

} // namespace od::app
