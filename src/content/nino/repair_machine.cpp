// ST18 (Nino island) at stories 9 / 0xa: Glyde's last attack. Roll repairs the machine while the player throws Glyde's
// missiles back (content/nino/glyde_missiles); the gauge of the ST18 spawner variant 3 (content/nino/field_spawners)
// fills while she works and sets flag 0x2e; the machine destroyed sets flag 0x2f. One routine per exe routine; the
// names are hypotheses from the observed behaviour.
//   0x4e41c0  pool 0x20 type 0x5e sub 0: the machine (a hit capsule + 2 hit entries, the HUD gauge, smoke columns by
//             its damage, destroyed -> flag 0x2f); spawns Roll + the gauge
//   0x4e67c0  pool 0x20 type 0x60 sub 0: Roll at the machine (work animations by the machine's hit state)
//   0x4e4930  pool 0x20 type 0x5e sub 1: the machine's other parts on ST18 sub 2
//   0x44a700  pool 0x20 type 2 sub 0: the prop riding a part, script-timed
// Render output left out as in the other ports: the shadows (exe 0x5f20f0).
#include "content/nino/repair_machine.hpp"
#include "content/nino/repair_machine_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/cutscene_actor.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/table_records.hpp"

namespace od::port {

using namespace repair_machine_state;

namespace {

int32_t stepTick() { return scriptState().stepTick; }

// the copies at the top of the updates (with the neutral tint)
void savePose(AnimatedActor& actor) {
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
}
// the animation stepped (the same) or started at key 0 (exe 0x5e1e80 / 0x5e1c30(a, anim, 0))
void animate(AnimatedActor& actor) {
    if (actor.anim == actor.prevAnim) animStepPose(actor);
    else animSetPose(actor, actor.anim, 0);
}
// placed at the actor followed + Ry(its yaw) (x, y, z), facing its way
void placeOn(AnimatedActor& actor, const AnimatedActor& base, int x, int y, int z) {
    int32_t o[3];
    rotateByYaw(base.rotation.y, x, y, z, o); // exe 0x5e38d0
    actor.pos.x = int32_t(uint32_t(base.pos.x) + uint32_t(o[0]));
    actor.pos.y = int32_t(uint32_t(base.pos.y) + uint32_t(o[1]));
    actor.pos.z = int32_t(uint32_t(base.pos.z) + uint32_t(o[2]));
    actor.rotation.y = base.rotation.y;
}

// ---- 0x4e41c0: the machine. States [0x8960cc]; the running state's sub-states [0x8960d8].
enum class MachineState : uint8_t { Init = 0, Run = 1, Destroyed = 2 };
enum class MachineSub : uint8_t { Hits = 0, React = 1 };
// the destroyed state's phases (exe 0x4e4740 by the sub-state)
enum class MachineEnd : uint8_t { Explode = 0, Wait = 1, Blasts = 2 };
// The machine's spawn parameters, rewritten as it runs: what happened to it (Roll reads it) and its hit state (the
// rate of the repair gauge and Roll's animations)
struct MachineParams {
    explicit MachineParams(const AnimatedActor& actor) : MachineParams(const_cast<AnimatedActor&>(actor)) {}
    explicit MachineParams(AnimatedActor& actor) : event(actor.params[0]), hitState(actor.params[1]) {}
    FieldAlias<int8_t, uint8_t> event; // kMachineHit / kMachineDestroyed
    FieldAlias<int8_t, uint8_t> hitState; // 0 idle, 1 / 2 by the smoke countdown
};
constexpr int8_t kMachineCalm = 0, kMachineHit = 1, kMachineDestroyed = 2;
// A smoke column on the machine (stage static type 0x26): its owner's slot, put out, its owner's type
struct SmokeColumnParams {
    explicit SmokeColumnParams(const StaticActor& actor) : SmokeColumnParams(const_cast<StaticActor&>(actor)) {}
    explicit SmokeColumnParams(StaticActor& actor) : ownerSlot(actor.params[0]), putOut(actor.params[1]), ownerType(actor.params[2]) {}
    uint8_t& ownerSlot;
    uint8_t& putOut;
    uint8_t& ownerType;
};
// a smoke column's place on the machine (exe data, 8 bytes): its z and y, its yaw
struct SmokeColumnPlace : DataRecord {
    int16_t z = 0;
    int16_t yaw = 0;
    int16_t y = 0;
    static SmokeColumnPlace load(const TableReader& r) {
        SmokeColumnPlace rec;
        rec.z = r.s16(0x0);
        rec.yaw = r.s16(0x2);
        rec.y = r.s16(0x4);
        return rec;
    }
};
const DataTable<RecordTable<SmokeColumnPlace, 4, 8>> smokeColumnPlaces{TableId::RepairMachineSmokeColumnPlaces};
const DataTable<ShortPoint> deathExplosionOffset{TableId::RepairMachineDeathExplosionOffset}; // exe data 0x896100
const DataTable<CollisionBoxRecord> machineBox{TableId::RepairMachineBox};
constexpr uint8_t kSmokeColumnType = 0x26, kMachineType = 0x5e, kRollType = 0x60;
constexpr uint8_t kRepairGaugeType = 0xa, kRepairGaugeVariant = 3;
constexpr uint16_t kHurtSound = 0x8d, kStrongHurtSound = 0x8e;
constexpr uint16_t kSmokeTicksLow = 0xc8, kSmokeTicksHigh = 0x190;
constexpr uint32_t kFlagMachineDestroyed = 0x2f;
constexpr uint16_t kCapsuleRadius = 0x18;
constexpr uint32_t kMachineSolidAttack = kHitAttackHarmless | kHitAttackSolid;

// exe 0x4e4860(actor, block, place, k): the smoke column k at its place, once (its slot kept); the slot (0: none)
uint32_t machineSmoke(const AnimatedActor& actor, MachineFields& fields, size_t k) {
    if (fields.smokeSlots[k] != 0) return 0;
    const uint32_t e = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (e != 0) {
        const SmokeColumnPlace place = smokeColumnPlaces[k];
        StaticActor& smoke = staticActorAt(e);
        auto&& params = SmokeColumnParams(staticActorAt(e));
        smoke.flags = kActorLive;
        smoke.loopFlags |= kActorSweepGroup2;
        smoke.type = kSmokeColumnType;
        smoke.subtype = 0;
        smoke.variant = 0;
        params.ownerSlot = actor.slot;
        params.putOut = 0;
        params.ownerType = kMachineType;
        smoke.pos.x.whole = 0;
        smoke.pos.y.whole = place.y;
        smoke.pos.z.whole = place.z;
        smoke.rotation.y = place.yaw;
        fields.smokeSlots[k] = smoke.slot;
    }
    return e;
}
// exe 0x4e48e0(block, k): the smoke column k put out while it is still a live stage type 0x26
void machineSmokeOut(MachineFields& fields, size_t k) {
    const uint8_t slot = fields.smokeSlots[k];
    if (slot == 0) return;
    const StaticActor& smoke = staticActorAt(staticPoolSlot(slot));
    if ((smoke.flags & kActorLive) && smoke.type == kSmokeColumnType && (smoke.loopFlags & kActorStageClass)) {
        SmokeColumnParams(smoke).putOut = 1;
        fields.smokeSlots[k] = 0;
    }
}
// exe 0x4e4370: the model (scenery-drawn), its row's hit points * 2, on the ground; Roll and the repair gauge spawned,
// the HUD's target gauge
void machineInit(const PlayerEnv& env, AnimatedActor& actor) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);           // exe 0x5e1490
        return;
    }
    actor.flags |= kActorScenery;
    actor.anim = 0;
    actor.prevAnim = kNoAnimShown;
    const int16_t hp =
        hasAnimatedStats(actor.type) ? int16_t(uint32_t(animatedStats(actor.type).hitPoints) * 2u) : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.collisionBox.pointAt(machineBox.stateAddress());
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.pos.y.whole = int16_t(actorGround(env, actor)); // exe 0x40f190
    actor.clearClassBlock();                              // exe 0x40dc40
    if (const uint32_t r = allocActor20()) {              // exe 0x5e1160: Roll at the machine
        AnimatedActor& roll = animatedActorAt(r);
        roll.loopFlags |= kActorSweepGroup2;
        roll.flags = kActorLive;
        roll.type = kRollType;
        roll.subtype = 0;
        roll.variant = 0;
        roll.pos = actor.pos;
        roll.params[0] = actor.slot;
    }
    if (const uint32_t g = allocActorE0()) { // exe 0x5e1440: the ST18 spawner variant 3 (the repair gauge)
        ControllerActor& gauge = controllerActorAt(g);
        gauge.flags = kActorLive;
        gauge.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
        gauge.type = kRepairGaugeType;
        gauge.variant = kRepairGaugeVariant;
        gauge.params[0] = actor.slot;
    }
    hudAddTargetGauge(actor, 0, 3); // exe 0x415e30(a, 0, 3)
    actor.enterState(uint8_t(MachineState::Run));
}
// exe 0x4e44d0: the water puts the smoke out faster; a hit (after the cooldown) flashes it, costs hit points and by the
// damage stage starts the smoke columns (a strong hit, or a new stage, the reaction); destroyed -> the end. Then the
// smoke countdown: the upper columns out below 0xc8, all out at 0
void machineHits(AnimatedActor& actor, MachineFields& fields) {
    MachineParams params(actor);
    const uint32_t v = fields.hit.result, d = v & kHitDamageMask;
    if (v & kHitAttackWater) fields.smokeTicks -= 0xfu;
    if (fields.hitCooldown == 0 && d != 0 && (v & (kHitAttackSolid | kHitAttackByPlayer))) {
        actor.tint = kTintFlash;
        fields.hitCooldown = 0xf;
        uint16_t sound = kHurtSound;
        bool react = (v & (kHitAttackStrongPush | kHitAttackThrown)) != 0, dead = false;
        if (hitPointsAdd(actor, -int(d)) != 0) { // exe 0x5e52f0
            react = false;
            dead = true;
        }
        const int hp = actor.hitPoints, mx = actor.maxHitPoints;
        bool newStage = false;
        switch (fields.damageStage) {
        case 0:
            if (hp < mx - (mx >> 2)) {
                machineSmoke(actor, fields, 0);
                machineSmoke(actor, fields, 1);
                fields.smokeTicks = int16_t(kSmokeTicksLow);
                fields.damageStage = 1;
                newStage = true; // the exe jumps into the reaction block
            }
            break;
        case 1:
            machineSmoke(actor, fields, 0);
            machineSmoke(actor, fields, 1);
            if (hp < (mx >> 1)) {
                machineSmoke(actor, fields, 2);
                machineSmoke(actor, fields, 3);
                fields.smokeTicks = int16_t(kSmokeTicksHigh);
                fields.damageStage = 2;
                react = true;
            }
            fields.smokeTicks = int16_t(kSmokeTicksLow);
            break;
        case 2:
            for (size_t k = 4; k-- > 0;) machineSmoke(actor, fields, k);
            fields.smokeTicks = int16_t(kSmokeTicksHigh);
            break;
        default: break;
        }
        if (react || newStage) {
            sound = kStrongHurtSound;
            params.event = kMachineHit;
            actor.subState = uint8_t(MachineSub::React);
            actor.restartStep();
        }
        soundPlayAtPosS16(sound, fields.hit.contact); // exe 0x5d5420
        if (dead) {
            actor.enterState(uint8_t(MachineState::Destroyed));
            return;
        }
    }
    const uint16_t t = uint16_t(fields.smokeTicks);
    if (t == 0) return;
    const int16_t n = int16_t(t - 1u);
    fields.smokeTicks = n;
    if (n <= 0) {
        machineSmokeOut(fields, 3);
        machineSmokeOut(fields, 2);
        machineSmokeOut(fields, 1);
        machineSmokeOut(fields, 0);
        fields.smokeTicks = 0;
        return;
    }
    if (n < int16_t(kSmokeTicksLow)) {
        machineSmokeOut(fields, 3);
        machineSmokeOut(fields, 2);
    }
}
// exe 0x4e4700: animation 3 once, back to the hits at its end
void machineReact(AnimatedActor& actor) {
    if (actor.step == 0) {
        actor.anim = 3;
        actor.enterStep(1);
        MachineParams(actor).event = kMachineCalm;
        return;
    }
    if (actor.animEnded()) {
        actor.anim = 0;
        actor.subState = uint8_t(MachineSub::Hits);
        actor.restartStep();
    }
}
// exe 0x4e4740: two death explosions, 0x3c ticks, then a blast a tick for 2 ticks; flag 0x2f, freed
void machineDestroyed(AnimatedActor& actor, MachineFields& fields) {
    switch (MachineEnd(actor.subState)) {
    case MachineEnd::Explode:
        for (int k = 2; k > 0; --k) {
            if (const uint32_t e = allocActor60(0)) { // exe 0x5e1300
                StaticActor& explosion = staticActorAt(e);
                auto&& params = ExplosionParams(actorHeaderAt(e));
                explosion.flags = kActorLive;
                explosion.type = typeByte(GlobalStaticType::Explosion);
                explosion.subtype = 0;
                explosion.variant = 0;
                explosion.pos.x.whole =
                    int16_t(uint16_t(actor.pos.x.whole) + uint16_t(deathExplosionOffset->x));
                explosion.pos.y.whole =
                    int16_t(uint16_t(actor.pos.y.whole) + uint16_t(deathExplosionOffset->y));
                params.spread = 0x100;
                params.flashes = 0x3c;
                explosion.pos.z.whole =
                    int16_t(uint16_t(actor.pos.z.whole) + uint16_t(deathExplosionOffset->z));
            }
        }
        fields.endTicks = 0x3c;
        actor.subState += 1u;
        return;
    case MachineEnd::Wait:
        fields.endTicks -= 1u;
        if (fields.endTicks == 0) {
            MachineParams(actor).event = kMachineDestroyed;
            actor.subState += 1u;
            fields.endTicks = 3;
        }
        return;
    case MachineEnd::Blasts:
        fields.endTicks -= 1u;
        if (fields.endTicks == 0) {
            setStoryFlag(kFlagMachineDestroyed);
            freeActor(actor); // exe 0x5e1490
            return;
        }
        if (const uint32_t e = allocActor60(0)) {
            StaticActor& blast = staticActorAt(e);
            auto&& params = BlastParams(actorHeaderAt(e));
            blast.loopFlags |= kActorSweepGroup2;
            blast.flags = kActorLive;
            blast.type = typeByte(GlobalStaticType::Blast);
            blast.subtype = 0;
            blast.variant = 0;
            blast.pos = actor.pos;
            params.attack = 0;
            blast.pos.y.whole -= 0xb4u;
            params.size = 0x120;
        }
        return;
    default: return;
    }
}
// exe 0x4e41c0
void machine(const PlayerEnv& env, AnimatedActor& actor) {
    MachineFields& fields = classState<MachineFields>(actor);
    savePose(actor);
    switch (MachineState(actor.state)) { // [0x8960cc]
    case MachineState::Init: machineInit(env, actor); break;
    case MachineState::Run: { // exe 0x4e4480
        switch (MachineSub(actor.subState)) { // [0x8960d8]
        case MachineSub::Hits: machineHits(actor, fields); break;
        case MachineSub::React: machineReact(actor); break;
        default: notPortedAt(kStoryPort, "0x4e41c0 sub-state", actor.subState);
        }
        const int ticks = fields.smokeTicks;
        MachineParams(actor).hitState = int8_t(ticks > int(kSmokeTicksLow) ? 2 : (ticks != 0 ? 1 : 0));
        if (fields.hitCooldown != 0) fields.hitCooldown -= 1u;
        break;
    }
    case MachineState::Destroyed: machineDestroyed(actor, fields); break;
    default: notPortedAt(kStoryPort, "0x4e41c0 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    animate(actor);
    fields.hit.result = 0;
    fields.hit.previousX = actor.prevPos.x.whole;
    fields.hit.previousZ = actor.prevPos.z.whole;
    const uint32_t owner = hitOwner(actor.slot);
    const ShortVec4& top = bonePoint(actor, 2, kStoryPort), base = bonePoint(actor, 1, kStoryPort); // exe 0x5e1810
    const int16_t from[3] = {base.x, base.y, base.z}, to[3] = {top.x, top.y, top.z};
    hitListSubmitCapsule(from, kCapsuleRadius, to, kCapsuleRadius, fields.hit,
                         owner | hitGroup(kHitGroupBodies) | kHitNoLockOn | kHitContactPoint | hitCapsule(2),
                         kMachineSolidAttack); // exe 0x5e4190
    int16_t dx, dz;
    angleVector(int(int16_t(uint16_t(actor.rotation.y) + 0xc00u)), 0x40, dx, dz); // exe 0x5e40c0
    const int16_t side[3] = {int16_t(dx + actor.pos.x.whole), actor.pos.y.whole, int16_t(dz + actor.pos.z.whole)};
    hitListSubmit(side, fields.hit,
                  owner | hitGroup(kHitGroupLiftable) | kHitNoLockOn | kHitContactPoint | hitRadius(0xb4),
                  kMachineSolidAttack); // exe 0x5e4120
    angleVector(actor.rotation.y, 0x80, dx, dz);
    const ShortVec4& head = bonePoint(actor, 2, kStoryPort);
    const int16_t front[3] = {int16_t(dx + head.x), head.y, int16_t(dz + head.z)};
    hitListSubmit(front, fields.hit,
                  owner | hitGroup(kHitGroupLiftable) | kHitNoLockOn | kHitContactPoint | hitRadius(0xc0),
                  kMachineSolidAttack);
    actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0); // exe 0x414050(a, 1, 0xffff, 0)
}

// ---- 0x4e67c0: Roll at the machine. States [0x896274]; the working state's sub-states [0x896280].
enum class RollState : uint8_t { Init = 0, Work = 1, Thrown = 2 };
enum class RollSub : uint8_t { Work = 0, Startled = 1 };
// the work (exe 0x4e6a00 by the step)
enum class RollWork : uint8_t { Start = 0, Loop = 1, Tool = 2, Finish = 3 };
// startled (exe 0x4e6b00 by the step)
enum class RollStartle : uint8_t { Jump = 0, Look = 1, Settle = 2 };
// thrown (exe 0x4e6b90 by the sub-state)
enum class RollFall : uint8_t { Launch = 0, Fall = 1 };
constexpr uint8_t kRollFree = 0x01;           // not placed at the machine
constexpr uint8_t kRollIgnoresMachine = 0x02; // the machine's events not read (its setter is not traced)
// her work loops and her next animations, by the machine's hit state (16 random picks each)
const DataTable<ScalarTable<uint8_t, 0x30>> rollWorkLoops{TableId::RepairMachineRollWorkLoops};
const DataTable<ScalarTable<uint8_t, 0x30>> rollWorkAnims{TableId::RepairMachineRollWorkAnims};
const DataTable<CollisionBoxRecord> rollBox{TableId::RepairMachineRollBox};
constexpr size_t kRollHiddenPart = 3, kRollToolPart = 11, kRollHandPart = 12;
constexpr uint8_t kToolAnimA = 4, kToolAnimB = 0xc;

size_t rollPick(const AnimatedActor& machine, uint32_t r) {
    return size_t(int32_t(MachineParams(machine).hitState) * 16 + int32_t(r & 0xf));
}
// exe 0x4e6900
void rollInit(AnimatedActor& actor, RollFields& fields) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    actor.flags |= kActorDrawn;
    actor.anim = 0;
    actor.velocity.clearWords();
    actor.spin.setXY(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.prevAnim = kNoAnimShown;
    actor.maxHitPoints = 1;
    actor.hitPoints = 1;
    actor.collisionBox.pointAt(rollBox.stateAddress());
    actor.parts[kRollHiddenPart].flags |= kPartHidden;
    actor.clearClassBlock(); // exe 0x40dc40
    pointAtWord(fields.machine, animatedPoolSlot(int8_t(actor.params[0])));
    actor.enterState(uint8_t(RollState::Work));
}
// exe 0x4e6a00: work animations picked by the machine's hit state
void rollWork(AnimatedActor& actor, RollFields& fields) {
    const AnimatedActor& machine = *fields.machine;
    switch (RollWork(actor.step)) {
    case RollWork::Start: {
        fields.flags = 0;
        actor.anim = 1;
        const uint32_t r = rngStep();
        fields.workLoops = rollWorkLoops[rollPick(machine, r)];
        actor.enterStep(uint8_t(RollWork::Loop));
        return;
    }
    case RollWork::Loop: {
        if (!actor.animEnded()) return;
        fields.workLoops -= 1u;
        if (fields.workLoops != 0) return;
        const uint32_t r = rngStep();
        const uint8_t anim = rollWorkAnims[rollPick(machine, r)];
        actor.anim = anim;
        actor.enterStep(uint8_t(anim == kToolAnimA || anim == kToolAnimB ? RollWork::Tool : RollWork::Finish));
        return;
    }
    case RollWork::Tool:
        if (actor.animKey.events & 1) fields.toolShown ^= 1u;
        [[fallthrough]];
    case RollWork::Finish:
        if (actor.animEnded()) actor.restartStep();
        return;
    default: return;
    }
}
// exe 0x4e6b00: animation 7, then 5 / 6 (+8 while the machine is hit), back to work
void rollStartled(AnimatedActor& actor, RollFields& fields) {
    switch (RollStartle(actor.step)) {
    case RollStartle::Jump:
        actor.anim = 7;
        actor.enterStep(uint8_t(RollStartle::Look));
        return;
    case RollStartle::Look: {
        if (!actor.animEnded()) return;
        const uint32_t r = rngStep();
        uint8_t anim = uint8_t((r & 1) ? 6 : 5);
        if (MachineParams(*fields.machine).hitState != 0) anim = uint8_t(anim + 8);
        actor.anim = anim;
        actor.enterStep(uint8_t(RollStartle::Settle));
        return;
    }
    case RollStartle::Settle:
        if (actor.animEnded()) {
            actor.subState = uint8_t(RollSub::Work);
            actor.restartStep();
        }
        return;
    default: return;
    }
}
// exe 0x4e6b90: animation 8, free of the machine, thrown up (-0x500, + 0x40 a tick) until she lands
void rollThrown(const PlayerEnv& env, AnimatedActor& actor, RollFields& fields) {
    if (RollFall(actor.subState) == RollFall::Launch) {
        actor.anim = 8;
        fields.flags = kRollFree;
        actor.velocity.y = -0x500;
        actor.subState += 1u;
    } else if (RollFall(actor.subState) != RollFall::Fall) {
        return;
    }
    actor.velocity.y += 0x40u;
    integrateVelocity(actor, 0, actor.velocity.y, 0);                     // exe 0x5e3690
    if (int32_t(actorTestMove(env, actor)) < 0) actor.subState += 1u;    // exe 0x40f570
}
// exe 0x4e67c0
void roll(const PlayerEnv& env, AnimatedActor& actor) {
    RollFields& fields = classState<RollFields>(actor);
    savePose(actor);
    switch (RollState(actor.state)) { // [0x896274]
    case RollState::Init: rollInit(actor, fields); break;
    case RollState::Work: { // exe 0x4e69b0: the machine's event (hit -> startled, destroyed -> thrown)
        if (!(fields.flags & kRollIgnoresMachine)) {
            const int event = MachineParams(*fields.machine).event;
            if (event == kMachineHit) {
                actor.subState = uint8_t(RollSub::Startled);
                actor.restartStep();
            } else if (event == kMachineDestroyed) {
                actor.enterState(uint8_t(RollState::Thrown));
                rollThrown(env, actor, fields);
                break;
            }
        }
        switch (RollSub(actor.subState)) { // [0x896280]
        case RollSub::Work: rollWork(actor, fields); break;
        case RollSub::Startled: rollStartled(actor, fields); break;
        default: notPortedAt(kStoryPort, "0x4e67c0 sub-state", actor.subState);
        }
        break;
    }
    case RollState::Thrown: rollThrown(env, actor, fields); break;
    default: notPortedAt(kStoryPort, "0x4e67c0 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (fields.toolShown == 0) {
        actor.parts[kRollToolPart].flags &= uint8_t(~kPartHidden);
        actor.parts[kRollHandPart].flags |= kPartHidden;
    } else {
        actor.parts[kRollToolPart].flags |= kPartHidden;
        actor.parts[kRollHandPart].flags &= uint8_t(~kPartHidden);
    }
    if (!(fields.flags & kRollFree)) placeOn(actor, *fields.machine, 0xd00, 0x40, 0x40); // at the machine
    animate(actor);
}

// ---- 0x4e4930: the machine's other parts on ST18 sub 2. States [0x89611c]; the cues by the variant [0x89612c]: 0
// animation 2 at the step tick 0x140, 1 sinks 4 a tick to -0xd60 then shakes and settles, the rest nothing. Variant 1
// also carries a stage static type 0x3d riding 0x80 to the side; variants 0 / 1 / 2 / 4 / 5 a prop (type 2) at its
// place.
enum class PartState : uint8_t { Init = 0, Cue = 1 };
enum class PartVariant : uint8_t { Lift = 0, Sink = 1, Still2 = 2, Still3 = 3, Still4 = 4, Still5 = 5 };
// the sink (exe 0x4e4b70 by the sub-state)
enum class PartSink : uint8_t { Sink = 0, Shake = 1, Settled = 2 };
const DataTable<ScalarTable<uint8_t, 6>> partAnims{TableId::RepairMachinePartAnims};
const DataTable<CollisionBoxRecord> partBox{TableId::RepairMachinePartBox};
constexpr uint8_t kPropType = 2, kRiderType = 0x3d;
constexpr int32_t kSunkY = int32_t(0xf2a00000u); // -0xd60 << 16
constexpr int16_t kSunkWhole = -0xd60;

// a prop (pool 0x20 type 2 of the variant) at the part's place
void spawnPartProp(const AnimatedActor& actor, uint8_t variant) {
    const uint32_t e = allocActor20(); // exe 0x5e1160
    if (e == 0) return;
    AnimatedActor& prop = animatedActorAt(e);
    prop.flags = kActorLive;
    prop.type = kPropType;
    prop.subtype = 0;
    prop.loopFlags |= kActorSweepGroup2;
    prop.variant = variant;
    prop.pos = actor.pos;
    prop.params[0] = actor.slot;
}
// exe 0x4e49c0
void partInit(AnimatedActor& actor, PartFields& fields) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);           // exe 0x5e1490
        return;
    }
    actor.flags |= kActorScenery;
    actor.anim = partAnims[actor.variant];
    actor.prevAnim = kNoAnimShown;
    actor.maxHitPoints = 1;
    actor.hitPoints = 1;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.collisionBox.pointAt(partBox.stateAddress());
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock(); // exe 0x40dc40
    switch (PartVariant(actor.variant)) {
    case PartVariant::Sink:
        if (const uint32_t s = allocActor60(kActorStageClass)) { // exe 0x5e12f0
            StaticActor& rider = staticActorAt(s);
            rider.flags = kActorLiveDrawn;
            rider.type = kRiderType;
            rider.subtype = 0;
            rider.variant = 0;
            rider.pos = actor.pos;
            fields.rider = s;
        }
        spawnPartProp(actor, actor.variant);
        break;
    case PartVariant::Lift:
    case PartVariant::Still2: spawnPartProp(actor, actor.variant); break;
    case PartVariant::Still4: spawnPartProp(actor, 3); break;
    case PartVariant::Still5: spawnPartProp(actor, 4); break;
    default: break;
    }
    actor.enterState(uint8_t(PartState::Cue));
}
// exe 0x4e4b70 (variant 1): sinks 4 a tick until below -0xd60 (a camera shake), shakes 0xf ticks, settles at -0xd60;
// its rider kept 0x80 to the side
void partSink(AnimatedActor& actor, PartFields& fields) {
    switch (PartSink(actor.subState)) {
    case PartSink::Sink:
        actor.pos.y = int32_t(uint32_t(actor.pos.y) - 0x40000u);
        if (actor.pos.y < kSunkY) {
            cameraShake(true, 0x200, 0x20); // exe 0x40ea80
            fields.shakeTicks = 0xf;
            actor.subState = uint8_t(PartSink::Shake);
        }
        break;
    case PartSink::Shake: {
        const int8_t v = fields.shakeTicks;
        actor.pos.y.whole = int16_t(uint16_t(uint32_t(((v & 1) ? int32_t(v) : 0) >> 2) - 0xd60u));
        fields.shakeTicks -= 1u;
        if (fields.shakeTicks == 0) {
            actor.pos.y.whole = kSunkWhole;
            actor.subState = uint8_t(PartSink::Settled);
        }
        break;
    }
    default: break;
    }
    if (const uint32_t s = fields.rider) {
        StaticActor& rider = staticActorAt(s);
        rider.pos.x = int32_t(uint32_t(actor.pos.x) + 0x800000u);
        rider.pos.z = actor.pos.z;
        rider.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) + 1u);
    }
}
// exe 0x4e4930
void machinePart(AnimatedActor& actor) {
    PartFields& fields = classState<PartFields>(actor);
    savePose(actor);
    switch (PartState(actor.state)) { // [0x89611c]
    case PartState::Init: partInit(actor, fields); break;
    case PartState::Cue: // exe 0x4e4b30: [0x89612c]
        switch (PartVariant(actor.variant)) {
        case PartVariant::Lift:
            if (stepTick() == 0x140) actor.anim = 2;
            break;
        case PartVariant::Sink: partSink(actor, fields); break;
        case PartVariant::Still2:
        case PartVariant::Still3:
        case PartVariant::Still4:
        case PartVariant::Still5: break; // exe 0x52ef00 (ret)
        default: notPortedAt(kStoryPort, "0x4e4930 variant", actor.variant);
        }
        break;
    default: notPortedAt(kStoryPort, "0x4e4930 state", actor.state);
    }
    if (actor.flags & kActorLive) animate(actor);
}

// ---- 0x44a700: the prop riding a machine part (its slot in the spawn's first parameter): placed at the part + Ry(its
// yaw) (0xd00, 0x40, 0x40) (variants 3 / 4: z -0x80) unless detached; texture tracks 0x88e484 / 0x88e48c; animation
// [0x88e4a4 + variant]. States [0x88e49c]; the cues by the variant [0x88e4ac] on the script's step tick / step sub.
enum class PropState : uint8_t { Init = 0, Cue = 1 };
// the variant 3's cues (exe 0x44aba0 by the sub-state [0x88e4c0])
enum class PropAct : uint8_t { Wait = 0, Talk = 1, Turn = 2 };
const DataTable<ScalarTable<uint8_t, 5>> propAnims{TableId::RepairMachinePropAnims};
const DataTable<CollisionBoxRecord> propBox{TableId::RepairMachinePropBox};
constexpr uint8_t kFrozenPropVariant = 4;

// exe 0x44a980 (variant 0): released by the script, frozen past 0xd6, animations 2 / 3 at 0xe0 / 0xf5, the mouth's
// sequence 3 at 0x120
void propCues0(AnimatedActor& actor, PropFields& fields) {
    switch (actor.step) {
    case 0:
        fields.detached = 0;
        actor.anim = 1;
        if (scriptState().part<ScriptCastStart>().started == 0) {
            fields.frozen = 1;
            return;
        }
        fields.frozen = 0;
        actor.step += 1u;
        [[fallthrough]];
    case 1:
        if (stepTick() > 0xd6) {
            fields.frozen = 1;
            actor.step += 1u;
            fields.tracks.first.select(1, 0);  // exe 0x5e33d0
            fields.tracks.second.select(1, 0); // exe 0x5e33f0
        }
        break;
    case 2:
        if (stepTick() == 0xe0) {
            fields.frozen = 0;
            actor.anim = 2;
            actor.step += 1u;
        }
        break;
    case 3:
        if (stepTick() == 0xf5) {
            fields.tracks.second.select(1, 0);
            actor.anim = 3;
            actor.step += 1u;
        }
        break;
    case 4:
        if (stepTick() == 0x120) {
            fields.tracks.second.select(3, 0);
            actor.step += 1u;
        }
        break;
    default: break;
    }
}
// exe 0x44aa90 (variant 2): the mouth's sequence 2 at the step ticks 0x3c / 0x70 / 0xbe, 0 at 0x67 / 0xb4 / 0xeb
void propCues2(PropFields& fields) {
    switch (stepTick()) {
    case 0x3c:
    case 0x70:
    case 0xbe: fields.tracks.second.select(2, 0); break;
    case 0x67:
    case 0xb4:
    case 0xeb: fields.tracks.second.select(0, 0); break;
    default: break;
    }
}
// exe 0x44aba0 (variant 3): by the step sub (3: talks, 4: turns at 0x190)
void propCues3(AnimatedActor& actor, PropFields& fields) {
    const uint8_t sub = scriptState().stepSub;
    switch (PropAct(actor.subState)) {
    case PropAct::Wait: // exe 0x44abc0
        if (sub == 3) {
            actor.subState = uint8_t(PropAct::Talk);
            actor.restartStep();
        }
        break;
    case PropAct::Talk: // exe 0x44abe0
        if (actor.step == 0) {
            fields.frozen = 1;
            actor.step += 1u;
            actor.anim = 5;
            fields.tracks.first.select(1, 0);
            fields.tracks.second.select(1, 0);
        } else if (actor.step == 1 && stepTick() == 0x24) {
            fields.tracks.second.select(0, 0);
            actor.step += 1u;
        }
        if (scriptState().stepSub == 4) {
            actor.subState = uint8_t(PropAct::Turn);
            actor.restartStep();
        }
        break;
    case PropAct::Turn: // exe 0x44ac60
        if (actor.step == 0) {
            fields.frozen = 0;
            actor.step = 1;
        } else if (actor.step == 1 && stepTick() == 0x190) {
            actor.anim = 6;
            actor.step = 2;
        }
        break;
    default: notPortedAt(kStoryPort, "0x44a700 variant 3 sub-state (table 0x88e4c0 has 3 entries)", actor.subState);
    }
}
// exe 0x44a870
void propInit(AnimatedActor& actor, PropFields& fields) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);           // exe 0x5e1490
        return;
    }
    actor.flags |= kActorDrawn;
    actor.anim = propAnims[actor.variant];
    actor.prevAnim = kNoAnimShown;
    actor.maxHitPoints = 1;
    actor.hitPoints = 1;
    actor.velocity.clearWords();
    actor.spin.setXY(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.collisionBox.pointAt(propBox.stateAddress());
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock(); // exe 0x40dc40
    pointAtWord(fields.part, animatedPoolSlot(int8_t(actor.params[0])));
    fields.tracks.init(stateAddress(TableId::RepairMachinePropCells), stateAddress(TableId::RepairMachinePropSecondCells)); // exe 0x5e33a0
    if (actor.variant != kFrozenPropVariant) {
        fields.tracks.first.select(0, 0);  // exe 0x5e33d0
        fields.tracks.second.select(0, 0); // exe 0x5e33f0
    } else {
        fields.frozen = 1;
    }
    actor.enterState(uint8_t(PropState::Cue));
}
// exe 0x44a700
void machinePartProp(AnimatedActor& actor) {
    PropFields& fields = classState<PropFields>(actor);
    savePose(actor);
    switch (PropState(actor.state)) { // [0x88e49c]
    case PropState::Init: propInit(actor, fields); break;
    case PropState::Cue: // [0x88e4ac]
        switch (actor.variant) {
        case 0: propCues0(actor, fields); break;
        case 1: break; // exe 0x52ef00 (ret)
        case 2: propCues2(fields); break;
        case 3: propCues3(actor, fields); break;
        case 4: // exe 0x44aca0
            if (stepTick() == 0xa0) fields.frozen = 0;
            break;
        default: notPortedAt(kStoryPort, "0x44a700 variant (table 0x88e4ac has 5 entries)", actor.variant);
        }
        break;
    default: notPortedAt(kStoryPort, "0x44a700 state (table 0x88e49c has 2 entries)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (!(fields.detached & 1)) {
        const AnimatedActor& part = *fields.part;
        if (actor.variant <= 2) placeOn(actor, part, 0xd00, 0x40, 0x40);
        else if (actor.variant <= 4) placeOn(actor, part, 0xd00, 0x40, -0x80);
        else notPortedAt(kStoryPort, "0x44a700 variant offset (the exe reads stack garbage)", actor.variant);
    }
    textureTracksTick(actor, fields.tracks); // exe 0x5e34f0
    if (actor.anim != actor.prevAnim) animSetPose(actor, actor.anim, 0); // exe 0x5e1c30
    else if (fields.frozen == 0) animStepPose(actor);                    // exe 0x5e1e80
    else animDecodeCurrent(actor);                                       // exe 0x5e1f30
}

} // namespace

void repairMachineUpdate(const PlayerEnv& env, uint32_t actor) { machine(env, animatedActorAt(actor)); }
void repairRollUpdate(const PlayerEnv& env, uint32_t actor) { roll(env, animatedActorAt(actor)); }
void machinePartUpdate(const PlayerEnv&, uint32_t actor) { machinePart(animatedActorAt(actor)); }
void machinePartPropUpdate(const PlayerEnv&, uint32_t actor) { machinePartProp(animatedActorAt(actor)); }

} // namespace od::port
