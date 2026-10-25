// The ruin under Nino, ST35 (story 0xa, after Glyde's defeat: ST19 sub 1 with flag 0x3da -> ST35 sub 0). Its room
// function 0x5dc040 is the plain template + the helper 0x586900 (content/nino/nino_rooms ninoRoomHelper); the room
// scripts' case 0x586950 runs [[0x8cb130 + story * 4] + sub * 4] (story 0xa: the table 0x8cb0e8), then the pace tail.
// The flow (the handlers, their step byte):
//   sub 5 0x586b30: the item on its pedestal (static type 0x40, record 0x8cb1b0, used -> flag 0x3d1) -> the event message
//     0x2b -> flag 0x93 -> script 0x15 (the guardian wakes) + the door lock 0x711 -> the guardian variant 0 (record
//     0x8cb188) + its music -> beaten (flag 0x94) -> the music faded, script 0x16, the lock 0x711 open.
//   sub 0 0x5869c0: on the way back (flag 0x94): script 0x17 (the second guardian falls in), the locks 0x710 / 0x711 ->
//     the guardian variant 1 (record 0x8cb19c) -> beaten (flag 0x95) -> script 0x19, the locks open.
//   sub 1 0x586aa0, sub 3 0x586ac0 (an NPC record 0x8cb1c8 until flag 0x93), sub 4 0x586af0 (the lock 0x711 until the
//     flags 0x3e9 and 0x3ea).
#include "content/nino/st35_rooms.hpp"

#include "game/audio_logic.hpp"
#include "game/display.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/player/player_state.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/spawn.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

constexpr uint32_t kFlagGuardianWakes = 0x93, kFlagFirstBeaten = 0x94, kFlagSecondBeaten = 0x95;
constexpr uint32_t kFlagItemUsed = 0x3d1, kFlagKeyA = 0x3e9, kFlagKeyB = 0x3ea;
constexpr uint32_t kLockA = 0x710, kLockB = 0x711;
constexpr uint8_t kScriptWakes = 0x15, kScriptBeaten = 0x16, kScriptDropsIn = 0x17, kScriptDissolves = 0x19;
constexpr uint16_t kGuardianMusic = 0x38, kMusicFadeSlow = 0x222;
constexpr uint16_t kItemWindow = 4, kItemMessage = 0x2b;
constexpr uint8_t kDryPace = 2;

// the guardian's fight steps (the handler's step byte)
enum class FightStep : uint8_t { Start = 0, Wait = 1, Spawn = 2, Fight = 3, Done = 4 };
// the item's steps (the handler's second byte)
enum class ItemStep : uint8_t { Spawn = 0, Wait = 1, Event = 2 };

void musicFade() { soundFadeOut(kMusicFades, kMusicFadeSlow, 0); } // exe 0x5d5780(0xf, 0x222, 0)

// exe 0x5869c0 (sub 0): the second guardian on the way back
void wayBack(RoomScriptState& room) {
    switch (FightStep(room.handlerStep)) {
    case FightStep::Start:
        room.handlerStep = uint8_t(storyFlag(kFlagSecondBeaten) ? FightStep::Done : FightStep::Wait);
        return;
    case FightStep::Wait:
        if (!storyFlag(kFlagFirstBeaten)) return;
        scriptRun(kScriptDropsIn); // exe 0x511270
        setStoryFlag(kLockA);
        setStoryFlag(kLockB);
        room.handlerStep = uint8_t(FightStep::Spawn);
        return;
    case FightStep::Spawn:
        spawnRecords(stateAddress(TableId::St35RoomsWayBackSpawns), 1);             // exe 0x511090: the guardian variant 1
        soundPlayFlatForced(kGuardianMusic);   // exe 0x5d5260
        room.handlerStep = uint8_t(FightStep::Fight);
        return;
    case FightStep::Fight:
        if (!storyFlag(kFlagSecondBeaten)) return;
        musicFade();
        scriptRun(kScriptDissolves);
        clearStoryFlag(kLockA);
        clearStoryFlag(kLockB);
        room.handlerStep = uint8_t(FightStep::Done);
        return;
    default: return;
    }
}
// exe 0x586b30 (sub 5): the item, then the first guardian
void itemRoom(RoomScriptState& room) {
    switch (FightStep(room.handlerStep)) {
    case FightStep::Start:
        room.handlerStep = uint8_t(storyFlag(kFlagFirstBeaten) ? FightStep::Done : FightStep::Wait);
        return;
    case FightStep::Wait:
        switch (ItemStep(room.handlerByte1)) {
        case ItemStep::Spawn:
            spawnRecords(stateAddress(TableId::St35RoomsItemSpawns), 1); // the item on its pedestal (static type 0x40, flag 0x3d1)
            room.handlerByte1 = uint8_t(ItemStep::Wait);
            [[fallthrough]];
        case ItemStep::Wait:
            if (storyFlag(kFlagItemUsed) && eventState().active == 0) {
                eventStartAt(stateAddress(TableId::St35RoomsItemEventPlace), nullptr, 0, kItemWindow, kItemMessage); // exe 0x543b40
                room.handlerByte1 = uint8_t(ItemStep::Event);
            }
            break;
        case ItemStep::Event:
            if (eventState().active == 0) setStoryFlag(kFlagGuardianWakes);
            break;
        default: break;
        }
        if (storyFlag(kFlagGuardianWakes)) {
            scriptRun(kScriptWakes);
            setStoryFlag(kLockB);
            room.handlerStep = uint8_t(FightStep::Spawn);
        }
        return;
    case FightStep::Spawn:
        spawnRecords(stateAddress(TableId::St35RoomsGuardianSpawns), 1); // the guardian variant 0
        soundPlayFlatForced(kGuardianMusic);
        room.handlerStep = uint8_t(FightStep::Fight);
        [[fallthrough]];
    case FightStep::Fight:
        if (!storyFlag(kFlagFirstBeaten)) return;
        musicFade();
        scriptRun(kScriptBeaten);
        clearStoryFlag(kLockB);
        room.handlerStep = uint8_t(FightStep::Done);
        return;
    default: return;
    }
}

} // namespace

bool ninoRuinSt35RoomHandler(RoutineId fn, bool run) {
    RoomScriptState& room = roomScriptState();
    switch (fn) {
    case RoutineId::St35RoomHandler:
        if (run) wayBack(room);
        return true;
    case RoutineId::St35Sub1RoomHandler: // sub 1: step 0 -> 1 once the first guardian is beaten
        if (run && room.handlerStep == 0 && storyFlag(kFlagFirstBeaten)) room.handlerStep = 1;
        return true;
    case RoutineId::St35Sub3RoomHandler:
        // sub 3: once, before the guardian woke, the record 0x8cb1c8 (pool 0x20 type 1)
        if (!run || room.handlerStep != 0) return true;
        if (!storyFlag(kFlagGuardianWakes)) spawnRecords(stateAddress(TableId::St35RoomsGuardianWakesSpawns), 1);
        room.handlerStep = 1;
        return true;
    case RoutineId::St35Sub4RoomHandler: // sub 4: once, without both keys the lock 0x711
        if (!run || room.handlerStep != 0) return true;
        if (!storyFlag(kFlagKeyA) || !storyFlag(kFlagKeyB)) {
            setStoryFlag(kLockB);
            room.handlerStep = 1;
        }
        return true;
    case RoutineId::St35RoomHandlerA:
        if (run) itemRoom(room);
        return true;
    default: return false;
    }
}

void ninoRuinSt35RoomTail() {
    RoomScriptState& room = roomScriptState();
    if (room.handlerByte3 != 0) return;
    display().pace = kDryPace;
    player().inWater = 0;
    room.handlerByte3 += 1u;
}

} // namespace od::port
