// The world's small native states in the exe's layout: the random generators, the room grid's globals, the
// territories, the shadow list's cursor, the map marker, the save slots' summaries and bits, the demo's replay.
#include "game/demo_playback.hpp"
#include "game/random.hpp"
#include "game/room_grid.hpp"
#include "game/save_slots.hpp"
#include "game/shadows.hpp"
#include "game/territory.hpp"
#include "game/visited_rooms.hpp"
#include "translator/layout.hpp"

namespace od::test {

using namespace od::port;

void worldLayout(LayoutVisitor& v) {
    RandomState& r = randomState();
    v.field(0x6d0000, r.rng);
    v.field(0x7f100080, r.crtSeed);

    RoomGridState& g = roomGrid();
    v.field(0x6d0080, g.block);
    v.field(0x6d0084, g.pool);
    v.field(0x6d0088, g.cells);
    v.field(0x6d008c, g.segments);
    v.field(0x6d0094, g.originX);
    v.field(0x6d0095, g.originZ);
    v.field(0x6d0096, g.lastX);
    v.field(0x6d0097, g.lastZ);
    v.field(0x6d0098, g.width);
    v.field(0x6d0099, g.depth);
    v.field(0x6d009b, g.roomFlags);

    v.field(0xa71808, territory().table);
    v.field(0x6d0070, shadowList().cursor);

    MapMarker& m = mapMarker();
    v.field(0xa71724, m.records);
    v.field(0xa71720, m.foundWord);
    v.field(0xa71703, m.shown);

    SaveSlotSummaries& s = saveSlotSummaries();
    for (size_t i = 0; i < kSaveSlots; ++i) {
        const uint32_t a = 0xa7b040 + uint32_t(i) * 8;
        v.field(a, s.slots[i].stage);
        v.field(a + 1, s.slots[i].room);
        v.field(a + 2, s.slots[i].difficulty);
        v.field(a + 3, s.slots[i].place);
        v.field(a + 4, s.slots[i].time);
    }
    v.field(0xa5a248, saveSlotBits().present);

    DemoState& d = demoState();
    v.field(0xa676a4, d.next);
    v.field(0xa676a8, d.recording);
}

} // namespace od::test
