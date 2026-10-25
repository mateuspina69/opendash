// The room handlers of the fire ruin (story 13; ST26 case 0x582330 table 0x8c93ac, ST27 0x582870 / 0x8c94b4, ST28
// 0x583030 / 0x8c9740), its one-shot setups after it (story 14), and the ruin ST33 / ST34. One case per exe handler
// (its address in the case); each keeps its step in the room handler's byte 0.
#include "content/saul_kada/fire_ruin_rooms.hpp"

#include "content/saul_kada/breath_puff.hpp"
#include "content/saul_kada/mission_rooms.hpp"
#include "content/saul_kada/st28_lava_boss.hpp"

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/encounter.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/game_state.hpp"
#include "game/message.hpp"
#include "game/player/player_state.hpp"
#include "game/random.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/spawn.hpp"
#include "game/story_flags.hpp"
#include "game/transition_state.hpp"

namespace od::port {

namespace {

constexpr uint32_t kFlagDoor = 0x700, kFlagDoorArmed = 0x701;
constexpr uint32_t kMessageBank = 0x73c000;
constexpr uint32_t kMusicFades = 0xf;
constexpr int16_t kMusicFadeStep = 0xfff;
constexpr uint8_t kBossWindow = 2, kEventWindow = 4;
constexpr uint8_t kStepEscapeMessage = 1; // the escape door's message window param
constexpr uint16_t kEscapeEndSound = 0x3c;
constexpr uint8_t kTransitionToRoom = 1;   // the transition request's state the escape's end sets

RoomScriptState& room() { return roomScriptState(); }
uint8_t step() { return room().handlerStep; }
void setStep(uint8_t s) { room().handlerStep = s; }
void nextStep() { room().handlerStep += 1u; }
int16_t playerZ() { return player().pos.z.whole; }
bool eventRunning() { return eventState().active != 0; }
void openWall(uint32_t column, uint32_t row) { switchGridCellVariant(column, row, 1); } // exe 0x58d5f0
template <class SetUp> bool once(bool run, SetUp setUp) {
    if (!run || step() != 0) return true;
    setUp();
    nextStep();
    return true;
}
// a room whose records come once, then the door test
template <class SetUp> bool onceThenDoor(bool run, SetUp setUp) {
    if (!run) return true;
    if (step() == 0) {
        setUp();
        nextStep();
    }
    roomDoorScript(); // exe 0x582490
    return true;
}
// a script run as the room's first visit: the door's flags cleared
void runVisitScript(uint8_t id) {
    scriptRun(id);
    clearStoryFlag(kFlagDoor);
    clearStoryFlag(kFlagDoorArmed);
}

// ---- ST26 ------------------------------------------------------------------------------------------------------------
// exe 0x582380 (sub 0): its record, flag 0x711; past z -0x700 script 0x63 once (flag 0x58); the key (flag 0x3ed) -> 0x711
// cleared
void st26Entry() {
    switch (step()) {
    case 0:
        spawnRecords(stateAddress(TableId::FireRuinRoomsSt26EntrySpawns), 1);
        setStoryFlag(0x711);
        setStep(storyFlag(0x58) ? 2 : 1);
        break;
    case 1:
        if (playerZ() > -0x700) {
            scriptRun(0x63);
            setStoryFlag(0x58);
            setStep(2);
        }
        break;
    case 2:
        if (storyFlag(0x3ed)) {
            clearStoryFlag(0x711);
            setStep(3);
        }
        break;
    default: break;
    }
    roomDoorScript();
}
// exe 0x582420 (sub 1): its record, flag 0x712 until flag 0x42
void st26Hall() {
    if (step() == 0) {
        spawnRecords(stateAddress(TableId::FireRuinRoomsSt26HallSpawns), 1);
        setStoryFlag(0x712);
        setStep(1);
    } else if (step() == 1 && storyFlag(0x42)) {
        clearStoryFlag(0x712);
        setStep(2);
    }
    roomDoorScript();
}
// exe 0x5824b0 (sub 3): flags 0x719 / 0x710 on; off (and flag 0x47) once flag 0x42 is set and enemies 0x12..0x17 are
// down
void st26Guards() {
    if (step() == 0) {
        setStoryFlag(0x719);
        setStoryFlag(0x710);
        setStep(1);
    } else if (step() == 1) {
        if (!storyFlag(0x42) || storyFlag(0x47)) {
            clearStoryFlag(0x719);
            clearStoryFlag(0x710);
            setStep(2);
        } else {
            bool all = true;
            for (uint32_t id = 0x12; id <= 0x17 && all; ++id) all = enemyDefeated(id);
            if (all) {
                clearStoryFlag(0x719);
                clearStoryFlag(0x710);
                setStoryFlag(0x47);
                setStep(2);
            }
        }
    }
    roomDoorScript();
}

// ---- ST27 ------------------------------------------------------------------------------------------------------------
// exe 0x582970 (sub 2): its records (with the two flags' extras), flag 0x714; both flags -> the last record, 0x714
// cleared
void st27Crossing() {
    if (step() == 0) {
        spawnRecords(stateAddress(TableId::FireRuinRoomsSt27CrossingSpawns), 1);
        if (storyFlag(0x44)) spawnRecords(stateAddress(TableId::FireRuinRoomsSt27CrossingFirstBridgeSpawns), 1);
        if (storyFlag(0x45)) spawnRecords(stateAddress(TableId::FireRuinRoomsSt27CrossingSecondBridgeSpawns), 1);
        setStoryFlag(0x714);
        setStep(1);
    } else if (step() == 1 && storyFlag(0x44) && storyFlag(0x45)) {
        spawnRecords(stateAddress(TableId::FireRuinRoomsSt27CrossingMoreSpawns), 1);
        clearStoryFlag(0x714);
        setStep(2);
    }
    roomDoorScript();
}
// a lava ember's point (exe data: s16 x, y, z and its yaw)
struct EmberPoint : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    int16_t x = 0; // +0x0
    int16_t y = 0; // +0x2
    int16_t z = 0; // +0x4
    int16_t yaw = 0; // +0x6
    template <class R> static EmberPoint load(const R& r) {
        EmberPoint v;
        v.x = r.template read<int16_t>(0x0);
        v.y = r.template read<int16_t>(0x2);
        v.z = r.template read<int16_t>(0x4);
        v.yaw = r.template read<int16_t>(0x6);
        return v;
    }
};
const DataTable<ScalarTable<int8_t, 4>> emberAngles{TableId::FireRuinRoomsEmberAngles};
// exe 0x582ed0(point, pitch, yaw add, frame): a lava ember (the stage's breath puff) at the point
void lavaEmber(uint32_t at, int16_t pitch, int16_t yawAdd, uint8_t frame) {
    const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!slot) return;
    const EmberPoint point = dataRecord<EmberPoint>(at, EmberPoint::kBytes);
    ProjectileActor& ember = projectileActorAt(slot);
    BreathPuffParams params(projectileActorAt(slot));
    ember.loopFlags |= kActorSweepGroup2;
    ember.flags = kActorLive;
    ember.type = kBreathPuffType;
    ember.variant = 0;
    ember.pos.x.whole = point.x;
    ember.pos.y.whole = point.y;
    ember.pos.z.whole = point.z;
    ember.rotation.x = pitch;
    ember.rotation.z = 0;
    params.damage = 8;
    params.life = 0x12;
    ember.rotation.y = int16_t(uint16_t(uint16_t(point.yaw) + uint16_t(yawAdd)));
    params.frame = frame;
    ember.velocity.z = int16_t(0xfd40);
    params.spin = 0;
    params.size = 0x70;
}
// exe 0x582a40 (sub 4, until flag 0x57): the broken wall (0x3f, 0x44) opens the passage (0x3f, 0x40), flag 0x57; else
// every other tick two lava embers at random angles; past z 0 its record once (flag 0x59)
void st27LavaPassage() {
    if (step() == 0 && !storyFlag(0x57)) {
        if (gridCellSegmentBroken(0x3f, 0x44)) { // exe 0x58d570
            openWall(0x3f, 0x40);
            setStoryFlag(0x57);
            nextStep();
        } else {
            const int16_t pitch = emberAngles[rngStep() & 3];
            const int16_t yaw = emberAngles[rngStep() & 3];
            const uint16_t frame = gameState().frame;
            if (frame & 1) {
                lavaEmber(stateAddress(TableId::FireRuinRoomsEmberPointA), pitch, yaw, uint8_t((frame >> 1) & 1));
                lavaEmber(stateAddress(TableId::FireRuinRoomsEmberPointB), pitch, yaw, uint8_t((uint8_t(gameState().frame) >> 1) & 1));
            }
        }
        if (!storyFlag(0x59) && playerZ() > 0) {
            spawnRecords(stateAddress(TableId::FireRuinRoomsSt27LavaPassageSpawns), 1);
            setStoryFlag(0x59);
        }
    }
    roomDoorScript();
}
// exe 0x582bf0 (sub 8, the fire ruin's entrance): the first visit script 0x40 (flag 0x4b), its records; the timed
// escape (flag 0x681): the mission opening (exe 0x582f50, message 0x2b) and script 0x41 at flag 0x4c; without it, z <=
// 0x800 -> script 0x2e. The door during the escape: a message, then the music fades out (flag 0x682) and the escape
// ends (its flags cleared, the transition, script 0)
void st27Entrance() {
    RoomScriptState& r = room();
    bool tail = true;
    switch (step()) {
    case 0:
        if (storyFlag(0x4c)) {
            spawnRecords(stateAddress(TableId::FireRuinRoomsSt27EntranceSpawns), 3);
            setStep(3);
        } else if (!storyFlag(0x4b)) {
            runVisitScript(0x40);
            setStoryFlag(0x4b);
            setStep(1);
            tail = false;
        } else {
            spawnRecords(stateAddress(TableId::FireRuinRoomsSt27EntranceMoreSpawns), 6);
            setStep(1);
        }
        break;
    case 1:
        if (!storyFlag(0x681)) {
            if (playerZ() <= 0x800) {
                scriptRun(0x2e);
                tail = false;
            }
        } else if (missionOpening(true, 0x2b, OpeningStep::HandlerByte2, 0x20)) { // exe 0x582f50
            setStep(2);
            r.handlerByte2 = 0;
        }
        break;
    case 2:
        if (storyFlag(0x4c)) {
            scriptRun(0x41);
            setStep(3);
            tail = false;
        }
        break;
    default: break;
    }
    if (!tail) return;
    if (r.handlerByte3 == 1) {
        if (!soundDone(kMusicFades)) return; // exe 0x5d5900
        soundPlayFlatForced(kEscapeEndSound);
        clearStoryFlag(0x681);
        clearStoryFlag(0x682);
        clearStoryFlag(0x739);
        transitionRequest().state = kTransitionToRoom;
        r.handlerByte1 = 0;
        r.handlerByte3 = 0;
        scriptRun(0);
        return;
    }
    if (r.handlerByte3 != 0 || !storyFlag(kFlagDoor)) return;
    if (!storyFlag(0x681)) {
        scriptRun(0);
        return;
    }
    if (storyFlag(0x4c)) {
        clearStoryFlag(0x739);
        clearStoryFlag(0x681);
        transitionRequest().state = kTransitionToRoom;
        scriptRun(0);
        return;
    }
    if (r.handlerByte1 == 0) {
        eventStartAt(0, nullptr, kStepEscapeMessage, 0, 0x1c); // exe 0x543b40
        r.handlerByte1 += 1u;
        return;
    }
    if (eventRunning()) return;
    if (storyFlag(0x682)) {
        soundFadeOut(kMusicFades, kMusicFadeStep, 0); // exe 0x5d5780
        r.handlerByte3 = 1;
        return;
    }
    clearStoryFlag(kFlagDoor);
    r.handlerByte1 = 0;
}

// ---- ST28 ------------------------------------------------------------------------------------------------------------
// exe 0x583080 (sub 0, the boss room): its record (the sand screen with flag 0x50); with 0x50 and without 0x55 the
// fight: the music faded, its flags, the boss, the mission opening (exe 0x5835f0, message 0x15) and the boss music;
// scripts 0x46 (flag 0x53) / 0x47 (0x54, then the opening 0x17); at half the boss's hit points window 2's message 0x1f
// (flag 0x681), its in the box -> message 0x1e (0x682); flag 0x56 -> window 4 closed, message 0x1c, the fight's flags
// cleared
void st28BossRoom() {
    RoomScriptState& r = room();
    switch (step()) {
    case 0:
        if (storyFlag(0x56)) {
            spawnRecords(stateAddress(TableId::FireRuinRoomsSt28BossRoomSpawns), 1);
            setStep(5);
            break;
        }
        if (storyFlag(0x50)) encounterSuspend(2, 0); // exe 0x5e6870(2, 0)
        else spawnRecords(stateAddress(TableId::FireRuinRoomsSt28BossRoomSpawns), 1);
        if (storyFlag(0x50) && !storyFlag(0x55)) {
            soundFadeOut(kMusicFades, kMusicFadeStep, 0);
            setStoryFlag(0x718);
            setStoryFlag(0x710);
            setStoryFlag(0x711);
            setStoryFlag(0x715);
            spawnRecords(stateAddress(TableId::FireRuinRoomsSt28BossRoomMoreSpawns), 1);
            setStep(1);
        } else {
            setStep(4);
        }
        break;
    case 1:
        if (soundDone(kMusicFades) && missionOpening(true, 0x15)) { // exe 0x5835f0
            soundPlayMusicWithFade(0x3e, 0x380, 2, 1);             // exe 0x5d52f0
            setStep(2);
            r.handlerByte1 = 0;
        }
        break;
    case 2: {
        if (eventRunning()) break;
        if (storyFlag(0x53)) {
            scriptRun(0x46);
            setStep(4);
            break;
        }
        if (storyFlag(0x54)) {
            scriptRun(0x47);
            setStep(3);
            break;
        }
        const AnimatedActor& boss = animatedActorAt(r.actors[0]);
        if (!storyFlag(0x681) && boss.hitPoints < boss.maxHitPoints / 2 && windowStatus(kBossWindow) == 0) {
            windowOpen(kBossWindow, kMessageBank, 0x1f); // exe 0x557390
            setStoryFlag(0x681);
        }
        if (!storyFlag(0x682) && LavaBossBombOut(boss).inBox != 0 && windowStatus(kBossWindow) == 0) {
            windowOpen(kBossWindow, kMessageBank, 0x1e);
            setStoryFlag(0x682);
        }
        break;
    }
    case 3:
        if (missionOpening(true, 0x17)) {
            setStep(4);
            r.handlerByte1 = 0;
        }
        break;
    case 4:
        if (storyFlag(0x56) && !eventRunning()) {
            windowClose(kEventWindow, WindowClose::Force); // exe 0x5576b0
            eventStartAt(0, nullptr, 0xb, 0, 0x1c);
            setStoryFlag(0x3d2);
            soundFadeOut(kMusicFades, kMusicFadeStep, 0);
            setStoryFlag(0x6f1);
            clearStoryFlag(0x718);
            clearStoryFlag(0x710);
            clearStoryFlag(0x711);
            clearStoryFlag(0x715);
            setStep(5);
        }
        break;
    default: break;
    }
    roomDoorScript();
}
// exe 0x583390 (sub 3): the first time script 0x44 (flag 0x4f); before flag 0x50 its record and at flag 0x4e script
// 0x45 (flag 0x50, 0x717 cleared)
void st28Approach() {
    switch (step()) {
    case 0:
        if (!storyFlag(0x4f)) {
            runVisitScript(0x44);
            setStoryFlag(0x4f);
            setStep(1);
            return;
        }
        if (storyFlag(0x50)) {
            setStep(2);
            break;
        }
        spawnRecords(stateAddress(TableId::FireRuinRoomsSt28ApproachSpawns), 1);
        setStep(1);
        [[fallthrough]];
    case 1:
        if (storyFlag(0x4e)) {
            scriptRun(0x45);
            setStoryFlag(0x50);
            clearStoryFlag(kFlagDoor);
            clearStoryFlag(kFlagDoorArmed);
            clearStoryFlag(0x717);
            setStep(2);
            return;
        }
        break;
    default: break;
    }
    roomDoorScript();
}
// exe 0x583460 (sub 4): until flag 0x591 its record while z > 0 (the tracked actor 1 freed when back); its other
// record once unless flag 0x4d
void st28Lift() {
    RoomScriptState& r = room();
    if (!storyFlag(0x591)) {
        if (step() == 0) {
            if (playerZ() > 0) {
                spawnRecords(stateAddress(TableId::FireRuinRoomsSt28LiftSpawns), 1);
                setStep(1);
            }
        } else if (playerZ() <= 0) {
            actorHeaderAt(r.actors[1]).state = 2;
            setStep(0);
        }
    }
    if (r.handlerByte1 == 0) {
        if (!storyFlag(0x4d)) spawnRecords(stateAddress(TableId::FireRuinRoomsSt28LiftMoreSpawns), 1);
        r.handlerByte1 = 1;
    }
    roomDoorScript();
}
// exe 0x5834f0 (sub 5): the first time script 0x42 (flags 0x4d / 0x717 / 0x718); flag 0x4e -> script 0x43, 0x718
// cleared
void st28Depths() {
    switch (step()) {
    case 0:
        if (!storyFlag(0x4d)) {
            scriptRun(0x42);
            setStoryFlag(0x4d);
            setStoryFlag(0x717);
            setStoryFlag(0x718);
            clearStoryFlag(kFlagDoor);
            clearStoryFlag(kFlagDoorArmed);
            setStep(1);
            return;
        }
        setStep(2);
        break;
    case 1:
        if (!eventRunning() && storyFlag(0x4e)) {
            scriptRun(0x43);
            clearStoryFlag(0x718);
            setStep(2);
            return;
        }
        break;
    default: break;
    }
    roomDoorScript();
}

// ---- ST33 / ST34 -----------------------------------------------------------------------------------------------------
// exe 0x5865e0 (ST33 sub 0): flag 0x52 once 0x44 and 0x45; before the key (0x3ed) flags 0x713 / 0x712 and the first
// time script 0x2d (flag 0x48), else its records; the tracked actor 0 at animation 0x1e -> flag 0x5b1 (until 0x584);
// 0x43 -> 0x713 / 0x712 off; 0x4c -> on with 0x710 and records; 0x4a -> script 0x2f; flag 0x680 -> window 2's message
// 0x19 (flag 0x681); the key -> the flags off
void st33Entry() {
    RoomScriptState& r = room();
    const auto actorFlag = [&r] {
        const uint32_t t = r.actors[0];
        if (!storyFlag(0x584) && t && animatedActorAt(t).anim == 0x1e) setStoryFlag(0x5b1);
    };
    switch (step()) {
    case 0:
        if (!storyFlag(0x52) && storyFlag(0x44) && storyFlag(0x45)) setStoryFlag(0x52);
        if (!storyFlag(0x3ed)) {
            setStoryFlag(0x713);
            setStoryFlag(0x712);
            setStep(1);
        } else {
            spawnRecords(stateAddress(TableId::FireRuinRoomsSt33EntrySpawns), 2);
            setStep(6);
        }
        break;
    case 1:
        if (!storyFlag(0x48)) {
            scriptRun(0x2d);
            setStoryFlag(0x48);
        } else if (!storyFlag(0x4c)) {
            spawnRecords(stateAddress(TableId::FireRuinRoomsSt33EntrySecondSpawns), 5);
            spawnRecords(stateAddress(TableId::FireRuinRoomsSt33EntrySecondMoreSpawns), 1);
        }
        setStep(2);
        [[fallthrough]];
    case 2:
        actorFlag();
        if (storyFlag(0x43)) {
            clearStoryFlag(0x713);
            clearStoryFlag(0x712);
            setStep(3);
        }
        [[fallthrough]];
    case 3:
        actorFlag();
        if (storyFlag(0x4c)) {
            setStoryFlag(0x713);
            setStoryFlag(0x710);
            setStoryFlag(0x712);
            spawnRecords(stateAddress(TableId::FireRuinRoomsSt33EntryLastSpawns), 1);
            spawnRecords(stateAddress(TableId::FireRuinRoomsSt33EntryLastMoreSpawns), 1);
            spawnRecords(stateAddress(TableId::FireRuinRoomsSt33EntrySpawns), 2);
            setStep(4);
        }
        [[fallthrough]];
    case 4:
        if (!eventRunning() && storyFlag(0x4a)) {
            scriptRun(0x2f);
            setStep(5);
        }
        if (storyFlag(0x680) && !storyFlag(0x681) && windowStatus(kBossWindow) == 0) {
            windowOpen(kBossWindow, kMessageBank, 0x19);
            setStoryFlag(0x681);
        }
        [[fallthrough]];
    case 5:
        if (storyFlag(0x3ed)) {
            clearStoryFlag(0x713);
            clearStoryFlag(0x710);
            clearStoryFlag(0x712);
            setStep(6);
        }
        break;
    default: break;
    }
    roomDoorScript();
}

} // namespace

bool fireRuinRoomHandler(RoutineId fn, bool run) {
    switch (fn) {
    // ---- ST26 (story 13) -----------------------------------------------------------------------------------------
    case RoutineId::St26RoomHandler:
        if (run) st26Entry();
        return true;
    case RoutineId::St26RoomHandlerA:
        if (run) st26Hall();
        return true;
    case RoutineId::St26RoomHandlerB:
        if (run) st26Guards();
        return true;
    case RoutineId::St26Sub6RoomHandler: // sub 6: its record unless flag 0x56
        return onceThenDoor(run, [] {
            if (!storyFlag(0x56)) spawnRecords(stateAddress(TableId::FireRuinRoomsSt26Sub6Spawns), 1);
        });
    // ---- ST27 (story 13) -----------------------------------------------------------------------------------------
    case RoutineId::St27Sub0RoomHandler: // sub 0: with flag 0x56 or without 0x52 its records (by the player's side)
        return onceThenDoor(run, [] {
            if (storyFlag(0x56) || !storyFlag(0x52)) spawnRecords(playerZ() < -0x1800 ? stateAddress(TableId::FireRuinRoomsSt27Sub0NearSpawns) : stateAddress(TableId::FireRuinRoomsSt27Sub0FarSpawns), 3);
        });
    case RoutineId::St27Sub1RoomHandler: // sub 1
        return onceThenDoor(run, [] { spawnRecords(stateAddress(TableId::FireRuinRoomsSt27Sub1Spawns), 1); });
    case RoutineId::St27Sub7RoomHandler: // sub 7
        return onceThenDoor(run, [] { spawnRecords(stateAddress(TableId::FireRuinRoomsSt27Sub7Spawns), 1); });
    case RoutineId::St27RoomHandler:
        if (run) st27Crossing();
        return true;
    case RoutineId::St27RoomHandlerA:
        if (run) st27LavaPassage();
        return true;
    case RoutineId::St27RoomHandlerB:
        if (run) st27Entrance();
        return true;
    // ---- ST28 (story 13) -----------------------------------------------------------------------------------------
    case RoutineId::St28RoomHandler:
        if (run) st28BossRoom();
        return true;
    case RoutineId::St28Sub2RoomHandler: // sub 2
        return onceThenDoor(run, [] { spawnRecords(stateAddress(TableId::FireRuinRoomsSt28Sub2Spawns), 1); });
    case RoutineId::St28RoomHandlerA:
        if (run) st28Approach();
        return true;
    case RoutineId::St28RoomHandlerB:
        if (run) st28Lift();
        return true;
    case RoutineId::St28RoomHandlerC:
        if (run) st28Depths();
        return true;
    // ---- story 14 (after the fire ruin): one-shot setups (the door: the case's flag-0x700 tail) -------------------
    case RoutineId::St26Sub2RoomHandler: // ST26 sub 2
        return once(run, [] { clearStoryFlag(0x712); });
    case RoutineId::St26Sub3RoomHandler: // ST26 sub 3
        return once(run, [] { clearStoryFlag(0x719); });
    case RoutineId::St27Sub0RoomHandlerA: // ST27 sub 0
        return once(run, [] { spawnRecords(playerZ() < -0x1800 ? stateAddress(TableId::FireRuinRoomsSt27Sub0NearSpawns) : stateAddress(TableId::FireRuinRoomsSt27Sub0FarSpawns), 3); });
    case RoutineId::St27Sub2RoomHandler: // ST27 sub 2
        return once(run, [] {
            clearStoryFlag(0x714);
            spawnRecords(stateAddress(TableId::FireRuinRoomsSt27CrossingFirstBridgeSpawns), 1);
            spawnRecords(stateAddress(TableId::FireRuinRoomsSt27CrossingSecondBridgeSpawns), 1);
        });
    case RoutineId::St27Sub8RoomHandler: // ST27 sub 8
        return once(run, [] { spawnRecords(stateAddress(TableId::FireRuinRoomsSt27EntranceSpawns), 3); });
    case RoutineId::St28Sub0RoomHandler: // ST28 sub 0: the fight's flags cleared
        return once(run, [] {
            clearStoryFlag(0x717);
            clearStoryFlag(0x718);
            clearStoryFlag(0x710);
            clearStoryFlag(0x711);
            clearStoryFlag(0x715);
        });
    // ---- ST33 / ST34 (story 13) ----------------------------------------------------------------------------------
    case RoutineId::St33RoomHandler:
        if (run) st33Entry();
        return true;
    case RoutineId::St34Sub5RoomHandler: // ST34 sub 5
        return onceThenDoor(run, [] { spawnRecords(stateAddress(TableId::FireRuinRoomsSt34Sub5Spawns), 1); });
    default: return false;
    }
}

} // namespace od::port
