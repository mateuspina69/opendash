// The room scripts of ST41 / ST42 / ST44: the handlers of the room scripts' stage cases ST41 [0x8cc900 + sub * 4],
// ST42 [0x8cca08 + sub * 4], ST44 [0x8ccc14 + sub * 4] (their state: the room scripts' handler step, its second byte a
// timer). One case per exe routine (addresses in the comments).
#include "content/saul_kada/st41_st44_rooms.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/encounter.hpp"
#include "game/message_triggers.hpp"
#include "game/player/player_state.hpp"
#include "game/room_scripts.hpp"
#include "game/spawn.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

// the room's guards: the spawn lists and the flags that end them
constexpr uint32_t kFlagGuardsA = 0x680, kFlagGuardsB = 0x681, kFlagGuardsC = 0x682, kFlagGuardsD = 0x683,
                   kFlagPairDown = 0x684;
constexpr uint32_t kFlagDoorLock = kFlagLocked + 1; // 0x711: the room's door locked while the guards stand
constexpr uint32_t kFlagGateLine = 0x153, kFlagSt41Lock = 0x713, kFlagSt42Lock = 0x714;
constexpr uint32_t kFlagSt41HintA = 0x590, kFlagSt41HintB = 0x591, kFlagSt41HintC = 0x592;
constexpr uint32_t kFlagSt42PairDone = 0x59d, kFlagSt42GuardsGate = 0x3e1;
constexpr uint32_t kFlagSt44Done = 0x172, kFlagSt44Gate = 0x155, kFlagSt44Lock = kFlagLocked;
constexpr uint8_t kTriggerCountdown = 0xe;
constexpr int8_t kTriggerLastStory = 0x12;
constexpr uint8_t kHandlerDone = 0xff;

// the room's actor i alive (its first byte's kActorLive; the exe reads through the slot unchecked). ST42 sub 14 without
// flag 0x3e1 spawns no guards and still counts slots 0..3 down: a slot left by an earlier room's guards reads as
// down, an empty one (the stage load clears the list; only sub 4's guards fill slots 2 / 3) is an address-0 read in
// the exe -- here down too, the same outcome as every slot that holds an actor no longer live
bool roomActorAlive(uint32_t i) {
    const uint32_t a = roomScriptState().actors[i];
    return a != 0 && (actorHeaderAt(a).flags & kActorLive) != 0;
}
// the n first room actors all down
bool roomActorsDown(uint32_t n) {
    uint32_t i = 0;
    while (i < n && !roomActorAlive(i)) ++i;
    return i == n;
}
// exe 0x58a830(variant, message): a message trigger (pool 0xe0 type 0x1b) of the variant showing the message, until
// story 0x12, setting no flag; nullptr when the pool is full
ControllerActor* spawnMessageTrigger(uint8_t variant, int16_t message) {
    const uint32_t slot = allocActorE0(); // exe 0x5e1440
    if (slot == 0) return nullptr;
    ControllerActor& trigger = controllerActorAt(slot);
    MessageTriggerFields& fields = classState<MessageTriggerFields>(trigger);
    trigger.flags = kActorLive;
    trigger.type = kMessageTriggerType;
    trigger.variant = variant;
    fields.flag = kNoMessageFlag;
    fields.message = message;
    fields.lastStory = kTriggerLastStory;
    return &trigger;
}
// ST41 subs 0 / 1: start -> wait `delay` ticks -> once (flag) the message shown at once
void delayedMessage(uint8_t delay, uint32_t flag, int16_t message) {
    RoomScriptState& room = roomScriptState();
    switch (room.handlerStep) {
    case 0:
        room.handlerByte1 = delay;
        ++room.handlerStep;
        return;
    case 1:
        --room.handlerByte1;
        if (room.handlerByte1 != 0) return;
        ++room.handlerStep;
        if (storyFlag(flag)) return;
        setStoryFlag(flag);
        spawnMessageTrigger(kMessageTriggerAtOnce, message);
        return;
    default: return;
    }
}
// the guards of a room, its door locked until they are down: on entry the n guards of the list (unless the room's
// flag is set); then the flag set -> the door unlocked; else all n down -> the flag
void guardRoom(uint32_t flag, uint32_t list, uint32_t n) {
    RoomScriptState& room = roomScriptState();
    switch (room.handlerStep) {
    case 0:
        room.handlerStep = 1;
        if (!storyFlag(flag)) {
            spawnRecords(list, n);
            setStoryFlag(kFlagDoorLock);
        }
        break;
    case 1: break;
    default: return;
    }
    if (storyFlag(flag)) {
        clearStoryFlag(kFlagDoorLock);
        return;
    }
    if (roomActorsDown(n)) setStoryFlag(flag);
}
// a pair of guards on entry (the door locked); both down -> `done`
template <class Done> void guardPair(uint32_t list, uint32_t first, Done done) {
    RoomScriptState& room = roomScriptState();
    switch (room.handlerStep) {
    case 0:
        room.handlerStep = 1;
        spawnRecords(list, 2);
        setStoryFlag(kFlagDoorLock);
        break;
    case 1: break;
    default: return;
    }
    if (!roomActorAlive(first) && !roomActorAlive(first + 1)) done();
}

} // namespace

bool st41St44RoomHandler(RoutineId fn, bool run) {
    RoomScriptState& room = roomScriptState();
    switch (fn) {
    // ---- ST41 (case 0x589d80, table 0x8cc900) -------------------------------------------------------------------
    case RoutineId::St41Sub0RoomHandler: // sub 0: the door; 0x14 ticks after entry, once (flag 0x590), the message 0x28
        if (!run || roomDoorScript()) return true;
        delayedMessage(0x14, kFlagSt41HintA, 0x28);
        return true;
    case RoutineId::St41Sub1RoomHandler: // sub 1: the same after 0xa ticks (flag 0x591, message 0x29)
        if (!run || roomDoorScript()) return true;
        delayedMessage(0xa, kFlagSt41HintB, 0x29);
        return true;
    case RoutineId::St41Sub2RoomHandler:
        // sub 2: the door; the player past x 0x600 -> once (flag 0x592) the message 0x2a
        if (!run || roomDoorScript()) return true;
        if (room.handlerStep == 0 && player().pos.x.whole < 0x600) {
            room.handlerStep = 1;
            if (!storyFlag(kFlagSt41HintC)) {
                setStoryFlag(kFlagSt41HintC);
                spawnMessageTrigger(kMessageTriggerAtOnce, 0x2a);
            }
        }
        return true;
    case RoutineId::St41Sub3RoomHandler:
        // sub 3: the door; the guards 0x8cc9a8 x 3 until flag 0x682 (flag 0x680 gates the spawn)
        if (!run || roomDoorScript()) return true;
        switch (room.handlerStep) {
        case 0:
            room.handlerStep = 1;
            if (!storyFlag(kFlagGuardsA)) {
                spawnRecords(stateAddress(TableId::St41St44RoomsSt41GuardsB), 3);
                setStoryFlag(kFlagDoorLock);
            }
            break;
        case 1: break;
        default: return true;
        }
        if (storyFlag(kFlagGuardsC)) {
            clearStoryFlag(kFlagDoorLock);
            return true;
        }
        if (roomActorsDown(3)) setStoryFlag(kFlagGuardsC);
        return true;
    case RoutineId::St41Sub5RoomHandler: // sub 5: the door; the guards 0x8cc940 x 3 until flag 0x680
        if (!run || roomDoorScript()) return true;
        guardRoom(kFlagGuardsA, stateAddress(TableId::St41St44RoomsSt41GuardsA), 3);
        return true;
    case RoutineId::St41Sub7RoomHandler: // sub 7: the door; on entry the lock 0x713 = !flag 0x153
        if (!run || roomDoorScript()) return true;
        if (room.handlerStep == 0) {
            room.handlerStep = 1;
            if (storyFlag(kFlagGateLine)) clearStoryFlag(kFlagSt41Lock);
            else setStoryFlag(kFlagSt41Lock);
        }
        return true;
    case RoutineId::St41Sub12RoomHandler:
        // sub 12: the door; the pair 0x8cc980; the room actors 1 / 2 both down -> done, the door unlocked
        if (!run || roomDoorScript()) return true;
        guardPair(stateAddress(TableId::St41St44RoomsSt41Pair), 1, [&room] {
            room.handlerStep = 2;
            clearStoryFlag(kFlagDoorLock);
        });
        return true;
    // ---- ST42 (case 0x58a1a0, table 0x8cca08) -------------------------------------------------------------------
    case RoutineId::St42Sub1RoomHandler: // sub 1: the door; the guards 0x8cca48 x 2 until flag 0x680
        if (!run || roomDoorScript()) return true;
        guardRoom(kFlagGuardsA, stateAddress(TableId::St41St44RoomsSt42GuardsA), 2);
        return true;
    case RoutineId::St42Sub4RoomHandler: // sub 4: the door; the guards 0x8ccab0 x 4 until flag 0x681
        if (!run || roomDoorScript()) return true;
        guardRoom(kFlagGuardsB, stateAddress(TableId::St41St44RoomsSt42GuardsB), 4);
        return true;
    case RoutineId::St42Sub6RoomHandler: // sub 6: the door; on entry, without flag 0x59d, flag 0x684
        if (!run || roomDoorScript()) return true;
        if (room.handlerStep == 0) {
            room.handlerStep = 1;
            if (!storyFlag(kFlagSt42PairDone)) setStoryFlag(kFlagPairDown);
        }
        return true;
    case RoutineId::St42Sub7RoomHandler:
        // sub 7: the door; the pair 0x8cca88; both down -> (the step stays 1) flag 0x59d, flags 0x684 / 0x711
        // cleared (every tick from then on)
        if (!run || roomDoorScript()) return true;
        guardPair(stateAddress(TableId::St41St44RoomsSt42Pair), 0, [&room] {
            room.handlerStep = 1;
            setStoryFlag(kFlagSt42PairDone);
            clearStoryFlag(kFlagPairDown);
            clearStoryFlag(kFlagDoorLock);
        });
        return true;
    case RoutineId::St42Sub8RoomHandler: // sub 8: the door; every tick the lock 0x714 = !flag 0x153
        if (!run || roomDoorScript()) return true;
        if (storyFlag(kFlagGateLine)) clearStoryFlag(kFlagSt42Lock);
        else setStoryFlag(kFlagSt42Lock);
        return true;
    case RoutineId::St42Sub10RoomHandler: // sub 10: the door; the pair 0x8ccb50; both down -> the door unlocked
        if (!run || roomDoorScript()) return true;
        guardPair(stateAddress(TableId::St41St44RoomsSt42PairB), 0, [&room] {
            room.handlerStep = 1;
            clearStoryFlag(kFlagDoorLock);
        });
        return true;
    case RoutineId::St42Sub14RoomHandler:
        // sub 14: the door; with flag 0x3e1 and without 0x683 the guards 0x8ccb00 x 4 until flag 0x683
        if (!run || roomDoorScript()) return true;
        switch (room.handlerStep) {
        case 0:
            if (!storyFlag(kFlagGuardsD) && storyFlag(kFlagSt42GuardsGate)) {
                room.handlerStep = 1;
                spawnRecords(stateAddress(TableId::St41St44RoomsSt42GuardsC), 4);
                setStoryFlag(kFlagDoorLock);
            }
            break;
        case 1: break;
        default: return true;
        }
        if (storyFlag(kFlagGuardsD)) {
            clearStoryFlag(kFlagDoorLock);
            return true;
        }
        if (roomActorsDown(4)) setStoryFlag(kFlagGuardsD);
        return true;
    // ---- ST44 (case 0x58a6f0, table 0x8ccc14) -------------------------------------------------------------------
    case RoutineId::St44Sub2RoomHandler:
        // sub 2: the door; first time (no flag 0x172): flags 0x155 / 0x710, the script slot's group 0 and the
        // countdown message 0x10 (0x3c ticks), step 2; flag 0x172 -> the message 0x11 at once, the flags cleared, done
        if (!run || roomDoorScript()) return true;
        switch (room.handlerStep) {
        case 0:
            ++room.handlerStep;
            if (!storyFlag(kFlagSt44Done)) {
                room.handlerStep = 2;
                setStoryFlag(kFlagSt44Gate);
                setStoryFlag(kFlagSt44Lock);
                encounterSpawnGroupScriptSlot(0); // exe 0x5e66b0
                if (ControllerActor* trigger = spawnMessageTrigger(kTriggerCountdown, 0x10))
                    classState<MessageTriggerFields>(*trigger).params[0] = 0x3c;
            }
            [[fallthrough]];
        case 1:
            if (storyFlag(kFlagSt44Done)) {
                room.handlerStep = kHandlerDone;
                clearStoryFlag(kFlagSt44Lock);
            }
            return true;
        case 2:
            if (storyFlag(kFlagSt44Done)) {
                room.handlerStep = kHandlerDone;
                spawnMessageTrigger(kMessageTriggerAtOnce, 0x11);
                clearStoryFlag(kFlagSt44Gate);
                clearStoryFlag(kFlagSt44Lock);
            }
            return true;
        default: return true;
        }
    default: return false;
    }
}

} // namespace od::port
