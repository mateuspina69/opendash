// The minimap callbacks of the Manda stages (see minimaps.hpp).
#include "content/manda/minimaps.hpp"

#include "game/game_state.hpp"
#include "game/loaded_data.hpp"
#include "game/minimap.hpp"
#include "game/story_flags.hpp"

namespace od::content {

using namespace od::port;

namespace {

// ---- the ruin (ST12..ST14): ST12's sub-rooms are areas 0.. (layer 7), ST13's 6.. (layer 0xb, its art 0x60 rows
// down), ST14's 0x11.. (layer 5, 0xc0 rows down); ST12's sub 6 has no map; the explored cells from byte 0 on ------
constexpr uint32_t kSt12Areas = 22;
const DataTable<ScalarTable<uint16_t, kSt12Areas>> st12VisitedBases{TableId::St12MinimapVisitedBases};
const DataTable<RecordTable<MinimapArea, kSt12Areas, 8>> st12Areas{TableId::St12MinimapAreas};
const DataTable<ScalarTable<uint32_t, kSt12Areas>> st12Dots{TableId::St12MinimapDots};
constexpr uint8_t kSt12Stage = 0x12, kSt13Stage = 0x13;
constexpr uint8_t kSt12NoMapRoom = 6;
constexpr uint32_t kUnexploredDim = 0x303030;
constexpr uint32_t kFlagUnexploredShown = 3, kFlagDotsShown = 4;
// the tiles' cluts by stage
constexpr uint16_t kSt12Clut = 0x7dc0, kSt13Clut = 0x7dc1, kSt14Clut = 0x7dcd;

// exe 0x54b970: the tiles that draw nothing, by stage
bool st12NoTile(uint8_t t) {
    switch (gameState().stage) {
    case kSt12Stage:
        switch (t) {
        case 0x21: case 0x2c: case 0x34: case 0x3c: case 0x3e: case 0x3f: case 0x43: case 0x44: case 0x45: case 0x47:
            return true;
        default: return false;
        }
    case kSt13Stage:
        switch (t) {
        case 0x16: case 0x2e: case 0x43: case 0x45: case 0x49: case 0x4a: return true;
        default: return false;
        }
    default:
        switch (t) {
        case 0x03: case 0x0e: case 0x2e: case 0x2f: case 0x30: case 0x31: case 0x3e: case 0x3f: return true;
        default: return false;
        }
    }
}

// exe 0x54baa0(t, area): the tiles the story changes: area 0's 0x11..0x15 from flag 0x5b6 or story 5 on (0x3f
// further), area 8's 0x3d with flag 1 (0x5e), area 0xc's 0x41 with flag 2 (0x5f)
uint8_t st12Texel(uint8_t t) {
    switch (minimap().area) {
    case 0:
        if ((storyFlag(0x5b6) || gameState().story >= 5) && t >= 0x11 && t <= 0x15) return uint8_t(t + 0x3f);
        return t;
    case 8: return storyFlag(1) && t == 0x3d ? 0x5e : t;
    case 0xc: return storyFlag(2) && t == 0x41 ? 0x5f : t;
    default: return t;
    }
}

// ST13: standing on some cells marks the cells next to them (the bits of the area's bytes)
void st13ExtraMarks(uint8_t room, int32_t cell) {
    uint8_t* b = &extraFlags().bytes[minimap().visitedBase];
    if (room == 2 && cell == 0x1f) b[3] |= 0x40, b[4] |= 0x01;
    else if (room == 3 && cell == 4) b[0] |= 0x80;
    else if (room == 6 && cell == 0x12) b[1] |= 0x20, b[2] |= 0x80;
    else if (room == 7 && cell == 4) b[0] |= 0x20;
    else if (room == 8 && cell == 7) b[0] |= 0x40;
    else if (room == 1 && cell == 9) b[1] |= 0x20;
}

// exe 0x54b230
void st12Minimap() {
    MinimapState& m = minimap();
    const GameState& g = gameState();
    if (g.stage == kSt12Stage && g.room == kSt12NoMapRoom) return;
    if (!m.ready) {
        if (g.stage == kSt12Stage) m.layer = 7, m.area = g.room, m.areaExtra = 0;
        else if (g.stage == kSt13Stage) m.layer = 0xb, m.area = uint8_t(g.room + 6), m.areaExtra = 0x60;
        else m.layer = 5, m.area = uint8_t(g.room + 0x11), m.areaExtra = 0xc0;
        m.ready = 1;
        m.visitedBase = st12VisitedBases[m.area];
    }
    const MinimapArea& a = st12Areas[m.area];
    const int32_t cell = minimapPlayerCell(a);
    minimapMarkCell(m.visitedBase, cell);
    if (g.stage == kSt13Stage) st13ExtraMarks(g.room, cell);
    if (!minimapOnScreen()) return;
    MinimapTileStyle s;
    s.layer = m.layer;
    s.firstBase = 0;
    s.unexplored = storyFlag(kFlagUnexploredShown) ? kUnexploredDim : 0;
    s.clut = g.stage == kSt12Stage ? kSt12Clut : g.stage == kSt13Stage ? kSt13Clut : kSt14Clut;
    s.noTile = st12NoTile;
    s.texel = st12Texel;
    s.texelRowOffset = m.areaExtra;
    if (storyFlag(kFlagDotsShown)) s.dots = st12Dots[m.area];
    minimapAreaPanel(a, s);
}

} // namespace

void registerMandaMinimaps(ContentRegistry& r) { r.roomCallbacks[RoutineId::St12RoomCallback] = &st12Minimap; }

} // namespace od::content
