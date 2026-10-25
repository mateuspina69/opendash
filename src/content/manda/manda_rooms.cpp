// The Manda ruin's rooms in the room setup and the room scripts (see manda_rooms.hpp), one routine per exe routine.
#include "content/manda/manda_rooms.hpp"

#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/game_state.hpp"
#include "game/player/player_state.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/spawn.hpp"
#include "game/story_flags.hpp"
#include "game/visited_rooms.hpp"

namespace od::content {

using namespace od::port;

namespace {

constexpr uint32_t kFlagSt12EntryVisited = 0x7ac; // ST12 sub 0's map flag
constexpr uint32_t kFlagSt13PassageA = 1;        // ST13 sub 2's passage opened
constexpr uint32_t kFlagSt13PassageB = 2;        // ST13 sub 6's

bool mandaRoomHelper(RoutineId fn, bool run) {
    const GameState& g = gameState();
    RoomScriptState& room = roomScriptState();
    switch (fn) {
    case RoutineId::St12RoomHelper: // ST12: no room list; sub 0's map flag
        if (!run) return true;
        room.clearHandlerWord();
        roomListSetup(kNoRoomList, 0, 0);
        if (g.room == 0) setStoryFlag(kFlagSt12EntryVisited);
        return true;
    case RoutineId::St13RoomHelper:
        // ST13: no room list; sub 2 / sub 6 with their passage opened: two cells switched each
        if (!run) return true;
        room.clearHandlerWord();
        roomListSetup(kNoRoomList, 0, 0);
        if (g.room == 2) {
            if (!storyFlag(kFlagSt13PassageA)) return true;
            switchGridCellVariant(0x3f, 0x40, 1);
            switchGridCellVariant(0x3f, 0x3e, 2);
        } else if (g.room == 6) {
            if (!storyFlag(kFlagSt13PassageB)) return true;
            switchGridCellVariant(0x3f, 0x3f, 1);
            switchGridCellVariant(0x3d, 0x3f, 2);
        }
        return true;
    default: return false;
    }
}

// ---- the room scripts of story 4 and after ---------------------------------------------------------------------------
constexpr uint32_t kFlagExitLock = kFlagLocked;                                       // the room's exit locked
constexpr uint32_t kFlagDoorLockA = kFlagLocked + 1, kFlagDoorLockB = kFlagLocked + 2; // ST12's two doors
constexpr uint32_t kFlagDoorSwitchA = 5, kFlagDoorSwitchB = 6;                         // their switches pressed
constexpr uint32_t kFlagCardUsed = 0x5b6;        // the blue card used at the terminal (ST12 sub 0)
constexpr uint32_t kFlagCardChestOpened = 0x3e4; // the chest of ST12 sub 5 (the card)
constexpr uint32_t kFlagSecondHallSeen = 0x5b2, kFlagSecondGuardianBeaten = 0x5b3;
constexpr uint32_t kFlagFirstHallSeen = 0x5b0, kFlagFirstGuardianBeaten = 0x5b1;
constexpr uint32_t kFlagLastHallSeen = 0x5b9, kFlagRuinGuardianBeaten = 0x5b5;
constexpr uint32_t kFlagRuinKeyTaken = 0x3d0; // the key item of the last hall taken
constexpr uint8_t kScriptFirstHall = 9, kScriptAfterFirstGuardian = 10, kScriptSecondHall = 0xb,
                  kScriptAfterSecondGuardian = 0xc, kScriptTerminal = 0xf, kScriptLastHall = 0x1a,
                  kScriptRuinGuardianBeaten = 0xe;
constexpr uint32_t kMusicChannel2 = 4; // the fade entry of the third music channel
constexpr int16_t kHallDipStep = 0xa3;

// the five cells of the terminal's platform (row 0x3f, columns 0x3b..0x43 step 2) up
void platformUp() {
    for (uint32_t x = 0x3b; x <= 0x43; x += 2) switchGridCellVariant(x, 0x3f, 2);
}
// the once-per-visit clear of the room's locks (the exit, and the doors asked), then the door test
bool unlockOnce(bool run, bool doorA, bool doorB) {
    if (!run) return true;
    RoomScriptState& room = roomScriptState();
    if (room.handlerStep == 0) {
        clearStoryFlag(kFlagExitLock);
        if (doorA) clearStoryFlag(kFlagDoorLockA);
        if (doorB) clearStoryFlag(kFlagDoorLockB);
        ++room.handlerStep;
    }
    roomDoorScript();
    return true;
}
// exe 0x57e380 (ST12 story 4 sub 0, the entrance hall): step 0 -- the exit open, the doors locked, the terminal's
// record (with the card used: its other record and the platform up, step 2); step 1 -- the card used and no event:
// script 0xf; bytes 1 / 2: the doors' switches unlock them; the door test
bool entranceHall(bool run) {
    if (!run) return true;
    RoomScriptState& room = roomScriptState();
    if (room.handlerStep == 0) {
        clearStoryFlag(kFlagExitLock);
        setStoryFlag(kFlagDoorLockA);
        setStoryFlag(kFlagDoorLockB);
        spawnRecords(storyFlag(kFlagCardUsed) ? stateAddress(TableId::MandaRoomsEntranceHallSpawnsCardUsed) : stateAddress(TableId::MandaRoomsEntranceHallSpawns), 1);
        if (!storyFlag(kFlagCardUsed)) {
            room.handlerStep = 1;
        } else {
            platformUp();
            room.handlerStep = 2;
        }
    } else if (room.handlerStep == 1 && eventState().active == 0 && storyFlag(kFlagCardUsed)) {
        scriptRun(kScriptTerminal);
        room.handlerByte2 = 3;
    }
    if (room.handlerByte1 == 0 && storyFlag(kFlagDoorSwitchA)) {
        clearStoryFlag(kFlagDoorLockA);
        room.handlerByte1 = 1;
    }
    if (room.handlerByte2 == 0 && storyFlag(kFlagDoorSwitchB)) {
        clearStoryFlag(kFlagDoorLockB);
        room.handlerByte2 = 1;
    }
    roomDoorScript();
    return true;
}
// exe 0x57e5c0 (ST12 story 4 sub 4): the second door locked until its switch; the door test
bool secondDoorRoom(bool run) {
    if (!run) return true;
    RoomScriptState& room = roomScriptState();
    if (room.handlerStep == 0) {
        setStoryFlag(kFlagDoorLockB);
        ++room.handlerStep;
    }
    if (room.handlerStep == 1 && storyFlag(kFlagDoorSwitchB)) {
        clearStoryFlag(kFlagDoorLockB);
        ++room.handlerStep;
    }
    roomDoorScript();
    return true;
}
// a guardian hall (exe 0x57e630 ST12 sub 5 after the card's chest; exe 0x57ea80 ST13 sub 1 at the first visit): the
// transition flags cleared, the exit locked, the hall's script; once its guardian is beaten the script after it, the
// exit open; the door test
bool guardianHall(bool run, bool secondHall) {
    if (!run) return true;
    RoomScriptState& room = roomScriptState();
    const uint32_t seen = secondHall ? kFlagSecondHallSeen : kFlagFirstHallSeen;
    const uint32_t beaten = secondHall ? kFlagSecondGuardianBeaten : kFlagFirstGuardianBeaten;
    const uint8_t hallScript = secondHall ? kScriptSecondHall : kScriptFirstHall;
    const uint8_t afterScript = secondHall ? kScriptAfterSecondGuardian : kScriptAfterFirstGuardian;
    if (room.handlerStep == 0) {
        const bool start = secondHall ? !storyFlag(seen) && storyFlag(kFlagCardChestOpened) && eventState().active == 0
                                      : !storyFlag(seen);
        if (secondHall && storyFlag(seen)) {
            room.handlerStep = 2;
        } else if (start) {
            clearStoryFlag(kFlagTransitionArmed);
            clearStoryFlag(kFlagTransitionArriving);
            setStoryFlag(seen);
            setStoryFlag(kFlagExitLock);
            scriptRun(hallScript);
            room.handlerStep = 1;
            return true;
        } else if (!secondHall) {
            room.handlerStep = 2;
        }
    } else if (room.handlerStep == 1 && storyFlag(beaten)) {
        scriptRun(afterScript);
        clearStoryFlag(kFlagExitLock);
        room.handlerStep = 2;
        return true;
    }
    roomDoorScript();
    return true;
}
// exe 0x57ed80 (ST14 story 4 sub 4, the last hall): 0 -> 1 (5 once seen); 1: the player past x 0xa7f near z 0 ->
// seen, the exit locked, script 0x1a; 2: the guardian beaten -> script 0xe; 3: the key taken and no event -> event
// message 0x16; 4: no event -> event message 0x17
bool lastHall(bool run) {
    if (!run) return true;
    RoomScriptState& room = roomScriptState();
    switch (room.handlerStep) {
    case 0: room.handlerStep = storyFlag(kFlagLastHallSeen) ? 5u : 1u; return true;
    case 1: {
        const Player& p = player();
        if (p.pos.x.whole > 0xa7f && p.pos.z.whole > -0x81 && p.pos.z.whole < 0x81) {
            setStoryFlag(kFlagLastHallSeen);
            setStoryFlag(kFlagExitLock);
            scriptRun(kScriptLastHall);
            room.handlerStep = 2;
        }
        return true;
    }
    case 2:
        if (storyFlag(kFlagRuinGuardianBeaten)) {
            scriptRun(kScriptRuinGuardianBeaten);
            room.handlerStep = 3;
        }
        return true;
    case 3:
        if (storyFlag(kFlagRuinKeyTaken) && eventState().active == 0) {
            eventStartAt(0, nullptr, 0, 0, 0x16); // exe 0x543b40
            ++room.handlerStep;
        }
        return true;
    case 4:
        if (eventState().active == 0) {
            eventStartAt(0, nullptr, 0xb, 0, 0x17);
            ++room.handlerStep;
        }
        return true;
    default: return true;
    }
}

} // namespace

bool mandaRuinRoomHandler(RoutineId fn, bool run) {
    RoomScriptState& room = roomScriptState();
    switch (fn) {
    case RoutineId::MandaEntranceHallRoomHandler: return entranceHall(run);
    case RoutineId::MandaSecondDoorRoomHandler: return secondDoorRoom(run);
    case RoutineId::MandaGuardianHallRoomHandler: return guardianHall(run, true);
    // ST12 stories 5.. (sub table 0x8c6efc): the exit and the doors open
    case RoutineId::St12Sub0RoomHandler: // sub 0: also the platform up
        if (!run) return true;
        if (room.handlerStep == 0) {
            clearStoryFlag(kFlagExitLock);
            clearStoryFlag(kFlagDoorLockA);
            clearStoryFlag(kFlagDoorLockB);
            platformUp();
            ++room.handlerStep;
        }
        roomDoorScript();
        return true;
    case RoutineId::St12Sub4RoomHandler: return unlockOnce(run, true, true);  // sub 4
    case RoutineId::St12Sub5RoomHandler: return unlockOnce(run, true, false); // sub 5
    // ST13 stories 5.. (sub table 0x8c6fdc)
    case RoutineId::St13Sub0RoomHandler: // sub 0: the exit open every tick; the door test
        if (!run) return true;
        clearStoryFlag(kFlagExitLock);
        roomDoorScript();
        return true;
    case RoutineId::St13Sub1RoomHandler: return unlockOnce(run, false, false); // sub 1
    case RoutineId::St13RoomHandler: return guardianHall(run, false);
    case RoutineId::St13RoomHandlerA:
    case RoutineId::St13RoomHandlerB: {
        // ST13 story 4 subs 2 / 6: their passage's flag once the breakable segment of its cell (column
        // 0x3f, row 0x40 / 0x3f) is broken open; the door test
        if (!run) return true;
        const uint32_t f = fn == RoutineId::St13RoomHandlerA ? kFlagSt13PassageA : kFlagSt13PassageB;
        if (!storyFlag(f) && gridCellSegmentBroken(0x3f, fn == RoutineId::St13RoomHandlerA ? 0x40u : 0x3fu))
            setStoryFlag(f);
        roomDoorScript();
        return true;
    }
    case RoutineId::St14Story4Sub2RoomHandler:
        // ST14 story 4 sub 2: once: the exit open, the third music channel restored
        if (!run || room.handlerStep != 0) return true;
        clearStoryFlag(kFlagExitLock);
        soundFadeRestore(kMusicChannel2, kHallDipStep, 0);
        ++room.handlerStep;
        return true;
    case RoutineId::St14Story4Sub3RoomHandler:
        // ST14 story 4 sub 3: once: the exit open, the third channel dipped, the guardian's record until it
        // is beaten
        if (!run || room.handlerStep != 0) return true;
        clearStoryFlag(kFlagExitLock);
        soundFadeDip(kMusicChannel2, kHallDipStep, 0, 0);
        if (!storyFlag(kFlagRuinGuardianBeaten)) spawnRecords(stateAddress(TableId::MandaRoomsSt14GuardianSpawns), 1);
        ++room.handlerStep;
        return true;
    case RoutineId::St13RoomHandlerC: return lastHall(run);
    default: return false;
    }
}

void registerMandaRooms(ContentRegistry& r) { r.roomHelpers.push_back(&mandaRoomHelper); }

} // namespace od::content
