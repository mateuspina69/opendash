// Calbania's enemy spawners (pool 0xe0 type 0xa): ST1F exe 0x41ef20, ST21 exe 0x41f850, ST22 exe 0x41f940 (see
// calbania_spawners.hpp).
#include "content/calbania/calbania_spawners.hpp"
#include "content/calbania/calbania_spawners_state.hpp"

#include "content/calbania/appo_dah_fight.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/address_forms.hpp"
#include "game/encounter.hpp"
#include "game/game_state.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"
#include "game/spawn.hpp"
#include "game/story_flags.hpp"
#include "game/transition_state.hpp"

namespace od::port {

using namespace calbania_spawners_state;

namespace {

constexpr const char* kEnemySpawnerPort = "enemy spawner port"; // the area of its port-gap errors

// the story's flags of the ST1F fight in front of Glyde's base
constexpr uint32_t kFlagDahAtGate = 0x580;
constexpr uint32_t kFlagAppoCarried = 0x581, kFlagDahCarried = 0x582; // the pair's flags: Appo / Dah carried away
constexpr uint32_t kFlagFightStarted = 0x583;
constexpr uint32_t kFlagFirstWave = 0x584, kFlagSecondWave = 0x585, kFlagAtGate = 0x586;
constexpr uint32_t kFlagBaseDone = 0x590;      // back in the town after Glyde's base
constexpr uint32_t kFlagSt21Done = 0x591, kFlagSt22Cleared = 0x598, kFlagSt1fCleared = 0x599;
constexpr uint8_t kStoryCalbania = 8;

// A spawn record with the encounter id the spawner tracks its actor with (0x18 bytes)
struct TrackedRecord : DataRecord {
    static constexpr uint32_t kBytes = 0x18;
    SpawnRecord record;
    uint32_t encounterId = 0; // +0x14
    template <class R> static TrackedRecord load(const R& r) { return {{}, SpawnRecord::load(r), r.template read<uint32_t>(0x14)}; }
};
uint32_t trackedId(uint32_t record) { return dataRecord<TrackedRecord>(record, TrackedRecord::kBytes).encounterId; }
constexpr uint32_t kTrackedRecordSize = 0x18;

constexpr size_t kAppo = 0, kDah = 1;

// the parameters of the fight's enemies (the leader, the guards, the reinforcements): their mode and the pair's slots
struct FightEnemyFields {
    explicit FightEnemyFields(const AnimatedActor& actor) : FightEnemyFields(const_cast<AnimatedActor&>(actor)) {}
    explicit FightEnemyFields(AnimatedActor& actor) : mode(actor.params[0]), appoSlot(actor.params[2]), dahSlot(actor.params[3]) {}
    uint8_t& mode;
    uint8_t& appoSlot;
    uint8_t& dahSlot;
};

// +9 of the ST1F spawner (the exe's table 0x88ad30) and the steps of its sub-states (+0xa)
enum class St1fSpawnerState : uint8_t { Clear = 0, Run = 1 }; // exe 0x41ef40 / 0x41ef70
enum class FightSub : uint8_t {
    Start = 0,      // exe 0x41ef90: the fight's stage the story flags reached
    FirstWave = 1,  // exe 0x41f1c0
    SecondWave = 2, // exe 0x41f390
    AtGate = 3,     // exe 0x41f4e0
};
enum class StartStep : uint8_t { Pick = 0, PlaceAll = 1, Spawn = 2, ResumeFirst = 3, ResumeSecond = 4, ResumeGate = 5,
                                 Done = 6 };
enum class WaveStep : uint8_t { Watch = 0, Release = 1, WaitScript = 2 };
enum class GateStep : uint8_t { Watch = 0, WaitScript = 1 };
enum class FieldSpawnerState : uint8_t { Spawn = 0, Free = 1 }; // ST21 / ST22: exe 0x41f870 / 0x41f960, 0x427cf0

constexpr uint8_t kPairReleased = 3; // the state the pair's actors are put in when they are let go


// the pair's room test of the waves: the player's integer x in [0x1377, 0x1677], z in [-0x300, 0x100] (u16
// differences) and on the ground
bool playerInPairRoom() {
    const Player& p = player();
    if (uint16_t(uint16_t(p.pos.x.whole) - 0x1377u) > 0x300) return false;
    if (uint16_t(uint16_t(p.pos.z.whole) + 0x300u) > 0x400) return false;
    return p.airborne == 0;
}

// the pair's actor of slot id `slot` (by the exe's unchecked slot arithmetic: a slot 0 lands below the pool, as in the
// exe)
AppoDahFightFields& pairFields(uint8_t slot) {
    return classState<AppoDahFightFields>(animatedActorAt(address_forms::animatedSlotAddress(slot)));
}
Actor& pairActor(uint8_t slot) { return actorAt(address_forms::animatedSlotAddress(slot)); }

// the pair's actor of slot s is carried
bool pairCarried(uint8_t s) { return s != 0 && pairFields(s).carried != 0; }

// the record placement shared by exe 0x41f540 / 0x41f610 / 0x41f680 / 0x41f710 / 0x41f790: a pool-0x20 actor with the
// record's flags, loop flags, type and parameters at its point and yaw (nullptr: the pool is full)
AnimatedActor* placeRecord(uint32_t record) {
    const uint32_t s = allocActor20();
    if (s == 0) return nullptr;
    const SpawnRecord r = dataRecord<SpawnRecord>(record, SpawnRecord::kBytes);
    AnimatedActor& actor = animatedActorAt(s);
    actor.flags = r.flags;
    actor.loopFlags |= r.loopFlags;
    actor.setTypeWord(r.typeWord);
    actor.setParamsWord(r.paramsWord);
    actor.pos.x.whole = r.point.x;
    actor.pos.y.whole = r.point.y;
    actor.pos.z.whole = r.point.z;
    actor.rotation.y = r.yaw;
    return &actor;
}

// exe 0x41f540: the leader and the 6 guards without their parameters, untracked
void placeWholeFight() {
    if (AnimatedActor* s = placeRecord(stateAddress(TableId::CalbaniaSpawnersLeaderRecord))) s->setParamsWord(0);
    for (uint32_t rec = stateAddress(TableId::CalbaniaSpawnersFirstGuardRecord);; rec -= kTrackedRecordSize) {
        if (AnimatedActor* s = placeRecord(rec)) s->setParamsWord(0);
        if (rec == stateAddress(TableId::CalbaniaSpawnersLastGuardRecord)) break;
    }
}

// exe 0x41f610: the pair (Dah, then Appo) into the spawner's slots (0: the pool is full)
void spawnPair(St1fSpawnerFields& f) {
    size_t k = kDah;
    for (uint32_t rec = stateAddress(TableId::CalbaniaSpawnersDahRecord);; rec -= kSpawnRecordSize, --k) {
        AnimatedActor* s = placeRecord(rec);
        f.pairSlots[k] = s ? uint8_t(s->slot) : uint8_t(0);
        if (rec == stateAddress(TableId::CalbaniaSpawnersAppoRecord)) break;
    }
}

// the pair's slots handed to an enemy of the fight
void givePairSlots(AnimatedActor& enemy, St1fSpawnerFields& f) {
    FightEnemyFields e(enemy);
    e.appoSlot = f.pairSlots[kAppo];
    e.dahSlot = f.pairSlots[kDah];
}

// exe 0x41f680: the leader (mode 0, the pair's slots), tracked
void spawnLeader(St1fSpawnerFields& f) {
    AnimatedActor* s = placeRecord(stateAddress(TableId::CalbaniaSpawnersLeaderRecord));
    if (s == nullptr) return;
    FightEnemyFields(*s).mode = 0;
    givePairSlots(*s, f);
    encounterTrackInScriptSlot(*s, uint16_t(trackedId(stateAddress(TableId::CalbaniaSpawnersLeaderRecord))));
}

// exe 0x41f710: the 6 guards (the pair's slots), tracked
void spawnGuards(St1fSpawnerFields& f) {
    for (uint32_t rec = stateAddress(TableId::CalbaniaSpawnersFirstGuardRecord);; rec -= kTrackedRecordSize) {
        if (AnimatedActor* s = placeRecord(rec)) {
            givePairSlots(*s, f);
            encounterTrackInScriptSlot(*s, uint16_t(trackedId(rec)));
        }
        if (rec == stateAddress(TableId::CalbaniaSpawnersLastGuardRecord)) break;
    }
}

// exe 0x41f790: reinforcements by the script slot's alive count (0 -> two records after the leader's; < 3 or id 1
// defeated -> the first; else none), their mode k & 1, the pair's slots, tracked
void spawnReinforcements(St1fSpawnerFields& f) {
    const int32_t n = encounterScriptSlotAlive();
    uint32_t k;
    if (n == 0) k = 3;
    else if (n < 3) k = 2;
    else k = enemyDefeated(1) ? 2 : 1;
    if (k - 1 == 0) return;
    uint32_t rec = stateAddress(TableId::CalbaniaSpawnersLeaderRecord) + k * kTrackedRecordSize;
    do {
        --k;
        rec -= kTrackedRecordSize;
        if (AnimatedActor* s = placeRecord(rec)) {
            FightEnemyFields(*s).mode = uint8_t(k & 1);
            givePairSlots(*s, f);
            encounterTrackInScriptSlot(*s, uint16_t(trackedId(rec)));
        }
    } while (k != 1);
}

// the pair placed at the fight's start
void placePair(St1fSpawnerFields& f) {
    if (f.pairSlots[kAppo] != 0) {
        Actor& appo = pairActor(f.pairSlots[kAppo]);
        appo.pos.x.whole = int16_t(0xf18e);
        appo.pos.y.whole = int16_t(0xfd44);
        appo.pos.z.whole = int16_t(0xf7b6);
    }
    if (f.pairSlots[kDah] != 0) {
        Actor& dah = pairActor(f.pairSlots[kDah]);
        dah.pos.x.whole = int16_t(0xef12);
        dah.pos.y.whole = int16_t(0xfd2a);
        dah.pos.z.whole = int16_t(0xf961);
    }
}

// the player's hands let go (the pair dropped): the interaction target and the carry cleared
void playerLetGo() {
    Player& p = player();
    p.interactionTarget.clear();
    p.carryFlags = 0;
}

// the sub-state s at its first step
void enterSub(ControllerActor& a, FightSub s) {
    a.subState = uint8_t(s);
    a.restartStep();
}

// exe 0x41ef90 (jump table 0x41f198): which stage of the fight the story flags reached
void fightStart(ControllerActor& a, St1fSpawnerFields& f) {
    switch (StartStep(a.step)) {
    case StartStep::Pick:
        if (storyFlag(kFlagBaseDone)) setStoryFlag(kFlagSt1fCleared);
        a.step = uint8_t(StartStep::PlaceAll);
        if (storyFlag(kFlagFightStarted)) a.step = uint8_t(StartStep::ResumeFirst);
        if (storyFlag(kFlagFirstWave)) a.step = uint8_t(StartStep::ResumeSecond);
        if (storyFlag(kFlagSecondWave)) a.step = uint8_t(StartStep::ResumeGate);
        if (storyFlag(kFlagAtGate)) a.step = uint8_t(StartStep::Done);
        if (gameState().story != kStoryCalbania) a.step = uint8_t(StartStep::Done);
        return;
    case StartStep::PlaceAll:
        placeWholeFight();
        setStoryFlag(kFlagFightStarted);
        a.step = uint8_t(StartStep::Spawn);
        return;
    case StartStep::Spawn:
        if (scriptState().inUse()) return;
        spawnPair(f);
        spawnLeader(f);
        spawnGuards(f);
        placePair(f);
        enterSub(a, FightSub::FirstWave);
        return;
    case StartStep::ResumeFirst:
    case StartStep::ResumeSecond:
    case StartStep::ResumeGate:
        spawnPair(f);
        spawnLeader(f);
        spawnGuards(f);
        a.subState = uint8_t(a.step - 2u);
        a.restartStep();
        return;
    case StartStep::Done: freeActor(a); return;
    default: return;
    }
}

// exe 0x41f1c0: the first wave: a carried one of the pair sets its flag; then the reinforcements
void fightFirstWave(ControllerActor& a, St1fSpawnerFields& f) {
    switch (WaveStep(a.step)) {
    case WaveStep::Watch: {
        if (!playerInPairRoom()) return;
        if (pairCarried(f.pairSlots[kAppo])) setStoryFlag(kFlagAppoCarried);
        if (pairCarried(f.pairSlots[kDah])) setStoryFlag(kFlagDahCarried);
        const uint32_t k = (storyFlag(kFlagAppoCarried) ? 1u : 0u) + (storyFlag(kFlagDahCarried) ? 2u : 0u);
        switch (k) { // jump table 0x41f374
        case 0: return;
        case 1: setStoryFlag(kFlagFirstWave); break;
        case 2:
            setStoryFlag(kFlagDahAtGate);
            setStoryFlag(kFlagFirstWave);
            break;
        default:
            setStoryFlag(kFlagSecondWave);
            a.subState = uint8_t(FightSub::SecondWave);
            break;
        }
        a.step = uint8_t(WaveStep::Release);
        playerLetGo();
        return;
    }
    case WaveStep::Release:
        if (storyFlag(kFlagAppoCarried)) pairActor(f.pairSlots[kAppo]).enterState(kPairReleased);
        if (storyFlag(kFlagDahCarried)) pairActor(f.pairSlots[kDah]).enterState(kPairReleased);
        a.step = uint8_t(WaveStep::WaitScript);
        [[fallthrough]];
    case WaveStep::WaitScript:
        if (scriptState().inUse()) return;
        spawnReinforcements(f);
        enterSub(a, FightSub::SecondWave);
        return;
    default: return;
    }
}

// exe 0x41f390: the second wave: the other one of the pair carried
void fightSecondWave(ControllerActor& a, St1fSpawnerFields& f) {
    switch (WaveStep(a.step)) {
    case WaveStep::Watch:
        if (!playerInPairRoom()) return;
        if (pairCarried(f.pairSlots[kAppo])) setStoryFlag(kFlagAppoCarried);
        else if (pairCarried(f.pairSlots[kDah])) setStoryFlag(kFlagDahCarried);
        else if (!storyFlag(kFlagAppoCarried) || !storyFlag(kFlagDahCarried)) return;
        playerLetGo();
        setStoryFlag(kFlagSecondWave);
        a.step = uint8_t(WaveStep::Release);
        return;
    case WaveStep::Release:
        pairActor(f.pairSlots[kAppo]).enterState(kPairReleased);
        pairActor(f.pairSlots[kDah]).enterState(kPairReleased);
        a.step = uint8_t(WaveStep::WaitScript);
        [[fallthrough]];
    case WaveStep::WaitScript:
        if (scriptState().inUse()) return;
        enterSub(a, FightSub::AtGate);
        return;
    default: return;
    }
}

// exe 0x41f4e0: at the gate: the player standing on Appo -> flag 0x586
void fightAtGate(ControllerActor& a, St1fSpawnerFields& f) {
    switch (GateStep(a.step)) {
    case GateStep::Watch:
        if (f.pairSlots[kAppo] != 0 && pairFields(f.pairSlots[kAppo]).ridden != 0) {
            setStoryFlag(kFlagAtGate);
            a.step = uint8_t(GateStep::WaitScript);
        }
        return;
    case GateStep::WaitScript:
        if (scriptState().inUse()) return;
        enterSub(a, FightSub::AtGate);
        return;
    default: return;
    }
}

// exe 0x41ef70: the sub-state
void st1fFight(ControllerActor& a) {
    St1fSpawnerFields& f = classState<St1fSpawnerFields>(a);
    switch (FightSub(a.subState)) {
    case FightSub::Start: fightStart(a, f); return;
    case FightSub::FirstWave: fightFirstWave(a, f); return;
    case FightSub::SecondWave: fightSecondWave(a, f); return;
    case FightSub::AtGate: fightAtGate(a, f); return;
    default: notPortedAt(kEnemySpawnerPort, "ST1F spawner sub", a.subState);
    }
}

// exe 0x41ef20 (states 0x88ad28): its bytes cleared and the pair's slots, then the sub-state at once; the variant is
// not read
void st1fSpawner(ControllerActor& a) {
    switch (St1fSpawnerState(a.state)) {
    case St1fSpawnerState::Clear: {
        St1fSpawnerFields& f = classState<St1fSpawnerFields>(a);
        f.clearBlock();
        a.enterState(uint8_t(St1fSpawnerState::Run));
        f.pairSlots[kAppo] = 0;
        f.pairSlots[kDah] = 0;
        st1fFight(a);
        return;
    }
    case St1fSpawnerState::Run: st1fFight(a); return;
    default: notPortedAt(kEnemySpawnerPort, "ST1F spawner 0x41ef20 state", a.state);
    }
}

// exe 0x41f9e0(record, point): a pool-0x20 actor: exe 0x50fd20 (the record's flags, its +1 / loop flags ORed, its type
// and parameters) and exe 0x50fd60(actor, point, 0) (the position and the previous position from the point, the
// rotation 0 / the point's yaw / 0, current and previous); its slot (0 = the pool is full)
uint8_t spawnAtPoint(uint32_t record, uint32_t point) {
    const uint32_t s = allocActor20();
    if (s == 0) return 0;
    const SpawnRecord r = dataRecord<SpawnRecord>(record, SpawnRecord::kBytes);
    const RoomPlace p = dataRecord<RoomPlace>(point, RoomPlace::kBytes);
    AnimatedActor& actor = animatedActorAt(s);
    actor.flags = r.flags;
    actor.runFlags |= r.listSlot; // the record's +1, ORed into the run flags
    actor.loopFlags |= r.loopFlags;
    actor.setTypeWord(r.typeWord);
    actor.setParamsWord(r.paramsWord);
    const int32_t x = int32_t(uint32_t(int32_t(p.x)) << 16), y = int32_t(uint32_t(int32_t(p.y)) << 16),
                  z = int32_t(uint32_t(int32_t(p.z)) << 16);
    actor.prevPos.x = x;
    actor.pos.x = x;
    actor.prevPos.y = y;
    actor.pos.y = y;
    actor.prevPos.z = z;
    actor.pos.z = z;
    actor.prevRotation.x = 0;
    actor.rotation.x = 0;
    actor.prevRotation.z = 0;
    actor.rotation.z = 0;
    actor.prevRotation.y = p.yaw;
    actor.rotation.y = p.yaw;
    return actor.slot;
}

// ST21 / ST22 records of the pair and their points (exe data)
constexpr uint32_t kPointSize = 8;

// ST21 (exe 0x41f850): state 0 (exe 0x41f870): story 8 without flag 0x598 -> the pair at its points; story 8 with flag
// 0x590 -> flag 0x591. Always: two records 0x88ad40 at the RNG (u16 % 3) pair of points. State 1 (exe 0x427cf0): freed.
void st21Spawner(ControllerActor& a) {
    switch (FieldSpawnerState(a.state)) {
    case FieldSpawnerState::Spawn: {
        classState<St1fSpawnerFields>(a).clearBlock();
        if (gameState().story == kStoryCalbania) {
            if (!storyFlag(kFlagSt22Cleared)) {
                spawnAtPoint(stateAddress(TableId::CalbaniaSpawnersSt21AppoRecord), stateAddress(TableId::CalbaniaSpawnersSt21AppoPoint));
                spawnAtPoint(stateAddress(TableId::CalbaniaSpawnersSt21DahRecord), stateAddress(TableId::CalbaniaSpawnersSt21DahPoint));
            }
            if (storyFlag(kFlagBaseDone)) setStoryFlag(kFlagSt21Done);
        }
        const uint32_t k = (rngStep() & 0xffff) % 3;
        uint32_t point = stateAddress(TableId::CalbaniaSpawnersSt21BirdPoints) + k * 0x10;
        for (int i = 0; i < 2; ++i, point += kPointSize) spawnAtPoint(stateAddress(TableId::CalbaniaSpawnersSt21BirdRecord), point);
        a.enterState(uint8_t(FieldSpawnerState::Free));
        return;
    }
    case FieldSpawnerState::Free: freeActor(a); return;
    default: notPortedAt(kEnemySpawnerPort, "ST21 spawner 0x41f850 state", a.state);
    }
}

// ST22 (exe 0x41f940): state 0 (exe 0x41f960): story 8 without flag 0x599 -> the pair at its points; story 8 with flag
// 0x590 -> flag 0x598. State 1 (exe 0x427cf0): freed.
void st22Spawner(ControllerActor& a) {
    switch (FieldSpawnerState(a.state)) {
    case FieldSpawnerState::Spawn:
        classState<St1fSpawnerFields>(a).clearBlock();
        if (gameState().story == kStoryCalbania) {
            if (!storyFlag(kFlagSt1fCleared)) {
                spawnAtPoint(stateAddress(TableId::CalbaniaSpawnersSt22AppoRecord), stateAddress(TableId::CalbaniaSpawnersSt22AppoPoint));
                spawnAtPoint(stateAddress(TableId::CalbaniaSpawnersSt22DahRecord), stateAddress(TableId::CalbaniaSpawnersSt22DahPoint));
            }
            if (storyFlag(kFlagBaseDone)) setStoryFlag(kFlagSt22Cleared);
        }
        a.enterState(uint8_t(FieldSpawnerState::Free));
        return;
    case FieldSpawnerState::Free: freeActor(a); return;
    default: notPortedAt(kEnemySpawnerPort, "ST22 spawner 0x41f940 state", a.state);
    }
}

} // namespace

void spawnerSt1fUpdate(const PlayerEnv&, uint32_t actor) { st1fSpawner(controllerActorAt(actor)); }
void spawnerSt21Update(const PlayerEnv&, uint32_t actor) { st21Spawner(controllerActorAt(actor)); }
void spawnerSt22Update(const PlayerEnv&, uint32_t actor) { st22Spawner(controllerActorAt(actor)); }

} // namespace od::port
