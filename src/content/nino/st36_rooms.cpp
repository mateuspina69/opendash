// The Nino ruin's ST36 (its second floor; story 10+): the room-script side of the stage (wired from game/spawn: the
// script cases {0x36, by story, 0x8cb250}, the handler chain, the room helper). One routine per exe routine; the names
// are hypotheses from the observed behaviour.
//   0x586c90  ninoSt36RoomHelperTail  the room helper (after its handler step cleared / exe 0x55c000(-1, 0, 0))
//   0x586d80  ninoSt36RoomCaseTail    the stage case of the room scripts' tick after the handler call
//   0x586e40 / 0x586e60 / 0x586f00 / 0x586f40 / 0x5878b0  ninoSt36RoomHandler (the zone-slot encounter groups)
#include "content/nino/st36_rooms.hpp"

#include "content/nino/ch2_rooms.hpp"

#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/display.hpp"
#include "game/encounter.hpp"
#include "game/game_state.hpp"
#include "game/player/player_state.hpp"
#include "game/room_scripts.hpp"
#include "game/spawn.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

constexpr uint32_t kFlagFlooded = 0x90;                             // the ruin's floors flooded
constexpr uint32_t kFlagSub4Opened = 0x9a, kFlagSub10Opened = 0x9b; // the floor cells broken open
constexpr uint8_t kSub4 = 4, kSub10 = 0xa, kDrySub = 0xd;
constexpr uint8_t kFloodedPace = 3, kDryPace = 2;
constexpr uint16_t kWaterSound = 0x1e4;

// the floor cell (column, row) broken open (its segment's strength below 0)
bool cellOpened(uint32_t column, uint32_t row) {
    return int16_t(uint16_t(cellSegmentStrength(ninoGridCell(column, row)))) < 0;
}
bool floodedRoom() { return storyFlag(kFlagFlooded) && gameState().room != kDrySub; }

} // namespace

bool ninoSt36RoomHandler(RoutineId fn, bool run) {
    RoomScriptState& room = roomScriptState();
    switch (fn) {
    case RoutineId::St36RoomHandler: // ST36 / ST37 / ST38 sub 1: step 0 -> the zone-slot group 0, step 1
    case RoutineId::St36Sub2RoomHandler: // ST36 sub 2: step 0 -> the zone-slot group 1, step 1
        if (run && room.handlerStep == 0) {
            encounterSpawnGroupZoneSlot(fn == RoutineId::St36RoomHandler ? 0 : 1); // exe 0x5e6640
            room.handlerStep += 1u;
        }
        return true;
    case RoutineId::St36Sub4RoomHandler:
        // ST36 sub 4: flag 0x9a once the floor cell (0x3b, 0x3c) is open; step 0 -> the group 3 when
    case RoutineId::St36Sub4RoomHandlerB:
        // flooded, else 2. Sub 10: the same with flag 0x9b, the cell (0x3f, 0x3f), the groups 8 / 7
        if (!run) return true;
        {
            const bool sub4 = fn == RoutineId::St36Sub4RoomHandler;
            const uint32_t flag = sub4 ? kFlagSub4Opened : kFlagSub10Opened;
            if (!storyFlag(flag) && (sub4 ? cellOpened(0x3b, 0x3c) : cellOpened(0x3f, 0x3f))) setStoryFlag(flag);
            if (room.handlerStep != 0) return true;
            encounterSpawnGroupZoneSlot(storyFlag(kFlagFlooded) ? (sub4 ? 3 : 8) : (sub4 ? 2 : 7));
            room.handlerStep += 1u;
        }
        return true;
    case RoutineId::St36Sub5RoomHandler: // ST36 sub 5: step 0 -> the group 6 when flooded, else 5
        if (run && room.handlerStep == 0) {
            encounterSpawnGroupZoneSlot(storyFlag(kFlagFlooded) ? 6 : 5);
            room.handlerStep += 1u;
        }
        return true;
    default: return false;
    }
}

void ninoSt36RoomCaseTail() { // exe 0x586d80 after [[0x8cb250 + story * 4] + sub * 4](handler step)
    RoomScriptState& room = roomScriptState();
    if (floodedRoom()) {
        switch (room.handlerByte3) {
        case 0:
            spawnRecords(stateAddress(TableId::St36RoomsEffectSpawns), 2); // exe 0x511090: the effect (pool 0xa0 type 0xa variant 1) + the bubbles
            room.handlerByte3 += 1u;
            [[fallthrough]];
        case 1:
            display().pace = kFloodedPace;
            player().inWater = 1;
            break;
        default: break;
        }
        return;
    }
    if (room.handlerByte3 == 0) {
        display().pace = kDryPace;
        player().inWater = 0;
        room.handlerByte3 += 1u;
    }
}

void ninoSt36RoomHelperTail() { // exe 0x586c90 after its handler step cleared / exe 0x55c000(-1, 0, 0)
    const uint8_t sub = gameState().room;
    if (sub == kSub4) {
        if (storyFlag(kFlagSub4Opened)) switchCellSegmentVariant(ninoGridCell(0x3b, 0x3c), 1); // exe 0x58d5f0
    } else if (sub == kSub10) {
        if (storyFlag(kFlagSub10Opened)) switchCellSegmentVariant(ninoGridCell(0x3f, 0x3f), 1);
    }
    if (floodedRoom()) {
        // exe 0x5d5380(0x1e4) called with one argument: its position is the stack slot above, the helper's return
        // address 0x5dc283 in the room function 0x5dc1f0; s16 +2 / +6 / +0xa of that code = 0x0d8a / 0x00a6 / 0x308f
        // (bytes 8a 0d .. a6 00 .. 8f 30)
        soundPlayAtXyz(kWaterSound, 0x0d8a, 0x00a6, 0x308f);
    }
}

} // namespace od::port
