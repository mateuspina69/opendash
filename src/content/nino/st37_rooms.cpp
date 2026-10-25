// The Nino ruin's ST37: the room-script side of the stage (wired from game/spawn: the script cases {0x37, by story,
// 0x8cb390}, the handler chain, the room helper). One routine per exe routine; the names are hypotheses from the
// observed behaviour.
//   0x586fe0  st37RoomHelperTail  the room helper (after its handler step cleared / exe 0x55c000(-1, 0, 0))
//   0x587120  st37RoomCaseTail    the stage case of the room scripts' tick after the handler call
//   0x5871e0 / 0x587300 / 0x587340 / 0x5873c0 / 0x587500 / 0x587580 / 0x587600 / 0x5876b0 / 0x5876d0 / 0x587710 /
//   0x587750  st37RoomHandler  (the zone-slot encounter groups; sub 6 at story 10: the boss room)
#include "content/nino/st37_rooms.hpp"

#include "content/nino/ch2_rooms.hpp"
#include "content/nino/pedestal_item.hpp"

#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/display.hpp"
#include "game/encounter.hpp"
#include "game/game_state.hpp"
#include "game/message.hpp"
#include "game/message_state.hpp"
#include "game/player/player_state.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/spawn.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

constexpr uint32_t kFlagDrained = 0x91; // the ruin's ST37 drained (its pedestal item used)
constexpr uint32_t kFlagSub2Opened = 0x9c, kFlagSub5Opened = 0x9d, kFlagSub9Opened = 0x9e; // the cells broken open
constexpr uint32_t kFlagHintNear = 0x599, kFlagHintFar = 0x59a;  // the hints (messages 0x28 / 0x29) shown
constexpr uint32_t kFlagOpeningSeen = 0x590, kFlagBossRoomCleared = 0x5a2, kFlagKeyTaken = 0x3e9;
constexpr uint8_t kSub2 = 2, kSub5 = 5, kSub9 = 9, kDrySub = 0xf;
constexpr uint8_t kFloodedPace = 3, kDryPace = 2;
constexpr uint16_t kWaterSound = 0x1e4;
constexpr uint8_t kOpeningScript = 0x5f;
constexpr int8_t kHintWindow = 4;
constexpr int16_t kHintNearMessage = 0x28, kHintFarMessage = 0x29, kClearedMessage = 0x30;
constexpr uint32_t kBossGroupIds[] = {0x13, 0x14, 0x15}; // the encounter ids of the boss room's three

// the hint window (window 4, the stage's message bank)
void hint(int16_t message) { windowOpen(kHintWindow, kMessageBank, message); } // exe 0x557390
// the room's cell (column, row) broken open (its segment's strength below 0) -> its flag (the opening checks of
// 0x5871e0 / 0x587340 / 0x587500 / 0x587600)
void flagOpenedCell(uint32_t flag, uint32_t column, uint32_t row) {
    if (!storyFlag(flag) && int16_t(uint16_t(cellSegmentStrength(ninoGridCell(column, row)))) < 0) setStoryFlag(flag);
}
// step 0: the zone slot spawns the group, the next step
void groupOnce(RoomScriptState& room, int group) {
    if (room.handlerStep != 0) return;
    encounterSpawnGroupZoneSlot(group); // exe 0x5e6640
    room.handlerStep += 1u;
}
bool drained() { return storyFlag(kFlagDrained); }

// the boss room's steps (sub 6, story 10)
enum class BossRoomStep : uint8_t {
    Enter = 0, Check = 1, Fight = 2, Wait = 3, Cleared = 4, KeyTaken = 5, KeyPlaced = 6, NoSlot = 7,
};
// exe 0x5873c0: the key already taken -> done; the room cleared before -> the key on its pedestal at the centre; else
// once no script runs: the opening (script 0x5f, once), then the fight of the encounter group 4 (its three no longer
// counted defeated), then the message 0x30 and the room cleared
void bossRoom(RoomScriptState& room) {
    switch (BossRoomStep(room.handlerStep)) {
    case BossRoomStep::Enter: room.handlerStep = uint8_t(BossRoomStep::Check); return;
    case BossRoomStep::Check:
        if (storyFlag(kFlagKeyTaken)) {
            room.handlerStep = uint8_t(BossRoomStep::KeyTaken);
            return;
        }
        if (storyFlag(kFlagBossRoomCleared)) {
            room.handlerStep = uint8_t(spawnPedestalKey(0, 0) ? BossRoomStep::KeyPlaced : BossRoomStep::NoSlot);
            return;
        }
        if (scriptState().inUse()) return;
        if (!storyFlag(kFlagOpeningSeen)) {
            scriptRun(kOpeningScript); // exe 0x511270
            setStoryFlag(kFlagOpeningSeen);
        }
        room.handlerStep += 1u;
        return;
    case BossRoomStep::Fight:
        for (const uint32_t id : kBossGroupIds) enemyClearDefeated(id);
        encounterSpawnGroupScriptSlot(4);
        room.handlerStep += 1u;
        return;
    case BossRoomStep::Wait:
        if (encounterScriptSlotAlive() != 0) return;
        hint(kClearedMessage);
        setStoryFlag(kFlagBossRoomCleared);
        room.handlerStep += 1u;
        return;
    default: return;
    }
}
void handler(RoutineId fn) {
    RoomScriptState& room = roomScriptState();
    const Player& p = player();
    switch (fn) {
    case RoutineId::St37StoryASub2RoomHandler:
        // sub 2, story 10: the group 1 (9 drained); the two hints by the player's z
        flagOpenedCell(kFlagSub2Opened, 0x3c, 0x3f);
        if (room.handlerStep == 0) {
            if (drained()) {
                encounterSpawnGroupZoneSlot(9);
                room.handlerStep = 2;
            } else {
                encounterSpawnGroupZoneSlot(1);
                room.handlerStep = 1;
            }
        } else if (room.handlerStep == 1) {
            if (!storyFlag(kFlagHintNear) && p.pos.z.whole > 0x160) {
                hint(kHintNearMessage);
                setStoryFlag(kFlagHintNear);
            }
            if (!storyFlag(kFlagHintFar) && p.pos.z.whole > 0x600) {
                hint(kHintFarMessage);
                setStoryFlag(kFlagHintFar);
            }
        }
        return;
    case RoutineId::St37StoryASub4RoomHandler: // sub 4, story 10: the group 2; the far hint marked seen unless drained
        if (room.handlerStep != 0) return;
        encounterSpawnGroupZoneSlot(2);
        if (!drained()) setStoryFlag(kFlagHintFar);
        room.handlerStep += 1u;
        return;
    case RoutineId::St37Sub5RoomHandler: // sub 5: the group 3
        flagOpenedCell(kFlagSub5Opened, 0x3c, 0x44);
        groupOnce(room, 3);
        return;
    case RoutineId::St37RoomHandler: bossRoom(room); return;
    case RoutineId::St37Sub9RoomHandler: // sub 9: the group 6
        flagOpenedCell(kFlagSub9Opened, 0x42, 0x40);
        groupOnce(room, 6);
        return;
    case RoutineId::St37StoryASub13RoomHandler:
        // sub 13, story 10: the group 7 (0xa drained); the near hint by the player's x
        if (room.handlerStep == 0) {
            if (drained()) {
                encounterSpawnGroupZoneSlot(0xa);
                room.handlerStep = 2;
            } else {
                encounterSpawnGroupZoneSlot(7);
                room.handlerStep = 1;
            }
        } else if (room.handlerStep == 1 && !storyFlag(kFlagHintNear) && p.pos.x.whole > -0x2a0) {
            hint(kHintNearMessage);
            setStoryFlag(kFlagHintNear);
        }
        return;
    case RoutineId::St37StoryBOnSub2RoomHandler: // sub 2, stories 11..: the group 1 (9 drained)
        flagOpenedCell(kFlagSub2Opened, 0x3c, 0x3f);
        if (room.handlerStep != 0) return;
        if (drained()) {
            encounterSpawnGroupZoneSlot(9);
            room.handlerStep = 3;
        } else {
            encounterSpawnGroupZoneSlot(1);
            room.handlerStep = 2;
        }
        return;
    case RoutineId::St37StoryBOnSub4RoomHandler: groupOnce(room, 2); return; // sub 4, stories 11..
    case RoutineId::St37Sub8RoomHandler:                             // sub 8: the group 5 (0xb drained)
        if (room.handlerStep != 0) return;
        encounterSpawnGroupZoneSlot(drained() ? 0xb : 5);
        room.handlerStep += 1u;
        return;
    case RoutineId::St37StoryBOnSub13RoomHandler: // sub 13, stories 11..: the group 7 (0xa drained)
        if (room.handlerStep != 0) return;
        encounterSpawnGroupZoneSlot(drained() ? 0xa : 7);
        room.handlerStep += 1u;
        return;
    case RoutineId::St37Sub14RoomHandler: groupOnce(room, 8); return; // sub 14
    default: return;
    }
}

} // namespace

bool st37RoomHandler(RoutineId fn, bool run) {
    switch (fn) {
    case RoutineId::St37StoryASub2RoomHandler:
    case RoutineId::St37StoryASub4RoomHandler:
    case RoutineId::St37Sub5RoomHandler:
    case RoutineId::St37RoomHandler:
    case RoutineId::St37Sub9RoomHandler:
    case RoutineId::St37StoryASub13RoomHandler: case RoutineId::St37StoryBOnSub2RoomHandler:
    case RoutineId::St37StoryBOnSub4RoomHandler:
    case RoutineId::St37Sub8RoomHandler:
    case RoutineId::St37StoryBOnSub13RoomHandler: case RoutineId::St37Sub14RoomHandler:
        if (run) handler(fn);
        return true;
    default: return false;
    }
}

void st37RoomHelperTail() { // exe 0x586fe0 after exe 0x55c000
    switch (gameState().room) {
    case kSub2:
        if (storyFlag(kFlagSub2Opened)) switchCellSegmentVariant(ninoGridCell(0x3c, 0x3f), 1); // exe 0x58d5f0
        break;
    case kSub5:
        if (storyFlag(kFlagSub5Opened)) switchCellSegmentVariant(ninoGridCell(0x3c, 0x44), 1);
        break;
    case kSub9:
        if (storyFlag(kFlagSub9Opened)) switchCellSegmentVariant(ninoGridCell(0x42, 0x40), 1);
        break;
    default: break;
    }
    // exe 0x5d5380(0x1e4) called with one argument: its position is the stack slot above, the helper's return address
    // 0x5dc433 in the room function 0x5dc3a0; s16 +2 / +6 / +0xa of that code = 0x0d8a / 0x00a6 / 0x2edf
    if (!drained() && gameState().room != kDrySub) soundPlayAtXyz(kWaterSound, 0x0d8a, 0x00a6, 0x2edf);
}

void st37RoomCaseTail() { // exe 0x587120 after [[0x8cb390 + story * 4] + sub * 4](handler step)
    RoomScriptState& room = roomScriptState();
    if (!drained() && gameState().room != kDrySub) {
        switch (room.handlerByte3) {
        case 0:
            spawnRecords(stateAddress(TableId::St37RoomsWaterRecords), 2); // exe 0x511090: the water's effects (pool 0xa0 type 0xa, pool 0xe0 type 0x20)
            room.handlerByte3 += 1u;
            [[fallthrough]];
        case 1:
            display().pace = kFloodedPace; // the flooded ruin runs at the 20 fps pace
            player().inWater = 1;          // the player swims
            break;
        default: break;
        }
        return;
    }
    if (room.handlerByte3 == 0) {
        display().pace = kDryPace;
        room.handlerByte3 += 1u;
        player().inWater = 0;
    }
}

} // namespace od::port

// the case 0x587120 calls exe 0x511090 (spawnRecords) itself: st37RoomCaseTail kHandlersTranscribed:
// 0x511090
