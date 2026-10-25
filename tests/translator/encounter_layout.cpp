// The encounters' state in the exe's layout (game/encounter).
#include "game/encounter.hpp"
#include "translator/layout.hpp"

namespace od::test {

using namespace od::port;

void encounterLayout(LayoutVisitor& v) {
    EncounterState& e = encounterState();
    for (size_t i = 0; i < kEncounterSlots; ++i) {
        EncounterSlot& s = e.slots[i];
        const uint32_t a = 0xa6d100 + uint32_t(i) * 0x20;
        v.field(a + 0x00, s.active);
        v.field(a + 0x01, s.kind);
        v.field(a + 0x02, s.state);
        v.field(a + 0x03, s.waveStep);
        v.field(a + 0x04, s.anchor);
        v.field(a + 0x0a, s.unread0a);
        v.field(a + 0x0c, s.rollPitch);
        v.field(a + 0x0e, s.rollYaw);
        v.field(a + 0x10, s.unread10);
        v.field(a + 0x12, s.rollMoveYaw);
        v.field(a + 0x14, s.room);
        v.field(a + 0x15, s.group);
        v.field(a + 0x16, s.alive);
        v.field(a + 0x17, s.lost);
        v.field(a + 0x18, s.cellX);
        v.field(a + 0x19, s.cellZ);
        v.field(a + 0x1a, s.pause);
        v.field(a + 0x1c, s.steps);
        v.field(a + 0x1d, s.level);
        v.field(a + 0x1e, s.stepIndex);
        v.field(a + 0x1f, s.unread1f);
    }
    v.field(0xa67844, e.slotOfKind);
    v.field(0xa7b088, e.trackedMasks);
    for (size_t i = 0; i < kTrackedActors; ++i) {
        const uint32_t a = 0xa7b0a0 + uint32_t(i) * 4;
        v.field(a, e.tracked[i].kind);
        v.field(a + 1, e.tracked[i].index);
        v.field(a + 2, e.tracked[i].id);
    }
    v.field(0xa6d1dc, e.defeated);
    v.field(0xa6d1ec, e.attackLocks);
    v.field(0xa7ad90, e.waveEnemies);
    v.field(0xa6c424, e.control);
    v.field(0xa6bf78, e.zoneTables);
    v.field(0xa714c8, e.groupTables);
    v.field(0xa6bb94, e.fieldTable);
    v.field(0xa7258c, e.enemyGroups);
    v.field(0xa6d160, e.formation);
    v.field(0xa6d164, e.positionCursor);
    v.field(0xa6d168, e.classCursor);
    v.field(0xa6d16c, e.waveOddsRow);
    v.field(0xa6d16d, e.positionSetByte);
    v.field(0xa6d16e, e.battleMusicLast);
    v.field(0xa6d16f, e.battleMusicRequest);
    v.field(0xa6d170, e.battleMusicOn);
}

} // namespace od::test
