// The Nino ruin's lowest floor ST38 (see content/nino/st38_rooms.hpp): the room-script handlers 0x5878d0 / 0x587920 /
// 0x587960 / 0x587a00 / 0x587a40 / 0x587a70 / 0x587ab0 / 0x587b00 and the tail of the stage case 0x587800 (the room
// helper 0x587770 is content/nino/nino_rooms ninoRoomHelper's: it needs exe 0x55c000). One routine per exe routine;
// the names are hypotheses from the observed behaviour.
#include "content/nino/st38_rooms.hpp"

#include "content/nino/ch2_rooms.hpp"

#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/display.hpp"
#include "game/encounter.hpp"
#include "game/player/player_state.hpp"
#include "game/room_scripts.hpp"
#include "game/spawn.hpp"
#include "game/stage_state.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

constexpr uint32_t kFlagDrained = 0x92;   // the floor drained (the boss beaten)
constexpr uint32_t kFlagSub5Opened = 0x9f; // the floor cell of sub 5 broken open
constexpr uint32_t kFlagLeaving = 0x700;  // the armed exit
constexpr uint16_t kBossMusic = 0x37, kFloorMusic = 0x36, kMusicFadeSlow = 0x222;
constexpr uint8_t kFloodedPace = 3, kDryPace = 2;

// the boss room's music latch (the stage block's first word: set by the boss room, read by subs 10 / 12)
uint32_t& bossMusicLatch() { return stageBlock().part<StageBlockHead>().word; }

} // namespace

bool nino38RoomHandler(RoutineId fn, bool run) {
    RoomScriptState& room = roomScriptState();
    switch (fn) {
    case RoutineId::St38Sub2RoomHandler:
        // sub 2: once: drained -> the group 9; else the group 1 + records 0x8cb548 x 4 (type 0x42)
        if (!run || room.handlerStep != 0) return true;
        if (storyFlag(kFlagDrained)) {
            encounterSpawnGroupZoneSlot(9);
        } else {
            encounterSpawnGroupZoneSlot(1);
            spawnRecords(stateAddress(TableId::St38RoomsFloodedSpawns), 4);
        }
        room.handlerStep += 1u;
        return true;
    case RoutineId::St38Sub3RoomHandler: // sub 3: once: the group 2 (drained) / 3
    case RoutineId::St38Sub6RoomHandler: // sub 6: once: the group 6 (drained) / 7
        if (!run || room.handlerStep != 0) return true;
        if (fn == RoutineId::St38Sub3RoomHandler) encounterSpawnGroupZoneSlot(storyFlag(kFlagDrained) ? 2 : 3);
        else encounterSpawnGroupZoneSlot(storyFlag(kFlagDrained) ? 6 : 7);
        room.handlerStep += 1u;
        return true;
    case RoutineId::St38Sub5RoomHandler:
        // sub 5: every tick: the cell (0x41, 0x3e) broken open -> flag 0x9f; once: the group 4 / 5
        if (!run) return true;
        if (!storyFlag(kFlagSub5Opened)) {
            // exe 0x58d570(cell): a switchable segment cell's strength (its low s16)
            if (int16_t(uint16_t(cellSegmentStrength(ninoGridCell(0x41, 0x3e)))) < 0) setStoryFlag(kFlagSub5Opened);
        }
        if (room.handlerStep != 0) return true;
        encounterSpawnGroupZoneSlot(storyFlag(kFlagDrained) ? 4 : 5);
        room.handlerStep += 1u;
        return true;
    case RoutineId::St38Sub7RoomHandler: // sub 7: once: not drained -> records 0x8cb548 x 4
        if (!run || room.handlerStep != 0) return true;
        if (!storyFlag(kFlagDrained)) spawnRecords(stateAddress(TableId::St38RoomsFloodedSpawns), 4);
        room.handlerStep += 1u;
        return true;
    case RoutineId::St38Sub8RoomHandler: // sub 8: once: the group 8; not drained -> records 0x8cb598 x 8
        if (!run || room.handlerStep != 0) return true;
        encounterSpawnGroupZoneSlot(8);
        if (!storyFlag(kFlagDrained)) spawnRecords(stateAddress(TableId::St38RoomsFloodedMoreSpawns), 8);
        room.handlerStep += 1u;
        return true;
    case RoutineId::St38RoomHandler:
        // subs 10 / 12: 0: step 2, or 1 when the boss music latch was set (cleared); 1: the music faded
        if (!run) return true; // out -> the floor's music 0x36 (forced), 2
        switch (room.handlerStep) {
        case 0:
            room.handlerStep = bossMusicLatch() == 0u ? 2 : 1;
            bossMusicLatch() = 0;
            return true;
        case 1:
            if (soundDone(kMusicFades)) { // exe 0x5d5900
                soundPlayFlatForced(kFloorMusic);
                room.handlerStep += 1u;
            }
            return true;
        default: return true;
        }
    case RoutineId::St38Sub11RoomHandler:
        // sub 11 (the boss room): 0: not drained -> records 0x8cb638 x 0x11 (the boss + 16 type 0x42), the
        // latch set, the music faded -> 1; drained -> 3; 1: faded -> the boss music 0x37 (forced), 2; 2: leaving (flag
        // 0x700) -> the music faded
        if (!run) return true;
        switch (room.handlerStep) {
        case 0:
            if (storyFlag(kFlagDrained)) {
                room.handlerStep = 3;
                return true;
            }
            spawnRecords(stateAddress(TableId::St38RoomsDrainedSpawns), 0x11);
            bossMusicLatch() = 1;
            soundFadeOut(kMusicFades, kMusicFadeSlow, 0); // exe 0x5d5780
            room.handlerStep = 1;
            return true;
        case 1:
            if (soundDone(kMusicFades)) {
                soundPlayFlatForced(kBossMusic);
                room.handlerStep += 1u;
            }
            return true;
        case 2:
            if (storyFlag(kFlagLeaving)) soundFadeOut(kMusicFades, kMusicFadeSlow, 0);
            return true;
        default: return true;
        }
    default: return false;
    }
}

void nino38RoomCaseTail() {
    RoomScriptState& room = roomScriptState();
    if (!storyFlag(kFlagDrained)) {
        switch (room.handlerByte3) {
        case 0:
            spawnRecords(stateAddress(TableId::St38RoomsEffectSpawns), 2); // the effect (pool 0xa0 type 0xa) + the bubbles (pool 0xe0 type 0x20)
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

} // namespace od::port
