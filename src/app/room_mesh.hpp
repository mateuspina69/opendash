// A room as the host draws it (RoomMesh: the scenery, the bank models, the live actor geometry, the sky, Megaman's
// parts and the texture slices), loadRoom, and the files / VRAM state of the last room loads.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "game/vram_moves.hpp"

#include "app/character_mesh.hpp"
#include "app/room_uv_anim.hpp"
#include "engine/bank.hpp"
#include "engine/config.hpp"
#include "engine/room.hpp"
#include "engine/vram.hpp"
#include "game/camera.hpp"
#include "game/effects_draw.hpp"
#include "game/loaded_data_fingerprint.hpp"
#include "game/player/player_snapshot.hpp"
#include "game/room_exits.hpp"
#include "render/renderer.hpp"

namespace od::app {

// One PS1 sky layer (Maybe_InitSkyLayers 0x5efb90 descriptor + tilemap of 32x32 tiles).
struct SkyLayer {
    size_t slot = 0; // native descriptor slot, retained after sorting by draw order
    uint8_t flags = 0, flags2 = 0;          // +0 (2 = render), +1 (1 manual scroll, 2 curved, 4 no parallax)
    int16_t base = 0, hofs = 0, speedX = 0, speedY = 0, w = 0, h = 0, scrollX0 = 0, scrollY0 = 0;
    uint16_t clutWord = 0, tpage = 0;
    float shade = 1.0f;
    uint8_t ot = 0;
    std::vector<uint32_t> tiles;            // low16 = u | v << 8 (0 = empty), high16 = CLUT offset
    std::map<uint16_t, int> sliceOfClut;    // effective CLUT -> texture slice
};

// A quad of a switchable room segment (corridor / doorway variants, door strip lists), drawn per tick when
// od::segmentQuadDrawn accepts the segment's current flags.
struct SegQuad {
    od::RoomQuad q;
    std::array<od::RenderVertex, 6> v;
};

// an actor part's emitted vertices -> the part's source vertex index (FUN_005f4980 shares vertices between parts by
// index), and the part's vertex count (the stride of its vertex colour words)
struct PartSource {
    std::vector<uint16_t> index;
    std::vector<uint8_t> flatColour; // per emitted vertex: its face's ModelFace::flatColour
    uint32_t verts = 0;
};

struct RoomMesh {
    std::vector<od::RenderVertex> tris;
    std::vector<SegQuad> segQuads;                 // switchable segments
    std::vector<SegQuad> lodFloorQuads;            // floor tiles drawn both sides by their far variant only
    od::RoomUvAnim uvAnim;                         // room faces' UV pair -> their vertices (room UV animation)
    std::map<int, uint8_t> segFileFlags;           // segment -> flags byte in the file (viewer)
    std::map<int, std::vector<od::RenderVertex>> entryGeom; // bank entry -> model space (game axes), all parts
    std::vector<uint8_t> bank;                     // the type-1 bank proper (mapped for Maybe_BindActorModel)
    std::vector<uint8_t> bankSegment;              // the whole type-1 segment (leading records + bank, FUN_005f3200)
    uint32_t bankOffset = 0;                       // the bank inside bankSegment
    long playerAnims = -1, bankExtra = -1;         // the bit-31 / bit-30 records (player +0x298 / 0xa71fbc)
    std::vector<od::BankEntry> bankIndex;
    // the alternate banks (g_altBankDat): the script DAT in each of the two type-1 buffers (kAltBankBuffers). The
    // scripts alternate them (0x6d000c): the next scene's DAT is loaded into one buffer while the casts of the
    // current scene stay bound to the other (the attract: ST0302 loaded while ST0301's casts still play).
    struct AltBank {
        // the class-0x20 entries' part geometry, keyed like partGeom, and where the game has the bank (0x6d0074
        // after FUN_005f3200 walked this type-1 buffer)
        std::vector<od::BankEntry> index;
        std::map<std::pair<int, int>, std::vector<od::RenderVertex>> partGeom;
        std::map<std::pair<int, int>, PartSource> partSrc;
        std::map<std::pair<int, int>, std::vector<uint32_t>> partTex;
        // the static / shot entries (class 0x60 / 0x40 / 0x10) in model space, like entryGeom: statics bound into
        // the swapped buffer (ST18 script 0x37's gunship 0x59f880) are drawn from here
        std::map<int, std::vector<od::RenderVertex>> entryGeom;
        uint32_t at = 0, size = 0;
        bool holds(uint32_t a) const { return at && a >= at && a < at + size; }
    };
    std::array<AltBank, 2> altBanks;
    const AltBank* altBankOf(uint32_t a) const {
        for (const AltBank& b : altBanks)
            if (b.holds(a)) return &b;
        return nullptr;
    }
    std::vector<std::array<float, 3>> camTris; // opaque triangle positions kept after upload (camera collision)
    float clear[3]{0.0f, 0.0f, 0.0f};                             // background = D3D fog colour
    // Maybe_SetSkyGradient 0x5f0370(0) (the room function's table entry is 0): DRAWENV isbg = 1 (0xa72248 -> drawenv
    // +0x18 via FUN_00549a40), the frame is cleared to the drawenv colour (black) instead of showing the fog colour
    bool bgCleared = false;
    float fogStart = 0.0f, fogEnd = 0.0f;                         // camera depth (world units)
    std::array<uint32_t, 4> semiStart{};                          // semi-transparent vertex ranges
    std::vector<SkyLayer> sky;
    // ST01 (the travel map, script 7): the 2D world map of FUN_00518c00 (8-bit pages 0x15 / 0x17 / 0x19 / 0x1b,
    // CLUT rows 0x1f0..0x1f3, uploaded by ST01T.DAT) and the destination markers of FUN_00518a30 (page 0x1e, CLUTs
    // 0x7fc0 / 0x7fc1): texture slices, -1 = not this stage
    std::array<int, 4> travelMapSlice{-1, -1, -1, -1};
    std::array<int, 2> travelMarkerSlice{-1, -1};
    // Maybe_DrawSkyGradient 0x5f03a0: the sub's {top, bottom} colours (0xBBGGRR) per 32-line band behind the sky
    // layers (Maybe_SetSkyGradient 0x5f0370 from the room function); the band at the top of a level view is
    // skyGradHofs (sky descriptor 0 +0x16 when it renders, else 0x240)
    std::vector<std::array<uint32_t, 2>> skyGrad;
    int skyGradHofs = 0x240, skyGradW = 0x50;
    std::map<std::pair<int, int>, std::vector<od::RenderVertex>> partGeom; // model space, game axes
    std::map<std::pair<int, int>, PartSource> partSrc;                     // partGeom's source vertices
    std::vector<uint8_t> slices; // sliceCount * 256*256 RGBA
    // the (tpage << 16 | clut) each slice was decoded from, bit 32 set (0 = not a plain VRAM slice): the animated
    // CLUTs re-decode the slices whose CLUT they change
    std::vector<uint64_t> sliceTex;
    int sliceCount = 0;
    float lo[3]{1e9f, 1e9f, 1e9f}, hi[3]{-1e9f, -1e9f, -1e9f};
    CharacterMesh player;                 // Megaman of the local player
    // texture-cell faces of the animated actors (part flags (+0xef + part * 4) & 3 = the word +0xd4 + k * 4,
    // FUN_005f4160): the (tpage << 16) | clut of every vertex, and the room's texture slices by key (page offsets
    // 1..3 of those textures included)
    std::map<std::pair<int, int>, std::vector<uint32_t>> partTex;
    std::map<uint32_t, int> sliceOfTex;
    int32_t playerSpawn[3]{};  // actor position, 16.16, unshifted collision space
    std::shared_ptr<const od::GameFile> roomFile; // ST##.DAT, for the collision of play mode
    std::string roomFileName; // its name (ST##.DAT or the story-state file): the language pack's bank of the same stem
    std::string textureFile; // ST##T.DAT (or the story-state file): the stage's MP3 music segments
    int grid = 0;                                         // active sub-grid
    int stage = 0, sub = 0;
    std::optional<od::port::RoomCamera> camera; // the room's camera record (FUN_0040e290 in the per-stage room setup)
    std::vector<od::port::RoomExit> exits;       // the room's exits (game/room_exits: the list *0xa7180c points at)
    int shift = 0;             // DAT_006d00b5 (bone translations are << shift)
    // endless track (buildTrackStrip): the axis (0 x / 2 z, -1 none) and the period of the looping room
    static constexpr int kTrackTilesAhead = 128; // the strip's length ahead (periods): ~9 minutes of the train fight
    int trackAxis = -1;
    float trackPeriod = 0.0f;
    uint32_t roomParams = 0;   // the exe's param struct of this sub-room (Maybe_SetupRoomParams 0x5f59d0)
    // combat effects (game/effects_draw): one slice per g_fxTextures entry (decoded from this stage's VRAM + PS1
    // colour modulation), and the gradient page of the gouraud primitives (rows of od::fx::gradients())
    std::vector<int> fxSlices;
    int fxGradSlice = 0;
};

// The player files of the running game (Maybe_RequestPlayerFiles 0x5d5c40 = od::playerDatRequest at stage load, see
// PlayerView::playerFiles): the body PL00P0xx and the special weapon's PL00R##; loadRoom builds Megaman's mesh from
// them (free-roam viewing without a started game keeps PL00P000)
extern std::string g_playerBodyDat, g_playerWeaponDat;

// The effect textures (tpage, clut, tint) the effect emitters met (game/effects_draw TextureCache): loadRoom decodes
// one slice per entry from the stage's VRAM; a new entry met during play rebuilds the room's slices (assetsDirty).
extern od::fx::TextureCache g_fxTextures;

// DAT files a script loaded during the stage (Maybe_RequestDatLoad 0x40efe0 -> the queue 0xa79fe0, drained by
// Maybe_ProcessPendingDatLoads 0x40ef20 in WinMain): their VRAM images / CLUTs are applied over the stage's when the
// room's textures are rebuilt; the last one with a type-1 segment in each type-1 buffer is an alternate model bank
// (mapped where the loader put it: 0x754800 / 0x76c800 by 0x6d000c), e.g. ST3A01 / ST3A02 of script 6. Cleared at
// a stage change.
extern std::vector<std::string> g_stageDats;

// the two type-1 buffers of the script DAT loads and the DAT each holds ("" = none)
constexpr std::array<uint32_t, 2> kAltBankBuffers{0x754800u, 0x76c800u};
extern std::array<std::string, 2> g_altBankDat;

// the geometry / texture files of the last stage load (FUN_005d5cd0's selector, then Maybe_LoadStageDat 0x5d5be0):
// an in-stage room switch (the transition's sub-room change, a script's request 0xff) loads no stage file, so it keeps
// them even when the selector's flags changed since (ST53: script 0x60 sets flag 0x5a0, which picks ST5301 +
// the empty ST5301T only for the next stage load; re-selecting at its sub 1 / 2 swaps drew the rooms black)
extern int g_stageFilesStage;

extern std::string g_stageFilesGeo, g_stageFilesTex;

extern bool g_inStageSwitch;

 // set by PlayerView::enterRoomOfState around an in-stage room load
// the live VRAM of play: never cleared, every play load writes its files over it (loadRoom), like the exe's. The
// HUD's sprites read the live VRAM in the exe (the ST18 repair gauge's plates and digits are in ST18T.DAT's pages)
extern od::Vram g_roomVram;
// the image moves (game/vram_moves) applied to it in this stage, each after the script DATs loaded before it: a rebuild
// of the room writes the stage's files over the VRAM again, so it makes the moves again between them, in the order the
// exe made them (ST02's shutters slid after ST0203.DAT)
struct KeptVramMove {
    size_t afterDats = 0; // the script DATs (g_stageDats) applied before it
    od::port::VramMove move;
};
extern std::vector<KeptVramMove> g_keptVramMoves;

extern uint32_t g_roomVramGen;

// the loaded files a ghost's pose depends on (the stage files of the last stage load, the script banks), for the
// ghosts' loaded-data fingerprint (game/loaded_data_fingerprint)
od::port::LoadedDataIdentity loadedDataIdentity();

extern int g_stageDatsStage;


// Megaman posed by the game's own pose code (od::port::playerPose) on an image of its player struct.
// Until the M2 player logic lands, the animation (index k of the table at PS1 0x113a00 = player +0xa2)
// and its frame (*(player +0x98)) are chosen directly -- a viewer for the 42 player animations.
// The fog the renderer gets for a room. Classic = the original's (fully fogged at fogEnd: right for a PS1 draw distance,
// it hides most of a room here). Remaster: exponential height fog from the original's colour and distances; the dense
// fog of the snowing town (ST08 / 09 / 0B: end 0.3 of the depth range, the others 0.5..0.8) stays heavy but softer,
// elsewhere a light haze -- about a third at the original's end, at most maxOpacity, thinner up high (towers and
// cliffs stay visible), thicker in valleys. distance scales the remaster distances (F1 Visual, fog_distance=).
od::Fog fogFor(const RoomMesh& mesh, od::FogMode mode, float distance);

// Loads ST{stage}.DAT geometry + floor, textures from INIT/GAME/ST{stage}T, and flattens quads into
// one triangle list (game Y-down -> Y-up).
RoomMesh loadRoom(int stage, int sub);

} // namespace od::app
