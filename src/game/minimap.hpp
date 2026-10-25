// The minimap: the 72 x 72 panel the field draws in its corner while a stage callback is set. Its block 0xa6d320 is
// cleared by the stage setup's template (exe 0x415410), reset by a room's setup (exe 0x415480: hidden off screen, the
// entrance at the player) and switched on when the room has a callback (exe 0x5d5b30); the frame body slides it in
// and out (exe 0x415560 -> 0x4155d0) and the draw pass runs the stage's callback (exe 0x4155b0 -> [0xa72584], content
// roomCallbacks), which marks the cell the player stands in among the explored cells (the save's block 0xa73de0, the
// map page reads them) and draws the panel into the ordering table's overlay entry. The callbacks share the pieces
// below: the entrance marker (exe 0x415880), the player's heading (exe 0x415710) and the icons of the towns' maps
// (exe 0x415a30).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/data_source.hpp"
#include "engine/screen_prim.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port {

struct Player;

// +7: where the panel is
enum class MinimapPhase : uint8_t {
    Hidden = 0,   // off screen (slideX kMinimapHiddenX)
    SlideIn = 1,  // 16 px a frame up to kMinimapShownX
    Shown = 2,    // at kMinimapShownX
    SlideOut = 3, // 16 px a frame back to kMinimapHiddenX
};
// +0
constexpr uint8_t kMinimapOn = 0x01;      // the room has a callback (exe 0x5d5b30)
constexpr uint8_t kMinimapAllowed = 0x02; // the scripts clear it to put the panel away (the cutscenes)
constexpr int16_t kMinimapHiddenX = -0x88, kMinimapShownX = 0xc;
constexpr int16_t kMinimapSize = 0x48; // the panel's width and height
constexpr int16_t kMinimapY = 0x18;    // its top

// The block 0xa6d320 (7 dwords)
struct MinimapState {
    uint8_t flags = 0;  // +0x0: kMinimapOn | kMinimapAllowed
    uint8_t layer = 0;  // +0x1: the room block's map layer the callback draws (its entry 2 + layer)
    uint8_t area = 0;   // +0x2: the callback's area (the index of its tables)
    uint8_t spare03 = 0;
    uint8_t ready = 0;  // +0x4: the callback set its area up (cleared by each room's setup)
    StateArray<uint8_t, 2> spare05{};
    uint8_t phase = 0;           // +0x7: MinimapPhase
    int16_t slideX = 0;          // +0x8: the panel's left edge
    uint16_t visitedBase = 0;    // +0xa: the area's first byte in the explored cells
    int16_t entryX = 0;          // +0xc: where the player entered the room (integer x / z)
    int16_t entryZ = 0;          // +0xe
    uint16_t entryYaw = 0;       // +0x10: the player's yaw then, its quadrant (0 / 0x400 / 0x800 / 0xc00)
    uint16_t entryYawSpare = 0;  // +0x12 (no reader traced)
    uint32_t spare14 = 0;        // +0x14 (only cleared)
    uint8_t areaExtra = 0;       // +0x18: a second byte of the area some callbacks keep (ST12 / the towns)
    StateArray<uint8_t, 3> spare19{};

    // the dwords +8, +0, +4, +0x14, +0x18 cleared (exe 0x415480)
    void clearRoomWords() {
        slideX = 0;
        visitedBase = 0;
        flags = 0;
        layer = 0;
        area = 0;
        spare03 = 0;
        clearSecondWord();
        spare14 = 0;
        areaExtra = 0;
        spare19.fill(0);
    }
    // the dword +4 cleared: not ready, hidden
    void clearSecondWord() {
        ready = 0;
        spare05.fill(0);
        phase = 0;
    }
};
inline MinimapState& minimap() {
    static MinimapState s;
    return s;
}

// exe 0x415410's minimap part (the stage setup's template): the block cleared
void minimapStageReset();
// exe 0x415480's minimap part (a room set up with param >= 0): cleared, hidden off screen, the entrance at the
// player's integer x / z and its yaw's quadrant
void minimapRoomReset(const Player& player);
// exe 0x5d5b30: a room set up while the stage has a callback: on (kMinimapOn | kMinimapAllowed), not ready, hidden
void minimapSwitchOn();
// exe 0x415560 (the frame body, after the late pools): the enemy radar's beep, the panel's slide (exe 0x4155d0). The
// HUD elements' logic it also runs (exe 0x415fe0) is game/hud's, ticked by the host.
void fieldHudFrame();

// The primitives of the draw pass's minimap go into `draw` (nullptr: none; the callback's writes happen either way)
void setMinimapDraw(std::vector<ScreenPrim>* draw);
// exe 0x4155b0's call of the stage callback (the draw pass, after the sprite banner): the content's routine of
// StageState::roomCallback (a callback with none is reported once and skipped)
void minimapDrawPass();

// ---- the callbacks' shared pieces ----
// An area of a callback's map (its table's 8-byte record, by area): the cells it spans, in cells of 0x400 units (two
// collision cells) from the collision cell (cellX, cellZ) on
struct MinimapArea : DataRecord {
    uint8_t cellX = 0, cellZ = 0;
    uint8_t columns = 0, rows = 0;
    int16_t pageX = 0, pageZ = 0; // read by the map page's callback
    static MinimapArea load(const TableReader& r) {
        return {{}, r.u8(0), r.u8(1), r.u8(2), r.u8(3), r.s16(4), r.s16(6)};
    }
};
// the area's cell the player stands in: row (from z) * columns + column (from x); outside the area it runs off the
// grid as the exe's arithmetic does
int32_t minimapPlayerCell(const MinimapArea& a);
// the tiles' default "no tile" bytes: 0x5c..0x5f and 0xff
bool minimapNoTile(uint8_t t);
// How a ruin callback draws its area's tiles (minimapAreaPanel)
struct MinimapTileStyle {
    uint8_t layer = 0;                        // the room block's map layer
    uint32_t firstBase = 0;                   // the stage's first area byte (the tiles from (visitedBase - it) * 8 on)
    uint32_t unexplored = 0;                  // the unexplored cells' colour (0: not drawn)
    uint16_t mode = 0x1d;                     // the draw mode (the tiles' tpage)
    uint16_t clut = 0x7fc0;                   // the tiles' clut
    bool (*noTile)(uint8_t t) = minimapNoTile; // the bytes that draw nothing
    uint8_t (*texel)(uint8_t t) = nullptr;    // a byte's tile in the art (nullptr: itself)
    uint8_t texelRowOffset = 0;               // added to the texel row (v)
    int16_t originShiftX = 0;                 // added to the tiles' left edge
    uint32_t dots = 0;           // the area's dot list (exe data: {u16 flag, u16, s16 x, s16 z} .., flag 0xffff ends; 0: none)
    bool dotsPaceHalved = false; // at pace 3 the dots pulse at (frame & 0xf) * 2
};
// the panel of an area drawn into the overlay entry, as the ruins' callbacks draw it (exe 0x54fd10 ...): the drawing
// area of the panel, the draw mode, the panel's semi-transparent grey, the area's tiles (16 x 16 semi-transparent
// sprites, explored ones neutral), scrolled to keep the player in the middle within the area's edges; the dots (4 x 4
// semi-transparent tiles: their flag set dark yellow, else pulsing yellow), the entrance marker, the player's heading,
// the display's drawing area back. The scroll's points pass through the geometry scratch's lanes as the exe's.
void minimapAreaPanel(const MinimapArea& a, const MinimapTileStyle& s);

// A town's map picture (its table's 8-byte record, by area): where the picture's corner sits from the grid's middle
struct MinimapPicture : DataRecord {
    int16_t pageX = 0, pageZ = 0; // read by the map page's callback
    int16_t originX = 0, originZ = 0;
    static MinimapPicture load(const TableReader& r) { return {{}, r.s16(0), r.s16(2), r.s16(4), r.s16(6)}; }
};
// the geometry scratch's lanes the callbacks pass their points through (0x50 / 0x52, 0x54 / 0x56; the tiles' texture
// word 0xa0 / 0xa2)
struct MinimapLanes {
    Lane16 x, y, cornerX, cornerY, texel, clut;
};
MinimapLanes minimapLanes();
// the panel's chain opened in the overlay entry: the panel's drawing area, the draw mode `mode`
size_t minimapPanelOpen(uint16_t mode);
// the panel's semi-transparent grey backdrop
void minimapPanelBackdrop(size_t c);
// the display's whole drawing area back (the chain's last packet)
void minimapPanelClose(size_t c);
// a town's scroll kept within +-0x5c
int16_t minimapScrollClamp(int16_t v);
// a town's picture: a 256 x 256 semi-transparent sprite at 0xa0a0a0 of `clut` (texels from (0, 0))
void minimapPictureSprite(size_t c, int16_t x, int16_t y, uint16_t clut);
// the pulse of the markers (t 0..0x1f): 0x7f up to 0xff and back
uint32_t minimapPulse(uint32_t t);

// the panel is on screen (the callbacks draw nothing while it is Hidden)
inline bool minimapOnScreen() { return minimap().phase != uint8_t(MinimapPhase::Hidden); }
// the explored cells (the save's block 0xa73de0): cell `cell` of the area at byte `base` marked / tested (the cell's
// byte cell >> 3 from base, bit cell & 7)
void minimapMarkCell(uint32_t base, int32_t cell);
bool minimapCellExplored(uint32_t base, int32_t cell);
// the address of the room block's map layer (the callback's tiles: a byte per cell, its texel row / column in its
// nibbles)
uint32_t minimapLayer(uint8_t layer);
// exe 0x415880(packet, x, y): the entrance marker into chain c: a G3 pointing along the entrance's yaw quadrant at
// (x, y), its red pulsing with the frame counter
void minimapEntranceMarker(size_t c, int16_t x, int16_t y);
// exe 0x415710(packet, x, y): the player's heading into chain c: the triangle (0, 0xc0), (-0x60, -0xa0), (0x60, -0xa0)
// turned by the stick's yaw offset minus the player's yaw (exe 0x40bea0 through the projection registers) and scaled
// by 1 / 32 at (x, y), a G3 (its tip cyan) and its outline
void minimapPlayerHeading(size_t c, int16_t x, int16_t y);
// exe 0x415a30(packet, x, y, flags): an icon of a town's map into chain c: flags & 4 an 8 x 8 sprite (column flags & 3
// of the row at (0xb0, 0xa0)), else a 24 x 24 quad turned by flags & 3; flags & 8 semi-transparent
void minimapIcon(size_t c, int16_t x, int16_t y, uint32_t flags);

} // namespace od::port
