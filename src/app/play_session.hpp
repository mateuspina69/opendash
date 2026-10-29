// PlayerView: the loaded files the port runs on and everything the host does around it -- play mode (the
// rooms, the actors, the audio, the hooks, save / load, the logic tick), the cameras (app/cameras.cpp) and the
// scene the renderer draws (app/scene_builder.cpp). Member functions in app/play_session.cpp.
#pragma once

#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "app/menu_vram.hpp"
#include "app/room_mesh.hpp"
#include "audio/mixer.hpp"
#include "engine/config.hpp"
#include "engine/file_data.hpp"
#include "engine/disc_image.hpp"
#include "engine/lang.hpp"
#include "engine/lang_pack.hpp"
#include "engine/screen_prim.hpp"
#include "game/actors.hpp"
#include "game/audio_logic.hpp"
#include "game/draw_command_decode.hpp"
#include "game/draw_commands.hpp"
#include "game/effects_draw.hpp"
#include "game/hud.hpp"
#include "game/actor_movement.hpp"
#include "game/player/player_state.hpp"
#include "game/save_image.hpp"
#include "content/intro/crash_landing_draw.hpp"
#include "game/fade.hpp"
#include "game/transition.hpp"
#include "game/weather_draw.hpp"
#include "render/renderer.hpp"

namespace od::app {

struct PlayerView {
    static constexpr uint32_t kPlayer = 0xa71d20, kFrameByte = 0x00100000;
    // the loaded files (the stage's banks, the player's files, the messages, the room block): bound for the
    // PlayerView's lifetime (the previous binding restored after)
    od::LoadedFileData files;
    od::ScopedLoadedFiles filesBinding{files};
    std::array<uint8_t, 0x10> frameKey{}; // kFrameByte: the animation viewer's key (the frame shown), built data
    // kStageParams: the room record the host builds for a room without a known one (the coordinate shift only)
    std::array<uint8_t, 0x24> stageParams{};
    od::FileDataBlock roomBlock; // kRoomBlock: the room block, shared with the loaded files and the collision
    bool ok = false;
    int anim = 0, frame = 0;
    bool playing = true;
    std::vector<int> frames; // frame count of each animation (table deltas / 64 bytes)

    // Maybe_RequestPlayerFiles 0x5d5c40 (the game calls it in the stage load 0x541295): the body / weapon files the
    // game state asks for are mapped into the PS1 image (meshes, animation tables, the weapon's arm animations)
    void playerFiles();
    void init(const od::Config& cfg);
    void tick();

    // ---- play mode: the ported game logic drives the player struct and the camera ----------------------
    bool play = false;
    // game logic ticks run in play mode and when the last one ran: the render blends the poses of the last two LOGIC
    // ticks (the pace byte 0x6d0005 makes them 60 / 30 / 20 per second; under water 20, two per three 30 Hz steps)
    uint64_t logicTicks = 0, logicNs = 0;
    std::string playError;
    od::port::PlayerEnv env;

    // The room the logic runs in, also during transitions (the room setup hook): the native actor spawn, message
    // bank, stage / sub bytes, and the room block mapped into the loaded files where
    // the transition scripts reach it like the exe (0x6d0084 pool, 0x6d0088 cells, 0x6d008c segments: the door
    // scripts switch segment flags, FUN_00600f10) -- the collision reads the same bytes.
    static constexpr uint32_t kRoomBlock = 0x7e000000;   // OpenDash's copy of the room block
    static constexpr uint32_t kStageParams = 0x7fe00000; // *0xa6d208: stage render parameters (+3 = shift)
    static constexpr uint32_t kBank = 0x7d000000;        // OpenDash's copy of the model bank (*0x6d0074)
    int bankStage = -1;
    // language (F1 / --lang / opendash.ini lang=): Japanese = the PC data; English / Portuguese = the PS1 packs
    // (engine/lang): message banks of the same stem as the room DAT, PS1 font, ODXA voice packs
    od::lang::Language language = od::lang::Language::Japanese;
    std::filesystem::path langDir;
    // the PS1 disc image (opendash.ini disc=): the English pack is read off it (engine/lang_pack), the other packs
    // from their folders in langDir
    std::shared_ptr<const od::DiscImage> disc;
    bool discData = false;
    std::shared_ptr<const od::lang::LangPack> pack; // the language's pack (nullptr: Japanese; shared with the mixer)
    std::unique_ptr<od::lang::LangPack> makePack(od::lang::Language l) const; // nullptr when there is none
    bool packOffered(od::lang::Language l) const; // the language can be chosen (Japanese, or its pack exists)
    // the pack OpenDash's Latin labels (the language row) are drawn with: PT-BR's (EN's glyphs + the accents), else EN's
    const od::lang::LangPack* labelPack();
    mutable std::map<od::lang::Language, bool> packOfferedCache;
    std::unique_ptr<od::lang::LangPack> labels;
    bool labelsTried = false;
    od::lang::Language bankLanguage = od::lang::Language::Japanese;
    std::shared_ptr<const od::GameFile> bankRoomFile; // the room file whose bank is loaded
    std::string bankRoomName;
    // message bank -> 0x73c000 (the game memcpys the room DAT's type-6 segment there at stage load)
    void loadMessageBank();
    // AI-restored voices of the pack: searched before the pack's own snd/, which keeps every clip they leave
    // out
    bool restoredVoices = true;
    bool restoredAvailable() const;
    void applyVoiceDirs();
    void setLanguage(od::lang::Language l);
    // the banks the PC keeps in dash2.exe (item names / descriptions, the status sub-screen texts): Japanese = the exe's
    // own; a language pack's PS1 copies are mapped at kLangBanks and the message VM (fb 20 / fb 3d) and game/menu_banks
    // pointed at them. A missing file keeps the exe's bank (Japanese).
    static constexpr uint32_t kLangBanks = 0x7c800000;
    static constexpr uint32_t kLangBanksSize = 0x20000;
    bool startWasDown = false;
    void loadExeBanks();
    uint32_t bankAt = 0;        // where the bank is mapped
    int roomStage = -1, roomSub = -1; // the room bound to the logic
    // ---- the room's actors: game/stage_setup (the game's own stage / room setup with the current story and flags) --
    std::vector<uint8_t> storySave;    // --story-save N: free play with this save's story state / flags
    // F2 out of play: the live game as a save image (the save menu's builder); the next F2 enters with it, before
    // storySave -- the character (equipment, weapons, items, zenny, life) and the flags stay the session's. Dropped
    // by a new game / a load / the title
    std::vector<uint8_t> sessionSave;
    std::filesystem::path savesDir;    // OpenDash's saves (the in-game save menu writes here; never the game's dir)
    int spawnStage = -1;               // the stage whose stage setup (class tables ...) ran last
    // a live actor whose class the port lacks: logged as PORT GAP (after the room setup and the room-script spawns)
    void reportUnported();
    // the port's room setup (FUN_005d5b30's actor part with game/stage_setup), after the room block is mapped
    // (throws when the port lacks a branch: play stops with the error)
    void spawnNative(const RoomMesh& mesh, int param);
    // OpenDash guard (not the game's): a special weapon (triangle; arm FUN_00572910, its shot class) whose arm handler
    // or shot class is not ported would stop play mode, so its slot is emptied as in towns. Every weapon 1..17 is
    // ported (arms 0x572aa0 / 0x572c50 / 0x572d10 / 0x572e50 in game/player/player_weapons; weapon 10 = the
    // Shield Arm has no arm handler, it is the charge-hold path of Maybe_UpdatePlayer) -- this only catches bad ids
    void weaponPolicy();
    // The animated CLUTs (Maybe_ClutAnimationUpdate 0x55da50 writes the output copy 0xa76400): 0xa74400 mirrors VRAM
    // rows 240..247 and 0xa75400 rows 496..503 (the CLUT uploads' copies, 0x40edb0 modes 1 / 2) and 0xa76400 starts
    // as that copy; every frame 0x417210
    // uploads 0xa76400 rows 0..7 to VRAM (0, 240) 256 x 8 (unless 0xa743e0 / 0xa7172c) and rows 8..15 to (0, 496).
    // Here: the room VRAM gets the same uploads, and the room slices whose CLUT row changed are re-decoded.
    std::vector<uint8_t> clutShown; // 0xa76400 as last applied (0x2000 bytes)
    void clutAnimationMirror();
    // the image moves the animated VRAM tiles queued this frame (game/vram_moves: ST48's sand pit 0x4262e0), applied
    // to the room VRAM like the GPU runs FUN_0040b710's packets; the room slices whose texture page or CLUT row the
    // destination touches are re-decoded
    void vramMovesApply(const RoomMesh& mesh, od::Renderer& r);
    void clutAnimationSync(const RoomMesh& mesh, od::Renderer& r);

    // param: FUN_005d5b30's (>= 0: room / stage load, the actors are killed first; < 0: in-stage switch)
    void bindRoomData(const RoomMesh& mesh, int param = 0);

    // ---- audio: game/audio_logic (the game's sound logic, bit-exact) -> audio/mixer (SDL3 playback) ------------
    // Boot: Maybe_SoundSystemReset with the masters of the save (0x7f / 0x7f), INIT.DAT's global SFX bank (slot 0).
    // Stage load (the game's FUN_005d5a80, collapsed into OpenDash's loader): the fade-out of Maybe_GameModeStage-
    // Transition substate 2, the type-8 banks of the stage DAT and the type-9 MP3 slots of its texture DAT
    // (container dispatch 0x40ed0e), then the per-stage setup's sound calls (game/stage_music; its voice-clip table
    // writes are game/stage_setup stageSetup's). The special-weapon bank (slot 3, PL00R##) is not loaded (special
    // weapons are disabled in play mode).
    std::unique_ptr<od::audio::Mixer> mixer;
    od::port::RecordingSoundBackend soundRec;
    bool audioLog = false, audioMute = false;
    float audioVolume = 0.8f;
    int audioStage = -1;
    uint32_t audioTicks = 0;
    int voiceStarts = 0, streamStarts = 0, requests = 0;
    void audioInit(const od::Config& cfg, bool device);
    void audioBindRoom(const RoomMesh& mesh);
    // Maybe_SoundUpdatePerFrame once per game tick (WinMain runs it after the game task)
    void audioTick();
    // leaving play mode: everything the logic plays stops (Maybe_SoundStopAll 0x5d5920)
    void audioSilence();
    // play-mode start: the room + a quiet game state (no event, no transition, field mode 3/2)
    void bindRoom(const RoomMesh& mesh);

    // Transitions: game/transition (FUN_00541070 frame: field mode FUN_005413a0 with the exit request 0xa6d2e0, the
    // transition scripts of the exit kind, fades; mode 2 / 1 room / stage loads). OpenDash's own loader runs where
    // the game sets the room up (FUN_005d5b30's per-stage setup: hooks.roomSetup) or loads a stage (mode 1).
    od::port::TransitionHooks hooks;
    od::port::FadeTask fadeTask;
    std::function<void()> afterPad, afterFrame; // Maybe_GameModeLoop's frame around the mode handler (main(): title)
    std::function<void()> gameOver;             // Maybe_GameModeLoop mode 8 (main(): game/title)
    std::function<void(int stage, int sub)> loadRoom; // main(): reload(stage, sub, true)
    const RoomMesh* room = nullptr;                   // main()'s current room
    bool snap = false;        // the next frames must not blend with the previous room
    // the endless-track travel frame (render side): everything built in game coordinates (dynamic objects, overlays,
    // the sky dome, the camera) is drawn at + renderOffset. Each wrap of a looping track (hooks.warp) moves the game
    // back by the exit delta and renderOffset forward by it, so nothing on screen ever jumps; the static room is the
    // continuous strip loadRoom built (RoomMesh::trackPeriod). 0 outside those rooms.
    bool warpPending = false; // a warp happened this tick (enterRoomOfState: the light room setup)
    float renderOffset[3] = {0.0f, 0.0f, 0.0f};
    int warpLogTicks = 0; // dev (OD_TRAVELLOG): the live actors logged on the ticks around a warp
    void warpActorLog(const char* when) const;
    uint8_t cutScript = 0, cutCamMode = 0; // last tick's script-running bit / camera mode (cut detection)
    // screen darkening 0..1 from the game's fade state (0xa72268 / 0xa7226e, wipes: dark)
    float fade() const;
    bool cutsceneHold() const; // black before a cutscene script takes the screen (see fade())
    bool fadeWhite() const; // fade types 9..0xc / 0x19..0x1c
    // the room in 0xa6d190 / 0xa6d191 (set by the transition) loaded and bound to the logic
    void enterRoomOfState(int param = 0);
    void setupHooks();
    // ---- actor system (game/actors): the ported classes run (a missing one is a PORT GAP) --------------------------
    od::port::ActorContext actors;
    // the actor's class is ported (and not disabled): it updates itself, submits its own solid, is drawn live.
    // updating = false (draw passes): also while frozen (byte 1 bit 0 off: a mission's opening message runs
    // FUN_005e05d0(0xffe, 1)); the exe's Maybe_DrawActorPools 0x5f3fd0 draws by byte 0 & byte 1 & 2 (6), not bit 0
    bool actorLive(uint32_t a, bool updating = true) const;
    bool flagSet(uint32_t n) const;
    void setFlag(uint32_t n, bool on);
    // ---- message windows (game/message: logic only, no text rendering yet) ----------------------------------
    uint32_t lastPage = 0;  // window 0 page start last logged
    uint32_t lastChoice = 0;
    void logMessages();
    // Start point of play mode, valid for any room:
    //  1. arrival points of the exits leading into this stage / sub-room (every room's exit table, game/stage_setup
    //     game/room_exits: dest stage +6, sub +7, x/y/z/yaw +0x10), moved inside along the arrival facing (-sin, -cos):
    //     the destination is the door's threshold, the game walks the player in;
    //  2. else a floor point of the room's grid nearest to its centre.
    // A point is accepted when the collision has floor under it at its height and on a ring around it (not an
    // edge, not the top of a wall).
    std::string spawnFrom; // how the last spawn was chosen (log / playcheck)
    std::vector<std::pair<int, int>> stageSubs; // (stage, sub-room count) of every ST##.DAT (App::stages)
    int doorTest = -1;     // --doortest: start in front of this exit of the room
    bool teleport = false; // --teleport x y z
    int teleportPos[3]{};
    int teleportYaw = -1;   // --yaw with --teleport
    bool chooseSpawn(const RoomMesh& mesh, int32_t pos[3], int16_t& yaw);
    // scripted runs: where the player ended and whether it still stands on the room's floor
    void logCheck(const RoomMesh& mesh) const;
    // effects / HUD evidence for scripted runs: a line whenever the live effect set or the HUD values change
    std::string fxLast, actLast;
    // drawcheck: live, displayed actors with a bound model that the renderer could not draw (one line per class /
    // reason) -- the list of invisible enemies / bosses / cutscene casts
    mutable std::set<std::string> drawMissLogged;
    void drawMiss(uint32_t a, od::port::ActorPool pool, const char* why, uint32_t model) const;
    void logEffects();
    bool enterPlay(const RoomMesh& mesh);
    void leavePlay(); // F2 out of play: sessionSave from the live state, then play off
    // FUN_00548910 / FUN_00548bc0 sub-state 0 on OpenDash's saves: 0xa5a248 = the slots present, 0xa7b040 their
    // summary (stage / area / location / play time) for the save menu's slot list
    void scanSaveSlots();
    int writeSaveSlot(uint8_t slot, std::span<const uint8_t> img);
    // The renderer's projection constants (FUN_0054a570 at boot: FUN_0040b990 defaults, FUN_0040ba00(0x180): focal
    // 0xa59d10 = 0x180, near 0xa59a20 = 0xc0; FUN_0040b9e0: screen centre 0xa59d20 / 0xa59d1c = 160 / 120 of the 320
    // x 240 mode). The lock-on selection (Maybe_SelectLockOnTarget 0x5e4b60), the lock's on-screen test and the shot
    // homing project with them.
    void setProjectionConstants();
    void beginPlay();

    // ---- new game / load / save (game/save_image) -------------------------------------------------------------------
    // Play started from "Novo jogo" / "Carregar save": the game state, flags and player come from the ported
    // Maybe_GameModeInit; the free-room play (F2 / browser) runs the same init (new game, or --story-save's save).
    bool gameStarted = false;
    // dev: the stage / sub a new game starts in (--newgame-stage; the game's new game is ST39 sub 0). ST3A sub 0 with
    // story 0 makes the room case 0x587c00 start script 6 (the crash landing) at once
    uint8_t newGameStage = 0x39, newGameSub = 0;
    bool starting = false;   // startGame is loading the room: the viewer must not pose / place the player
    std::string saveMessage; // last load / save result (F1 menu)
    // Maybe_GameModeInit 0x541150 (game mode 0) then the stage load (mode 1, as gameFrame runs it: OpenDash's loader
    // + game/transition stageLoaded). save = a DASH2-%02d.DAT image (load flag 1); nullptr = new game, set up like
    // the title menu (0x4292e5 / 0x4294f0: load flag 0, entry mode 0, ST39 sub 0, the chosen difficulty).
    bool startGame(const std::vector<uint8_t>* save, uint8_t difficulty);
    // Maybe_GameModeInit's state part (player struct cleared, FUN_0040e170 camera, the depth-range table, the ported
    // FUN_00540d10 / flags / difficulty tables / player init) for the load flag / save buffer / stage bytes set
    void gameModeInit();
    // the game loop's start (Maybe_GameModeLoop from mode 0) on the state the title flow (game/title) or startGame
    // left: load flag 0xa6d18c (1: the save image at 0x6c0000), entry mode 0xa6d18d, stage / sub 0xa6d190, story
    // 0xa6d194.., difficulty 0xa6d196
    // the game start (the title's Continue / new game, F1 loads, F2): stage / sub >= 0 = a browsed room instead of
    // the state's, Megaman placed by placeInBrowsedRoom
    bool startGameFromState(int stage = -1, int sub = -1);
    void placeInBrowsedRoom(const RoomMesh& mesh);
    // FUN_00549430's image build (title, header, FUN_00542ac0) -> file, in the game's format; verified by reading
    // it back (same bytes, checksums good). Never the game's own directory (the caller passes OpenDash's saves dir).
    bool saveGame(const std::filesystem::path& file, uint8_t slot);
    // the game task's frame (FUN_00541070) and the fade task. Mode 1 (stage load) is OpenDash's loader + the
    // ported state part; mode 5 the status sub-screen (game/status_screen); the modes 6 / 7 the field prompts of flags 0x246 /
    // 0x24d (game/transition).
    // the status sub-screen (game mode 5): its VRAM and the last frame's primitives (game/status_screen)
    MenuVram menuVram;
    bool inStatus = false; // mode 5 ran last frame (the sub-screen's VRAM was based on the field's on entry)
    std::vector<od::port::ScreenPrim> menuPrims;
    std::vector<od::port::ScreenPrim> shopPrims; // the junk shop's list (game/shop shopListDraw), over its windows
    // Roll's development room (content/yosyonke/dev_room): its list and stats panel
    std::vector<od::port::ScreenPrim> devPrims;
    std::vector<od::port::ScreenPrim> entryPrims; // script 0x74's entry list (content/yosyonke/st05_entry_list entryListDraw)
    std::vector<od::port::ScreenPrim> quizPrims;  // the ST32 quiz's answer mark / counter (OT +0x78, over the windows)
    std::vector<od::port::ScreenPrim> minimapPrims; // the draw pass's minimap (game/minimap, OT +0x78 under the HUD)
    void gameFrame();
    // Maybe_ProcessPendingDatLoads 0x40ef20 (WinMain): the queue 0xa79fe0 of {1, file id} through
    // Maybe_ParseDatContainer 0x40eb90 -- type 1 to the other type-1 buffer (0x6d000c ? 0x754800 : 0x76c800), type 6 to
    // the message bank 0x73c000, types 3 / 4 / 5 (VRAM) remembered for the room's textures (g_stageDats; rebuilt by
    // main through assetsDirty), type 9 to Maybe_LoadMp3MusicTrack 0x5d50c0(data, size, slot, loop = TOC arg) with
    // slot = the running count of type-9 segments in the file (0x40ed27: ST1701 / ST1705's Glyde theme -> slot 0,
    // started later by the script's music request; the stage's MP3 comes back only through another queued DAT or the
    // stage DAT load on a stage change, 0x5d5be0, as audioBindRoom does), type 8 to the SFX bank slots; then the queue and the busy word 0xa714c4 cleared
    bool assetsDirty = false;
    void processDatQueue();
    // one game tick: pad -> game task frame (game/transition: field mode, transitions, the player / actors /
    // camera frame body) -> message windows
    void playTick(uint16_t ps1Buttons, uint8_t stickX = 0x80, uint8_t stickY = 0x80, float lx = 0.0f, float ly = 0.0f);
    // ---- modern controls (default; "controles classicos" in F1 restores the original scheme) ----------------
    // The PC game steers like a tank: the stick X turns in place, d-pad up runs along the facing. Here the
    // left stick / arrows give a direction relative to OpenDash's free orbit camera; the body yaw (+0x2a)
    // is turned toward it (0x180 per tick) and the game gets "up", so run speed, animation, collision and
    // physics stay the game's. Outside plain locomotion the direction becomes body-relative d-pad.
    bool modern = true;
    uint8_t steerStickX = 0x80, steerStickY = 0x80; // steer's analog pad for the game (jet skates; else centre)
    float orbitYaw = 0.0f, orbitPitch = 0.30f; // radians, port camera convention (forward = sin, cos)
    bool usingGameCamera = false;              // the last frame showed the game camera (see orbitCamera)
    float posPrev[3]{}, posCur[3]{};          // player position of the last two ticks (game units, Y down)
    void orbitBehind();
    // the player is locked on a target (+0x174 = the target actor, set by FUN_005780e0, cleared by FUN_00578500)
    // scripted runs: the town walkers (Maybe_TownWalkerUpdate 0x441950) in pool 0x20, their positions / state
    void logTownNpcs() const;
    bool logLock = false; // scripted runs: log the lock-on (lockcheck lines)
    uint32_t lockTicks = 0;
    int camLogTicks = 0;
    bool lockedOn() const;
    // After a room change the game camera hands back to the orbit camera before the arrival is done (game mode 1 /
    // 2: the previous room's camera, e.g. ST09's pitch 80 in the ST0A house whose room record sets 480). Once the game
    // is back in field play (mode 3) with the follow camera (+4 = 0, no event): behind the player at the arrival
    // facing, at the game camera's pitch (+0x9a, from the room's camera record); distance / height are the game's.
    bool arrivalPending = false;
    void orbitArrival();
    void orbit(float dx, float dy);
    uint16_t steer(uint16_t ps1, float lx, float ly);
    bool fireFacing(uint16_t& ps1, float lx, float ly); // firing: face the camera, strafe (app/fire_facing)
    // the orbit camera around the interpolated player position (distance / height from the game camera)
    // modern mode: the orbit camera, except while the game camera is not following the player (talk camera mode 2,
    // scripted mode 1) or an event runs; when it is back in follow mode the orbit camera resumes from its pose
    bool gameCameraActive() const;
    bool lockCamShown = false; // the last orbitCamera frame was driven by the lock (scripted log: source "lock")
    bool orbitCamera(const RoomMesh& mesh, od::Camera& cam, float alpha, float dt);
    // first hit along from + ray * [0, 1] with the opaque room triangles (Moller-Trumbore, both sides); 1 = none
    static float rayHit(const RoomMesh& mesh, const float* o, const float* d);
    void trackPosition(bool snapNow);
    // ---- lock-on camera (modern mode; OpenDash's own, the game keeps its camera in classic mode) -------------
    // While the player is locked on (+0x174): a group-framing camera (app/cameras orbitCamera), eased in / out from
    // the orbit view by lockBlend; orbitYaw / orbitPitch follow it, so on release the orbit continues from there.
    uint32_t lockTarget = 0;
    float tgtPrev[3]{}, tgtCur[3]{};
    float lockBlend = 0.0f; // 0 = orbit view, 1 = lock view (the view pitch toward the aim point eases with it)
    float lockYawVel = 0.0f; // smoothDamp velocity (radians / s)
    // the frame's safe area (kFrameEdge of the edges), the game's FOV; lockDist eases the orbit's distance back to 1
    // after a lock; lockFovTan the FOV the lock view widened to (a wall pulled the eye in), easing back
    static constexpr float kFrameEdge = 0.85f, kTanHalfFov = 120.0f / 384.0f;
    float lockDist = 1.0f, lockDistVel = 0.0f;
    float lockFovTan = kTanHalfFov; // tan(half the vertical FOV) of the lock view (>= kTanHalfFov)
    // the lock view's state: its yaw, its aim point (render units), its eye distance (render units: grows at once,
    // comes back slowly)
    float lockYaw = 0.0f, lockBack = 0.0f;
    float lockCentre[3]{};
    bool lockCentreValid = false;
    // a drawn body's extent around its position (render units: the built model's vertices minus the position), per
    // tick from the geometry the frame draws; invalid = nothing drawn (the position alone is framed)
    struct BodyBox {
        bool valid = false;
        float lo[3]{}, hi[3]{};
    };
    BodyBox playerBody, targetBody;
    static constexpr float kBodyShrink = 0.05f; // a kept BodyBox moves this much a tick toward a smaller pose
    uint32_t targetBodyOf = 0; // the lock target targetBody belongs to
    // measures playerBody (object kKindPlayer 0) and, while locked on, targetBody from the tick's built objects
    void measureBodies(const RoomMesh& mesh, const std::vector<od::DynamicObject>& objects);
    // the drawn objects of the actor at `address` (animated pool: kKindActor slot, any blend; static / shot /
    // projectile pools: kKindPoolModel)
    static bool objectOfActor(uint32_t address, uint32_t id);
    // play in this room outside cutscenes (no script, event or talk holds the game's camera): the views OpenDash adds
    // (the see-through, the model clearance) apply
    bool freeView(const RoomMesh& mesh) const;
    // the player's position at the last tick, the room's units (render y up)
    void pivotAt(const RoomMesh& mesh, float out[3]) const;
    // the see-through's character in focus (renderer SeeThroughBody): the box of the player's model among objects, as
    // they place it (a tick's built objects: the room's units; a frame's: as drawn), whenever drawn (never an enemy:
    // no seeing a target through the walls); none in cutscenes
    void seeThroughBodies(const RoomMesh& mesh, const std::vector<od::DynamicObject>& objects,
                          std::vector<od::SeeThroughBody>& out) const;
    // critically damped spring toward `target` (Game Programming Gems 4, 1.10): framerate independent; returns the
    // step to add to `current`
    static float smoothDamp(float current, float target, float& vel, float smoothTime, float dt);
    // ---- jet skate camera (modern mode; OpenDash's own) --------------------------------------------------------
    // While skating (state 1, sub-state 3 skate / 9 skate fall) the orbit yaw chases behind the body (+0x2a) with a
    // critically damped lag, so a turn swings the camera out of the curve; mouse / right stick take over for
    // skateTune.manualHold seconds. The game's own lean and speed drive the feel, the physics stay the game's:
    // camera roll from the body lean +0x104 (skatecam log, turning: -0x40 accelerating / -0x60 cruising / -0x80
    // braking, so brake + turn, the game's "drift", leans most), FOV from the speed -(+0x3c) (0x280 brake .. 0x400 full).
    struct SkateTune {
        float chaseSmooth = 0.30f; // s, smoothDamp time of the chase yaw
        float manualHold = 1.0f;   // s without mouse / right stick before the chase resumes
        float rollDeg = 5.0f;      // camera roll at full lean (|+0x104| >= kSkateLeanFull)
        float fovDeg = 7.0f;       // extra vertical FOV at full speed (0x400)
    } skateTune;
    static constexpr float kSkateLeanFull = 0x80; // |+0x104| skating: 0x40 accelerate, 0x60 cruise, 0x80 brake
    float manualOrbitAge = 1e9f; // s since the last mouse / right stick orbit input
    float skateYawVel = 0.0f, skateRoll = 0.0f, skateFov = 0.0f; // roll / extra FOV in radians
    bool skating() const;
    void skateCamera(bool locked, float dt);
    bool logSkateCam = false; // --skatelog 1: skatecam lines (scripted runs)
    uint32_t skateLogFrames = 0;
    bool lockAimValid = false; // tgtPrev / tgtCur hold a target to aim at (also while the view eases out)
    bool lockCamActive() const;
    static float wrapPi(float a);

    // game camera of the last two ticks (eye 0xa6bccc unshifted, follow yaw / pitch), interpolated per frame
    struct CamPose {
        float eye[3]{};
        float yaw = 0.0f, pitch = 0.0f;
        int shift = 0;
    } camPrev, camCur;
    CamPose readCamera() const;
    // precise poses: the value the script camera's interpolation slot k (FUN_00512260) gave this tick, on its curve's
    // continuous form (16.16 units); false when the slot is idle
    bool smoothCameraSlot(uint32_t k, float out[3]) const;
    bool gameCamera(const RoomMesh& mesh, od::Camera& cam, float alpha) const;
    // Per-tick room parts (port axes): switchable segments by their current flags (the game's segment list in
    // play mode: corridors / open doorways switched by the door scripts), the door-leaf actors the door
    // transitions spawn, and the door actors at their live position.
    // segVerts (optional): how many of the leading vertices are the room's switchable segments (the rest: the
    // static-pool actors, shots and door actors drawn live)
    // perActor (optional): the static-pool / shot / pool-0x10 bank-model actors each as its own object (stable id per
    // pool slot) instead of in the returned list -- one object for all made every shot that appeared or died cost
    // every actor its blend and motion vectors that tick (the ST0E locomotive flickered)
    // segSemi (optional): the drawn semi-transparent segment quads by PS1 blend mode (q.semi 1 avg / 2 add / 3 sub),
    // left out of the returned list (the static room splits its own the same way, room_mesh semiTris)
    std::vector<od::RenderVertex> liveScene(const RoomMesh& mesh, size_t* segVerts = nullptr,
                                            std::vector<od::DynamicObject>* perActor = nullptr,
                                            std::array<std::vector<od::RenderVertex>, 3>* segSemi = nullptr) const;
    // Animated actors (pool 0x20) use only the live bone blocks produced by their
    // native class updates. One object per slot, in game units with no stage shift.
    // The bone points / bone blocks the game poses are in the coordinate shift of the moment (0x6d00b5, set by the
    // stage parameters or a camera script: the ST39 opening runs at shift 2 in a room loaded at shift 0); the room
    // mesh and the camera eye use the room's shift: posed geometry is scaled by 2^(room shift - live shift)
    float liveShiftScale(const RoomMesh& mesh) const;
    std::vector<od::DynamicObject> liveActors(const RoomMesh& mesh) const;
    // Combat effects (game/effects_draw: shots, sparks, bursts, the death explosion, hurt sparks, pickups) as the
    // game draws them, collected after each logic tick (fxPrims) and turned here into triangles facing `cam`: one
    // DynamicObject per actor and blend mode (blended objects use the renderer's PS1 semi pipelines).
    od::fx::State fx;
    std::vector<od::fx::Prim> fxPrims;
    std::vector<od::fx::Prim> dcPrims; // the decoded draw commands of the last tick (game/draw_commands)
    od::dc::Stats dcStats, dcLast;
    od::storm::Frame storm; // the storm's 2D sprites / flash triangles of the last tick (content/intro/crash_landing_draw)
    std::vector<od::weather::Quad> weather; // the blizzard layer / snow sprites of the last tick (game/weather_draw)
    size_t weatherLast = 0;
    uint32_t stormLogTick = 0;
    std::array<size_t, 5> stormLast{};
    std::vector<od::DynamicObject> liveEffects(const RoomMesh& mesh, const od::Camera& cam) const;
    // The lock-on reticle (app/reticle_overlay): built every rendered frame with that frame's camera
    std::vector<od::DynamicObject> reticleOverlay(const RoomMesh& mesh, const od::Camera& cam) const;
    // The screen-space overlays (the storm's glow, the weather layers): planes in front of the camera, so they are
    // built every rendered frame with the camera of that frame -- built once per logic tick (with the per-actor
    // effects) they stayed where the camera was on that tick and fell behind / out of view as it moved.
    // remasterWeather: the weather quads over the scene are left out (the renderer's precipitation draws that weather)
    std::vector<od::DynamicObject> screenOverlays(const RoomMesh& mesh, const od::Camera& cam, bool remasterWeather) const;
    float viewAspect = 4.0f / 3.0f; // the window's aspect (the storm's screen glow), set by the render loop
    // The field HUD (game/hud): ticked with the logic, drawn by HudUi over the scene
    od::hud::Hud hud;
    std::vector<od::hud::Prim> overlayPrims; // the draw pass's sprite banner (od::hud::overlayDrawPass), last tick
    // world-space triangles (port axes) of the posed model
    std::vector<od::RenderVertex> build(const RoomMesh& mesh);
};

} // namespace od::app
