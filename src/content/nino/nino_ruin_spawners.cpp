// The Nino ruin's enemy spawners (pool 0xe0 type 0xa): ST36 exe 0x4213e0, ST37 exe 0x421670, ST38 exe 0x421bb0 (see
// nino_ruin_spawners.hpp), one routine per exe routine.
#include "content/nino/nino_ruin_spawners.hpp"
#include "content/nino/nino_ruin_spawners_state.hpp"

#include "content/nino/water.hpp"
#include "game/loaded_data.hpp"
#include "game/water_splash_state.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/game_state.hpp"
#include "game/message.hpp"
#include "game/message_state.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/room_scripts.hpp"
#include "game/spawn.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace nino_ruin_spawners_state;

namespace {

constexpr const char* kEnemySpawnerPort = "enemy spawner port"; // the area of its port-gap errors

// [0x88b5c4] / [0x88b860] / [0x88ba80]
enum class RuinSpawnerState : uint8_t { Init = 0, Run = 1 };
// ST36's script steps (sub-room 5, the sub-state)
enum class St36ScriptStep : uint8_t { WaitActor = 0, Fight = 1, Last = 2, Done = 3 };

// A bubble entry of the exe's tables (10 bytes): its point, yaw, speed, size mask and the next countdown's limit base
struct BubbleEntry : DataRecord {
    static constexpr uint32_t kBytes = 0xa;
    int16_t x = 0; // +0x0
    int16_t y = 0; // +0x2
    int16_t z = 0; // +0x4
    int8_t yaw = 0; // +0x6
    uint8_t speed = 0; // +0x7
    uint8_t sizeMask = 0; // +0x8
    int8_t limit = 0; // +0x9
    template <class R> static BubbleEntry load(const R& r) {
        BubbleEntry v;
        v.x = r.template read<int16_t>(0x0);
        v.y = r.template read<int16_t>(0x2);
        v.z = r.template read<int16_t>(0x4);
        v.yaw = r.template read<int8_t>(0x6);
        v.speed = r.template read<uint8_t>(0x7);
        v.sizeMask = r.template read<uint8_t>(0x8);
        v.limit = r.template read<int8_t>(0x9);
        return v;
    }
};
// a stage's bubble tables (by sub-room) and their entry counts
struct BubbleTables {
    TableId entries;  // the tables' pointers by sub-room
    TableId divisors; // the entry counts (s8) by sub-room
};
constexpr BubbleTables kSt36Bubbles = {TableId::NinoRuinSpawnersSt36BubbleEntries, TableId::NinoRuinSpawnersSt36BubbleDivisors};
constexpr BubbleTables kSt37Bubbles = {TableId::NinoRuinSpawnersSt37BubbleEntries, TableId::NinoRuinSpawnersSt37BubbleDivisors};
constexpr BubbleTables kSt38Bubbles = {TableId::NinoRuinSpawnersSt38BubbleEntries, TableId::NinoRuinSpawnersSt38BubbleDivisors};
BubbleEntry bubbleEntry(const BubbleTables& t, uint8_t sub, int16_t k) {
    return dataRecord<BubbleEntry>(dataScalar<uint32_t>(stateAddress(t.entries) + uint32_t(sub) * 4) + uint32_t(int32_t(k)) * 10, BubbleEntry::kBytes);
}

constexpr uint8_t kRoomActorDormant = 1; // a room record's actor (its third parameter) kept still
constexpr uint32_t kFlagSt36Cleared = 0x90, kFlagSt37Cleared = 0x91, kFlagSt38Cleared = 0x92;
constexpr uint32_t kFlagSt37TrapA = 0x96, kFlagSt37TrapB = 0x97, kFlagTrapSprung = 0x599;
constexpr uint16_t kSoundBurst = 0x1e3;

uint32_t roomRecordChecked(uint32_t rec) {
    const uint32_t s = spawnRecordTracked(rec);
    if (s == 0) notPorted(kEnemySpawnerPort, "ST36 room record with a full pool (the exe writes address 0xe)");
    return s;
}
// a room record's actor kept still (its third parameter)
void keepStill(uint32_t actor) { actorHeaderAt(actor).params[2] = kRoomActorDormant; }
// the parameter of room actor k (the room scripts' records)
uint8_t& roomActorParam(size_t k) {
    return actorHeaderAt(roomScriptState().actors[k]).params[0]; // its +0xc
}

// exe 0x421d80(entry): a bubble burst (a splash, static type 0x48) from the entry: its point, its yaw, its speed, its
// size mask; its particles' life and count random (taken only when placed)
void spawnBubbles(const BubbleEntry& ent) {
    const uint32_t s = allocActor60(kActorStageClass);
    if (s == 0) return;
    StaticActor& splash = staticActorAt(s);
    WaterSplashFields& params = classState<WaterSplashFields>(splash);
    splash.flags = kActorLive;
    splash.loopFlags |= kSplashLoopFlags;
    splash.type = kSplashType;
    splash.subtype = 0;
    splash.variant = 0;
    splash.pos.x.whole = ent.x;
    splash.pos.y.whole = ent.y;
    splash.pos.z.whole = ent.z;
    splash.rotation.x = 0;
    splash.rotation.y = int16_t(int32_t(ent.yaw));
    splash.rotation.z = 0;
    params.speed = int16_t(0u - uint32_t(ent.speed));
    params.sizeMask = int8_t(ent.sizeMask);
    params.particleLife = int8_t((rngStep() & 0xf) + 0x10u);
    params.count = int8_t((rngStep() & 3) + 4u);
}

// exe 0x421aa0(entry, burst): the ST37 bubbles from the entry (its yaw << 6): a burst -> pitched up, faster and bigger;
// else as spawnBubbles
void spawnBubblesSt37(const BubbleEntry& ent, bool burst) {
    const uint32_t s = allocActor60(kActorStageClass);
    if (s == 0) return;
    StaticActor& splash = staticActorAt(s);
    WaterSplashFields& params = classState<WaterSplashFields>(splash);
    splash.loopFlags |= kSplashLoopFlags;
    splash.flags = kActorLive;
    splash.type = kSplashType;
    splash.subtype = 0;
    splash.variant = 0;
    splash.pos.x.whole = ent.x;
    splash.pos.y.whole = ent.y;
    splash.pos.z.whole = ent.z;
    splash.rotation.y = int16_t(uint32_t(int32_t(ent.yaw)) << 6);
    splash.rotation.z = 0;
    if (burst) {
        const uint32_t r = rngStep();
        splash.rotation.x = -0x180;
        params.speed = int16_t(((r & 0x3f) << 3) - (uint32_t(ent.speed) << 3));
        params.particleLife = 0x18;
        params.sizeMask = int8_t(ent.sizeMask + 0x20u);
        params.count = 7;
    } else {
        params.speed = int16_t(0u - uint32_t(ent.speed));
        splash.rotation.x = 0;
        params.sizeMask = int8_t(ent.sizeMask);
        params.particleLife = int8_t((rngStep() & 0xf) + 0x10u);
        params.count = int8_t((rngStep() & 3) + 4u);
    }
}

// the bubbles' countdown: at its limit a burst from the entry; at 0 a new countdown (0x40 + RNG & 0x3f), entry (RNG %
// the sub-room's count) and limit (the entry's + RNG & 0xf)
template <class Spawn>
void bubbleCountdown(RuinSpawnerFields& spawner, const BubbleTables& tables, const char* who, Spawn spawn) {
    const uint8_t sub = gameState().room;
    spawn(bubbleEntry(tables, sub, spawner.entry));
    if (uint16_t(spawner.countdown) == 0) {
        spawner.countdown = int16_t((rngStep() & 0x3f) + 0x40u);
        const uint32_t r = rngStep();
        const uint32_t div = uint32_t(int32_t(dataScalar<int8_t>(stateAddress(tables.divisors) + gameState().room)));
        if (div == 0) notPorted(kEnemySpawnerPort, who);
        spawner.entry = int16_t(r % div);
        const uint32_t r2 = rngStep();
        spawner.limit = int16_t(uint32_t(int32_t(bubbleEntry(tables, gameState().room, spawner.entry).limit)) + (r2 & 0xf));
    }
}
// the countdown run down (at 0 its limit cleared)
void countDown(RuinSpawnerFields& spawner) {
    const int16_t t = int16_t(uint16_t(uint16_t(spawner.countdown) - 1u));
    spawner.countdown = t;
    if (t < 0) {
        spawner.countdown = 0;
        spawner.limit = 0;
    }
}

// exe 0x4213e0: state 0 = 0x421400 (the room records by sub-room 3 / 4 / 5; without flag 0x90 the sub 3 / 4 ones kept
// still), 1 = 0x4214c0: with flag 0x90 the bubbles' countdown (tables [0x88b4fc + sub * 4]). Sub-room 5: room actor 1
// active -> the script group 4; its end -> the window messages 0x2d / 0x2f and room actors 2 / 3 started / stopped. The
// alive count of the script's slot kept.
void st36(ControllerActor& actor) {
    auto&& spawner = classState<RuinSpawnerFields>(actor);
    const uint8_t sub = gameState().room;
    switch (RuinSpawnerState(actor.state)) {
    case RuinSpawnerState::Init:
        spawner.clearCountdown();
        if (sub == 3) {
            const uint32_t r = roomRecordChecked(stateAddress(TableId::NinoRuinSpawnersSt36RoomRecord));
            if (!storyFlag(kFlagSt36Cleared)) keepStill(r);
        } else if (sub == 4) {
            const uint32_t r0 = roomRecordChecked(stateAddress(TableId::NinoRuinSpawnersSt36FirstRoomRecord));
            const uint32_t r1 = roomRecordChecked(stateAddress(TableId::NinoRuinSpawnersSt36SecondRoomRecord));
            if (!storyFlag(kFlagSt36Cleared)) {
                keepStill(r0);
                keepStill(r1);
            }
        } else if (sub == 5) {
            spawnRecordTracked(stateAddress(TableId::NinoRuinSpawnersSt36FirstRecord));
            spawnRecordTracked(stateAddress(TableId::NinoRuinSpawnersSt36SecondRecord));
            spawnRecordTracked(stateAddress(TableId::NinoRuinSpawnersSt36ThirdRecord));
        }
        actor.enterState(uint8_t(RuinSpawnerState::Run));
        return;
    case RuinSpawnerState::Run: {
        if (storyFlag(kFlagSt36Cleared) && spawner.countdown <= spawner.limit)
            bubbleCountdown(spawner, kSt36Bubbles, "ST36 spawner: division by zero in the exe",
                            [](const BubbleEntry& e) { spawnBubbles(e); });
        if (gameState().room == 5) {
            switch (St36ScriptStep(actor.subState)) {
            case St36ScriptStep::WaitActor:
                if (roomActorParam(1) != 0) {
                    encounterSpawnGroupScriptSlot(4);
                    actor.subState = uint8_t(St36ScriptStep::Fight);
                }
                break;
            case St36ScriptStep::Fight:
                if (encounterScriptSlotAlive() == 0) {
                    actor.subState = uint8_t(St36ScriptStep::Done);
                } else {
                    windowOpen(0, kMessageBank, 0x2d);
                    roomActorParam(2) = 1;
                    roomActorParam(3) = 1;
                    actor.subState = uint8_t(St36ScriptStep::Last);
                }
                break;
            case St36ScriptStep::Last:
                if (encounterScriptSlotAlive() == 0) {
                    windowOpen(0, kMessageBank, 0x2f);
                    roomActorParam(2) = 0;
                    roomActorParam(3) = 0;
                    actor.subState = uint8_t(St36ScriptStep::Done);
                }
                break;
            default: break;
            }
        }
        spawner.scriptAlive = uint8_t(encounterScriptSlotAlive());
        countDown(spawner);
        return;
    }
    default: notPortedAt(kEnemySpawnerPort, "ST36 spawner 0x4213e0 state", actor.state);
    }
}

// the moved copy of sub-rooms 2 / 13 (flag 0x96 / 0x97): the second record's actor marked moved (its variant's high
// bit), at the first one's position, rising 0x10, 0x20 higher
void st37Move(uint32_t first, uint32_t second) {
    if (first == 0 || second == 0)
        notPorted(kEnemySpawnerPort, "ST37 room record with a full pool (the exe dereferences null)");
    Actor& a = actorAt(first);
    Actor& b = actorAt(second);
    b.variant |= 0x80u;
    b.pos.x = a.pos.x;
    b.pos.y = a.pos.y;
    b.velocity.y = 0x10;
    b.pos.z = a.pos.z;
    b.pos.y.whole = int16_t(uint16_t(b.pos.y.whole) - 0x20u);
}

// exe 0x421670: state 0 = 0x421690 (the room records by sub-room 2 / 8 / 13; the trap's point and its pull), 1 =
// 0x421830: without flag 0x91, in sub-rooms 2 / 13 while room actor 1 is idle: a burst of bubbles (8 entries in turn)
// with sound 0x1e3 every 20 ticks, and the player within the range of the trap's point is pulled toward it; room actor
// 1 active -> flag 0x599 once. Then the same countdown as ST36 (its own tables).
void st37(ControllerActor& actor) {
    auto&& spawner = classState<RuinSpawnerFields>(actor);
    const uint8_t sub = gameState().room;
    switch (RuinSpawnerState(actor.state)) {
    case RuinSpawnerState::Init: {
        spawner.clearCountdown();
        if (sub == 2 || sub == 0xd) {
            const uint32_t b = spawnRecordTracked(sub == 2 ? stateAddress(TableId::NinoRuinSpawnersSt37Sub2FirstRecord) : stateAddress(TableId::NinoRuinSpawnersSt37FirstRecord));
            const uint32_t e = spawnRecordTracked(sub == 2 ? stateAddress(TableId::NinoRuinSpawnersSt37Sub2SecondRecord) : stateAddress(TableId::NinoRuinSpawnersSt37SecondRecord));
            if (storyFlag(sub == 2 ? kFlagSt37TrapA : kFlagSt37TrapB)) st37Move(b, e);
            if (storyFlag(kFlagSt37Cleared)) {
                if (e == 0)
                    notPorted(kEnemySpawnerPort, "ST37 room record with a full pool (the exe writes address 0xe)");
                keepStill(e);
                actor.enterState(uint8_t(RuinSpawnerState::Run));
                return;
            }
            spawner.trapPoint.x = 0x200;
            spawner.trapPoint.y = 0;
            spawner.trapPoint.z = int16_t(sub == 2 ? 0x600 : 0x200);
            spawner.pullStrength = -0x580;
            spawner.pullRange = 0x400;
        } else if (sub == 8) {
            const uint32_t r0 = spawnRecordTracked(stateAddress(TableId::NinoRuinSpawnersSt37ThirdRecord));
            const uint32_t r1 = spawnRecordTracked(stateAddress(TableId::NinoRuinSpawnersSt37FourthRecord));
            if (storyFlag(kFlagSt37Cleared)) {
                if (r0 == 0 || r1 == 0)
                    notPorted(kEnemySpawnerPort, "ST37 room record with a full pool (the exe writes address 0xe)");
                keepStill(r0);
                keepStill(r1);
            }
        }
        actor.enterState(uint8_t(RuinSpawnerState::Run));
        return;
    }
    case RuinSpawnerState::Run: {
        const bool trapRoom = sub == 2 || sub == 0xd;
        if (storyFlag(kFlagSt37Cleared)) {
            if (trapRoom && actor.step == 0) {
                roomActorParam(2) = 2;
                actor.step += 1u;
            }
        } else {
            bool countdown = true;
            if (trapRoom) {
                if (actor.step == 0) {
                    roomActorParam(2) = 2;
                    actor.step += 1u;
                }
                if (roomActorParam(1) == 0) {
                    if (spawner.soundTicks == 0) {
                        soundPlayAtPosS16(kSoundBurst, spawner.trapPoint);
                        spawner.soundTicks = 0x14;
                        actor.step += 1u;
                    }
                    spawner.soundTicks -= 1u;
                    // exe 0x5e3b40({the player's x, y, z}, the trap's point): the planar distance of the s16 points
                    Player& p = player();
                    const int32_t dx = int16_t(uint16_t(uint16_t(spawner.trapPoint.x) - uint16_t(p.pos.x.whole)));
                    const int32_t dz = int16_t(uint16_t(uint16_t(spawner.trapPoint.z) - uint16_t(p.pos.z.whole)));
                    const int d = isqrt(int32_t(uint32_t(dx) * uint32_t(dx) + uint32_t(dz) * uint32_t(dz)));
                    const int32_t range = spawner.pullRange;
                    if (d < range) {
                        p.pulled = 1;
                        p.pullX = spawner.trapPoint.x;
                        p.pullY = spawner.trapPoint.y;
                        p.pullZ = spawner.trapPoint.z;
                        p.pullStrength = spawner.pullStrength;
                        p.pullRadius = int16_t(range);
                    }
                    spawnBubblesSt37(bubbleEntry(kSt37Bubbles, gameState().room, spawner.entry), true);
                    spawner.entry += 1u;
                    if (uint16_t(spawner.entry) == 8) spawner.entry = 0;
                    countdown = false;
                } else {
                    if (actor.stepCounter == 0) {
                        setStoryFlag(kFlagTrapSprung);
                        actor.stepCounter = 1;
                    }
                    spawner.soundTicks = 0;
                }
            }
            if (countdown && spawner.countdown <= spawner.limit) // exe 0x42197c
                bubbleCountdown(spawner, kSt37Bubbles, "ST37 spawner: division by zero in the exe",
                                [](const BubbleEntry& e) { spawnBubblesSt37(e, false); });
        }
        countDown(spawner); // exe 0x421a86
        return;
    }
    default: notPortedAt(kEnemySpawnerPort, "ST37 spawner 0x421670 state", actor.state);
    }
}

// exe 0x421bb0: state 0 = 0x421bd0 (the room records: sub-room 2 -> 0x88ba40, 7 / 8 -> 0x88ba58 / 0x88ba6c; with flag
// 0x92 kept still), 1 = 0x421c70: without flag 0x92 the bubbles' countdown (its own tables)
void st38(ControllerActor& actor) {
    auto&& spawner = classState<RuinSpawnerFields>(actor);
    const uint8_t sub = gameState().room;
    switch (RuinSpawnerState(actor.state)) {
    case RuinSpawnerState::Init:
        spawner.clearCountdown();
        if (sub == 2) {
            const uint32_t r = spawnRecordTracked(stateAddress(TableId::NinoRuinSpawnersSt38Record));
            if (storyFlag(kFlagSt38Cleared)) {
                if (r == 0)
                    notPorted(kEnemySpawnerPort, "ST38 room record with a full pool (the exe writes address 0xe)");
                keepStill(r);
            }
        } else if (sub > 6 && sub <= 8) {
            const uint32_t r0 = spawnRecordTracked(stateAddress(TableId::NinoRuinSpawnersSt38FirstRecord));
            const uint32_t r1 = spawnRecordTracked(stateAddress(TableId::NinoRuinSpawnersSt38SecondRecord));
            if (storyFlag(kFlagSt38Cleared)) {
                if (r0 == 0 || r1 == 0)
                    notPorted(kEnemySpawnerPort, "ST38 room record with a full pool (the exe writes address 0xe)");
                keepStill(r0);
                keepStill(r1);
            }
        }
        actor.enterState(uint8_t(RuinSpawnerState::Run));
        return;
    case RuinSpawnerState::Run: {
        if (!storyFlag(kFlagSt38Cleared) && spawner.countdown <= spawner.limit)
            bubbleCountdown(spawner, kSt38Bubbles, "ST38 spawner: division by zero in the exe",
                            [](const BubbleEntry& e) { spawnBubbles(e); });
        countDown(spawner);
        return;
    }
    default: notPortedAt(kEnemySpawnerPort, "ST38 spawner 0x421bb0 state", actor.state);
    }
}

} // namespace

void spawnerSt36Update(const PlayerEnv&, uint32_t actor) { st36(controllerActorAt(actor)); }
void spawnerSt37Update(const PlayerEnv&, uint32_t actor) { st37(controllerActorAt(actor)); }
void spawnerSt38Update(const PlayerEnv&, uint32_t actor) { st38(controllerActorAt(actor)); }

} // namespace od::port
