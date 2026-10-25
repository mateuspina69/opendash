// The ice ruin's rooms in the room scripts (see ice_ruin_rooms.hpp): the handlers [table + sub * 4] of the room
// scripts' stage cases ST2D (0x584530, table 0x8cab00), ST2E (0x584950, table 0x8cab50) and ST2F (0x584bd0, table
// 0x8cab98). Each keeps its step in the room script's handler byte (game/room_scripts RoomScriptState::handlerStep),
// its second byte / word a countdown. One case per exe routine (addresses in the comments).
#include "content/yosyonke/ice_ruin_rooms.hpp"

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_sweeps.hpp"
#include "game/actor_state.hpp"
#include "game/audio_logic.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/game_state.hpp"
#include "game/message_triggers.hpp"
#include "game/player/player_state.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/spawn.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

constexpr uint8_t kHandlerOver = 0xff;
constexpr uint8_t kTriggerCountdown = 0xe; // the message trigger's countdown variant
constexpr int8_t kTriggerLastStory = 0x10;
constexpr int16_t kHintCountdown = 0x1e, kLongCountdown = 0x258;
constexpr uint32_t kMusicFadeMask = 4; // the music channel the deep rooms dip
constexpr int16_t kMusicDeepDip = 0x111, kMusicRestoreStep = 0x222;

// The handler's countdown word: the room script's third and fourth bytes as an s16
int16_t roomCountdown() {
    const RoomScriptState& r = roomScriptState();
    return int16_t(uint16_t(r.handlerByte2 | r.handlerByte3 << 8));
}
void setRoomCountdown(int16_t ticks) {
    RoomScriptState& r = roomScriptState();
    r.handlerByte2 = uint8_t(uint16_t(ticks));
    r.handlerByte3 = uint8_t(uint16_t(ticks) >> 8);
}

void musicDip(int16_t step) { soundFadeDip(kMusicFadeMask, step, 0, 0); } // exe 0x5d5830(4, step, 0, 0)
void musicBack() { soundFadeRestore(kMusicFadeMask, kMusicRestoreStep, 0); } // exe 0x5d58a0(4, 0x222, 0)
// exe 0x5852c0(variant, message): a message trigger of the variant showing the message, until story 0x10, setting no
// flag; nullptr when the pool is full
ControllerActor* messageTrigger(uint8_t variant, int16_t message) {
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
// ... shown at once
void messageNow(int16_t message) { messageTrigger(kMessageTriggerAtOnce, message); }
// ... after a countdown
void messageAfter(int16_t message, int16_t ticks) {
    if (ControllerActor* c = messageTrigger(kTriggerCountdown, message)) classState<MessageTriggerFields>(*c).params[0] = ticks;
}
// a message shown at once, the first time only (its flag)
void messageOnce(uint32_t flag, int16_t message) {
    if (storyFlag(flag)) return;
    setStoryFlag(flag);
    messageNow(message);
}

// ---- ST2D ---------------------------------------------------------------------------------------------------------
constexpr uint32_t kFlagSt2dVisited = 0x784;
constexpr uint32_t kFlagSt2dCleared = 0x3ef; // the ruin's second groups (the hints stop)
constexpr uint32_t kFlagShrinkFirst = 0x72, kFlagShrinkSecond = 0x73;
constexpr uint8_t kShrinkScript = 0x48;
enum class EntranceStep : uint8_t { Start = 0, Hint = 1, WaitNorth = 2, Drop = 3, WaitNorthAgain = 4 };
constexpr int16_t kEntranceNorthZ = -0xc00, kEntranceDropY = -0x200;
constexpr uint8_t kEntranceDropTicks = 0xf0;

// ---- ST2E ---------------------------------------------------------------------------------------------------------
constexpr uint32_t kFlagSt2eCleared = 0x3f0;

// ---- ST2F ---------------------------------------------------------------------------------------------------------
// sub 1: the pens of the herded creatures (pool 0x20 type 0x34 sub 5), their flags (set: penned) and the doors they
// lock until penned
constexpr uint32_t kHerdCount = 3;
constexpr uint8_t kHerdType = 0x34, kHerdSubtype = 5, kHerdRow = 5;
const DataTable<ScalarTable<uint16_t, kHerdCount>> kPenFlags{TableId::IceRuinRoomsPenFlags};
// the creatures' starting places (s16 x / y / z / yaw)
struct HerdStart : DataRecord {
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;
    int16_t yaw = 0;
    static HerdStart load(const TableReader& r) {
        HerdStart rec;
        rec.x = r.s16(0);
        rec.y = r.s16(2);
        rec.z = r.s16(4);
        rec.yaw = r.s16(6);
        return rec;
    }
};
const DataTable<RecordTable<HerdStart, kHerdCount, 8>> kHerdStarts{TableId::IceRuinRoomsHerdStarts};
constexpr uint32_t kFlagPenA = 0x275, kFlagPenB = 0x276, kFlagPenC = 0x277;
constexpr uint32_t kFlagPenLockA = 0x711, kFlagPenLockB = 0x712, kFlagPenLockC = 0x713;
// sub 6: the catchable critters' room
constexpr uint32_t kFlagCritterRoomLock = 0x714, kFlagCritterSceneSeen = 0x74;
constexpr uint32_t kFlagCritterCaughtA = 0x3f1, kFlagCritterCaughtB = 0x3f2, kFlagCritterCaughtC = 0x3f3;
constexpr uint32_t kFlagCritterHintReady = 0x274;
constexpr uint8_t kCritterSceneScript = 0x49;
constexpr uint32_t kCritterCount = 4;
// sub 7 / 8: the tutorial of class 0x4d6b40 and the room after it
constexpr uint32_t kFlagTutorialGate = 0x683, kFlagTutorialDone = 0x75, kFlagTutorialRoomDone = 0x76;
constexpr uint32_t kFlagTutorialLockA = 0x716, kFlagTutorialLockB = 0x717;
constexpr uint32_t kFlagRewardTaken = 0x3d3, kFlagRewardLock = 0x715;
constexpr uint8_t kTutorialScript = 0x4a, kTutorialSceneScript = 0x4b;
constexpr uint8_t kSub5RecordLastStory = 0x10;
constexpr int16_t kSub7NorthZ = -0x900;
constexpr uint32_t kRewardEventParam = 0x33;
constexpr uint16_t kRewardWindow = 1, kRewardMessage = 0x1c;

bool eventRunning() { return eventState().active != 0; }

// 0x584550: ST2D sub 0
void st2dEntrance(RoomScriptState& room) {
    const Player& p = player();
    switch (EntranceStep(room.handlerStep)) {
    case EntranceStep::Start:
        ++room.handlerStep;
        setStoryFlag(kFlagSt2dVisited);
        if (!storyFlag(0x251)) {
            setStoryFlag(0x251);
            messageAfter(0x1f, kHintCountdown);
        }
        [[fallthrough]];
    case EntranceStep::Hint:
        if (!storyFlag(0x252)) {
            ++room.handlerStep;
            setStoryFlag(0x252);
        }
        return;
    case EntranceStep::WaitNorth:
        if (p.pos.z.whole < kEntranceNorthZ) {
            room.handlerStep = uint8_t(EntranceStep::Drop);
            room.handlerByte1 = kEntranceDropTicks;
        }
        return;
    case EntranceStep::Drop:
        if (p.pos.y.whole > kEntranceDropY) {
            room.handlerStep = kHandlerOver;
            return;
        }
        --room.handlerByte1;
        if (room.handlerByte1 == 0) room.handlerStep = uint8_t(EntranceStep::WaitNorthAgain);
        return;
    case EntranceStep::WaitNorthAgain:
        if (p.pos.z.whole < kEntranceNorthZ) {
            messageNow(0x20);
            ++room.handlerStep;
        }
        return;
    default: return;
    }
}
// 0x5846c0: ST2D sub 5 -- the shrinking block's script once on each of its flags, then the door
void st2dShrinkRoom(RoomScriptState& room) {
    switch (room.handlerStep) {
    case 0:
        ++room.handlerStep;
        if (storyFlag(kFlagShrinkFirst)) room.handlerStep = 2;
        break;
    case 1:
        if (storyFlag(kFlagShrinkFirst) && !eventRunning()) {
            ++room.handlerStep;
            scriptRun(kShrinkScript);
        }
        break;
    case 2:
        ++room.handlerStep;
        if (storyFlag(kFlagShrinkSecond)) room.handlerStep = 4;
        break;
    case 3:
        if (storyFlag(kFlagShrinkSecond) && !eventRunning()) {
            ++room.handlerStep;
            scriptRun(kShrinkScript);
        }
        break;
    default: break;
    }
    roomDoorScript();
}
// 0x584780: ST2D sub 6 -- before the ruin is cleared, the player's boxes -> the hints once each
void st2dHintBoxes() {
    if (storyFlag(kFlagSt2dCleared)) return;
    const Player& p = player();
    const int16_t x = p.pos.x.whole, y = p.pos.y.whole, z = p.pos.z.whole;
    if (!storyFlag(0x254) && x < 0x200 && x > -0xa00 && z < 0x800 && z > 0x400) {
        setStoryFlag(0x254);
        messageNow(0x22);
    }
    if (!storyFlag(0x255) && y < -0x400 && x < -0x600 && x > -0xa00 && z < 0x400) {
        setStoryFlag(0x255);
        messageNow(0x23);
    }
    if (!storyFlag(0x256) && y == 0 && x < -0x600 && x > -0xa00 && z < 0x400) {
        setStoryFlag(0x256);
        messageNow(0x24);
    }
}
// 0x584c40: ST2F sub 1 -- the three herded creatures placed (or penned already), the pens' doors locked; the pens
// filled unlock them, the hints by how many; with none the long countdown's hint; a creature gone -> its hint
void st2fPens(RoomScriptState& room) {
    switch (room.handlerStep) {
    case 0: {
        for (uint32_t i = 0; i < kHerdCount; ++i) {
            const uint32_t slot = allocActor20(); // exe 0x5e1160
            if (!slot) continue;
            room.actors[i] = slot;
            AnimatedActor& a = animatedActorAt(slot);
            a.loopFlags |= kActorSweepGroup2;
            a.flags = kActorLiveDrawn;
            a.type = kHerdType;
            a.subtype = kHerdSubtype;
            if (!storyFlag(kPenFlags[i])) {
                a.variant = 0;
                a.spawnByte = kHerdRow;
                a.params[2] = 0;
                const HerdStart start = kHerdStarts[i];
                a.pos.x.whole = start.x;
                a.pos.y.whole = start.y;
                a.pos.z.whole = start.z;
                a.rotation.y = start.yaw;
            } else {
                a.variant = 1;
                a.params[2] = uint8_t(i);
            }
        }
        setStoryFlag(kFlagPenLockA);
        setStoryFlag(kFlagPenLockB);
        setStoryFlag(kFlagPenLockC);
        room.handlerStep = 1;
        setRoomCountdown(kLongCountdown);
        [[fallthrough]];
    }
    case 1: {
        int n = 0;
        if (storyFlag(kFlagPenA)) {
            clearStoryFlag(kFlagPenLockA);
            n = 1;
        }
        if (storyFlag(kFlagPenB)) {
            clearStoryFlag(kFlagPenLockB);
            ++n;
        }
        if (storyFlag(kFlagPenC)) {
            clearStoryFlag(kFlagPenLockC);
            ++n;
        }
        if (n == 1) messageOnce(0x267, 0x13);
        else if (n == 2) messageOnce(0x268, 0x14);
        else if (n == 3) messageOnce(0x269, 0x15);
        if (!storyFlag(0x265) && n == 0) {
            int16_t t = roomCountdown();
            if (t != 0) {
                --t;
                if (t == 0) {
                    setStoryFlag(0x265);
                    messageNow(0x11);
                }
                setRoomCountdown(t);
            }
        }
        if (!storyFlag(0x266)) {
            for (uint32_t i = 0; i < kHerdCount; ++i) {
                if (actorHeaderAt(room.actors[i]).flags & kActorLive) continue;
                setStoryFlag(0x266);
                messageAfter(0x12, kHintCountdown);
            }
        }
        break;
    }
    default: break;
    }
    roomDoorScript();
}
// 0x584f30: ST2F sub 6 (after the door test) -- the music dipped, the room locked; the critters' scene the first
// time; then the critters and the hint; the hint of the hint flag; the room unlocked once all three are caught
void st2fCritterRoom(RoomScriptState& room) {
    switch (room.handlerStep) {
    case 0:
        musicDip(kMusicDeepDip);
        setStoryFlag(kFlagCritterRoomLock);
        room.handlerStep = 2;
        if (storyFlag(kFlagCritterSceneSeen)) return;
        room.handlerStep = 1;
        actorsSweep(kSweepAnimatedGroup2 | kSweepProjectiles | kSweepStaticsGroup2 | kSweepEffectsGroup2, kSweepFree);
        setStoryFlag(kFlagCritterSceneSeen);
        scriptRun(kCritterSceneScript);
        return;
    case 1:
        if (!scriptState().inUse()) ++room.handlerStep;
        return;
    case 2:
        ++room.handlerStep;
        spawnRecords(stateAddress(TableId::IceRuinRoomsCritterRecords), kCritterCount);
        messageOnce(0x26a, 0x16);
        return;
    case 3:
        if (!storyFlag(0x264) && storyFlag(kFlagCritterHintReady)) {
            setStoryFlag(0x264);
            messageNow(0x22);
            messageAfter(0x23, kLongCountdown);
        }
        if (storyFlag(kFlagCritterCaughtA) && storyFlag(kFlagCritterCaughtB) && storyFlag(kFlagCritterCaughtC))
            clearStoryFlag(kFlagCritterRoomLock);
        return;
    default: return;
    }
}
// 0x5850b0: ST2F sub 7 (after the door test) -- before the tutorial's gate: once the music back, its record and the
// hints; with the gate: the tutorial (script 0x4a), then its scene (script 0x4b) once it is done
void st2fTutorialRoom(RoomScriptState& room) {
    if (!storyFlag(kFlagTutorialGate)) {
        if (storyFlag(kFlagTutorialRoomDone)) return;
        if (room.handlerStep == 0) {
            room.handlerStep = 1;
            musicBack();
            spawnRecords(stateAddress(TableId::IceRuinRoomsSub7Record), 1);
            messageOnce(0x26c, 0x19);
        }
        if (!storyFlag(0x26d) && player().pos.z.whole < kSub7NorthZ) {
            setStoryFlag(0x26d);
            messageNow(0x1b);
        }
        return;
    }
    switch (room.handlerStep) {
    case 0:
        if (storyFlag(kFlagTutorialDone)) {
            room.handlerStep = 2;
            return;
        }
        ++room.handlerStep;
        setStoryFlag(kFlagTutorialLockA);
        setStoryFlag(kFlagTutorialLockB);
        scriptRun(kTutorialScript);
        return;
    case 1:
        if (!storyFlag(kFlagTutorialDone)) return;
        ++room.handlerStep;
        clearStoryFlag(kFlagTutorialLockA);
        clearStoryFlag(kFlagTutorialLockB);
        scriptRun(kTutorialSceneScript);
        return;
    default: return;
    }
}
// 0x585200: ST2F sub 8 (after the door test) -- before the gate its record; with the gate and no event running the
// reward's event at the player, its flags
void st2fRewardRoom(RoomScriptState& room) {
    switch (room.handlerStep) {
    case 0:
        if (storyFlag(kFlagTutorialGate) || storyFlag(kFlagTutorialRoomDone)) return;
        ++room.handlerStep;
        spawnRecords(stateAddress(TableId::IceRuinRoomsSub8Record), 1);
        return;
    case 1:
        if (!storyFlag(kFlagTutorialGate) || eventRunning()) return;
        ++room.handlerStep;
        eventStartAtActor(player(), kRewardEventParam, kRewardWindow, kRewardMessage); // exe 0x543af0
        setStoryFlag(kFlagRewardLock);
        setStoryFlag(kFlagRewardTaken);
        setStoryFlag(kFlagTutorialRoomDone);
        return;
    default: return;
    }
}

} // namespace

bool iceRuinRoomHandler(RoutineId fn, bool run) {
    RoomScriptState& room = roomScriptState();
    switch (fn) {
    // ---- ST2D -------------------------------------------------------------------------------------------------
    case RoutineId::St2DSub0RoomHandler: // sub 0: the door first; the hints of the entrance
        if (!run || roomDoorScript()) return true;
        st2dEntrance(room);
        return true;
    case RoutineId::St2DSub2RoomHandler:
        // sub 2: the door first; before the ruin is cleared, standing on an actor -> its hint once
        if (!run || roomDoorScript()) return true;
        if (!storyFlag(kFlagSt2dCleared) && player().onPlatform != 0) messageOnce(0x253, 0x21);
        return true;
    case RoutineId::St2DSub5RoomHandler: // sub 5
        if (!run) return true;
        st2dShrinkRoom(room);
        return true;
    case RoutineId::St2DSub6RoomHandler: // sub 6: the door first
        if (!run || roomDoorScript()) return true;
        st2dHintBoxes();
        return true;
    case RoutineId::St2DSub7RoomHandler: // sub 7: the door first; before the ruin is cleared its hint once
        if (!run || roomDoorScript()) return true;
        if (!storyFlag(kFlagSt2dCleared)) messageOnce(0x257, 0x25);
        return true;
    // ---- ST2E -------------------------------------------------------------------------------------------------
    case RoutineId::St2ESub2RoomHandler:
        // sub 2: the door first; before the ruin is cleared: its hint once; on the floor (y 0) another once
        if (!run || roomDoorScript()) return true;
        if (storyFlag(kFlagSt2eCleared)) return true;
        messageOnce(0x259, 0x14);
        if (player().pos.y.whole == 0) messageOnce(0x25a, 0x15);
        return true;
    case RoutineId::St2ESub3RoomHandler: // sub 3: the door first; before the ruin is cleared its hint once
        if (!run || roomDoorScript()) return true;
        if (!storyFlag(kFlagSt2eCleared)) messageOnce(0x25b, 0x16);
        return true;
    case RoutineId::St2eRoom4Handler: {
        // sub 4: the door first; before the ruin is cleared: its hint once; inside x / z (-0x600, 0x800)
        // x (-0x600, 0x600) another once
        if (!run || roomDoorScript()) return true;
        if (storyFlag(kFlagSt2eCleared)) return true;
        const Player& p = player();
        const int16_t x = p.pos.x.whole, z = p.pos.z.whole;
        messageOnce(0x25c, 0x17);
        if (x > -0x600 && x < 0x800 && z > -0x600 && z < 0x600) messageOnce(0x25d, 0x19);
        return true;
    }
    case RoutineId::St2ESub7RoomHandler: // sub 7: the door first; before the ruin is cleared its hint once
        if (!run || roomDoorScript()) return true;
        if (!storyFlag(kFlagSt2eCleared)) messageOnce(0x25e, 0x1a);
        return true;
    // ---- ST2F -------------------------------------------------------------------------------------------------
    case RoutineId::St2FSub0RoomHandler: // sub 0: the door first; its hint once
        if (!run || roomDoorScript()) return true;
        messageOnce(0x26b, 0x10);
        return true;
    case RoutineId::St2FSub1RoomHandler: // sub 1
        if (!run) return true;
        st2fPens(room);
        return true;
    case RoutineId::St2FSub5RoomHandler:
        // sub 5: the door first; once the music back and, up to story 0x10 before the reward, its record
        if (!run || roomDoorScript()) return true;
        if (room.handlerStep != 0) return true;
        room.handlerStep = 1;
        musicBack();
        if (gameState().story <= kSub5RecordLastStory && !storyFlag(kFlagRewardTaken)) spawnRecords(stateAddress(TableId::IceRuinRoomsSub5Record), 1);
        return true;
    case RoutineId::St2FSub6RoomHandler: // sub 6: the door first
        if (!run || roomDoorScript()) return true;
        st2fCritterRoom(room);
        return true;
    case RoutineId::St2FSub7RoomHandler: // sub 7: the door first
        if (!run || roomDoorScript()) return true;
        st2fTutorialRoom(room);
        return true;
    case RoutineId::St2FSub8RoomHandler: // sub 8: the door first
        if (!run || roomDoorScript()) return true;
        st2fRewardRoom(room);
        return true;
    default: return false;
    }
}

} // namespace od::port
