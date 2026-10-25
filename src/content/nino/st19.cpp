// ST19 actor classes, one routine per exe routine.
//
// The scripted machinery 0x4e7d20 (pool 0x20: ST19). Its variant picks what it does ([0x896400]): 0 drops from above
// while the script's step sub is 1 (to y -0x880, four dust effects), shudders, swings round to yaw 0x800, rattles,
// opens (animation 1, two bursts + a flying spark (pool e0 type 0x1c) from bone 1) and shakes the camera on frame 2 of
// it; 1 waits for the script's tick 0x73, slides to x -0x300 (shake), swings to yaw 0x100 tilting part 1 back (its
// tilt added to the decoded frame every tick), rattles, opens likewise; 2 rises to y -0xc00 and shudders; 3 stands,
// solid. Variants 0 / 2 carry a pool-0x60 type-1 companion (a still solid model) on them.
#include "content/nino/st19.hpp"
#include "content/nino/st19_state.hpp"
#include "content/nino/fly_point.hpp"

#include "engine/data_source.hpp"
#include "game/patched_data.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/camera.hpp"
#include "game/vectors.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/table_records.hpp"

namespace od::port {

using namespace st19_state;

namespace {


// [0x8963f8]
enum class MachineState : uint8_t {
    Init = 0,  // exe 0x4e7de0
    Run = 1,   // exe 0x4e7f30: by the variant
    Drop = 2,  // the variants' routines entered directly (not reached from the spawn)
    Slide = 3,
    Rise = 4,
    Idle = 5,
};
// the variants ([0x896400])
enum class MachineVariant : uint8_t { Drop = 0, Slide = 1, Rise = 2, Stand = 3 };
// the drop's sub-states ([0x4e8180])
enum class DropSub : uint8_t { Fall = 0, Shudder = 1, Swing = 2, Rattle = 3, Wait = 4, Opening = 5, Open = 6 };
// the slide's sub-states ([0x4e83ec])
enum class SlideSub : uint8_t {
    WaitCue = 0, Slide = 1, Settle = 2, Swing = 3, Rattle = 4, Wait = 5, Opening = 6, Open = 7
};
// the rise's sub-states
enum class RiseSub : uint8_t { Rise = 0, Shudder = 1, Done = 2 };

// One dust effect's place relative to the machine and its parameter (exe data, 8 bytes)
struct DustPlace : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    ShortVec3 offset; // +0x0
    uint16_t param = 0; // +0x6
    template <class R> static DustPlace load(const R& r) {
        DustPlace v;
        v.offset = {r.template read<int16_t>(0x0), r.template read<int16_t>(0x2), r.template read<int16_t>(0x4)};
        v.param = r.template read<uint16_t>(0x6);
        return v;
    }
};

const DataTable<CollisionBoxRecord> machineBox{TableId::St19MachineBox};
// the bursts' offsets from bone 1 (exe data 0x8963c8 / 0x8963d0)
const DataTable<ShortPoint> burstOffsetA{TableId::St19BurstOffsetA}, burstOffsetB{TableId::St19BurstOffsetB};
constexpr TableId kDropDust[4] = {TableId::St19DropDust0, TableId::St19DropDust1, TableId::St19DropDust2, TableId::St19DropDust3};
constexpr uint8_t kEffectType = 3;     // the stage's pool-0xa0 effect class
constexpr uint8_t kDustVariant = 0x15, kBurstVariant = 0x17;
constexpr uint8_t kCompanionType = 1; // the pool-0x60 still model (content/nino/st18_props)
constexpr uint8_t kAnimOpen = 1, kAnimRisen = 2;
constexpr uint8_t kOpenShakeFrame = 2;
constexpr uint16_t kShudderTicks = 0xf, kRattleTicks = 8;
constexpr int32_t kDropTop = int32_t(0xf7800000), kDropStep = int32_t(0xfffa0000);
constexpr int32_t kRiseTop = int32_t(0xf4000000), kRiseStep = 0x60000;
constexpr int16_t kDropY = -0x880, kRiseY = -0xc00;
constexpr int16_t kDropYaw = 0x800, kSlideYaw = 0x100;
constexpr int16_t kSlideEndX = -0x300, kSlideMaxSpeed = -0xc0;
constexpr int16_t kSlideTiltStart = 0x280, kSlideTiltStep = 0x18, kSlideTilt = 0x156;
constexpr int32_t kCompanionAbove = 0x180000, kCompanionBelow = -0x80000;
constexpr int32_t kSlideCueTick = 0x73;

// exe 0x4e81a0: a dust effect at the machine's position + the place's offset
void dustAt(AnimatedActor& actor, const DustPlace& place, uint8_t variant) {
    const uint32_t e = allocActorA0(0); // exe 0x5e13b0
    if (!e) return;
    EffectActor& effect = effectActorAt(e);
    effect.variant = variant;
    effect.flags = kActorLiveDrawn;
    effect.type = kEffectType;
    effect.pos.x = int16_t(uint16_t(actor.pos.x.whole) + uint16_t(place.offset.x));
    effect.pos.y = int16_t(uint16_t(actor.pos.y.whole) + uint16_t(place.offset.y));
    effect.pos.z = int16_t(uint16_t(actor.pos.z.whole) + uint16_t(place.offset.z));
    effect.setParamsLowWord(place.param);
}

// exe 0x4e84c0: an effect at bone 1 offset by `offset`, facing the machine's yaw
void effectAtBone(AnimatedActor& actor, uint8_t variant, const ShortPoint& offset) {
    const int16_t v[3] = {offset.x, offset.y, offset.z};
    int16_t p[3];
    bonePointOffset(actor, 1, v, p); // exe 0x5e1970(a, 1, buf, buf) on a stack copy
    const uint32_t e = allocActorA0(0);
    if (!e) return;
    EffectActor& effect = effectActorAt(e);
    effect.flags = kActorLiveDrawn;
    effect.type = kEffectType;
    effect.variant = variant;
    effect.pos.x = p[0];
    effect.pos.y = p[1];
    effect.pos.z = p[2];
    effect.setParamsLowWord(uint16_t(actor.rotation.y));
}

// the opening: animation 1, two bursts at bone 1 and a flying spark from bone 1 to the target offset `sparkTo` in
// `ticks`
void openWithBursts(AnimatedActor& actor, uint32_t sparkTo, uint16_t ticks) {
    actor.anim = kAnimOpen;
    effectAtBone(actor, kBurstVariant, *burstOffsetA);
    effectAtBone(actor, kBurstVariant, *burstOffsetB);
    const uint32_t e = allocActorE0(); // exe 0x5e1440
    if (!e) return;
    ControllerActor& point = controllerActorAt(e);
    auto&& fly = classState<FlyPointFields>(controllerActorAt(e));
    point.flags = kActorLive;
    point.type = kFlyPointType;
    PatchedPoint& from = patchedRecordFor<PatchedPoint>(stateAddress(TableId::St19StateSparkFrom));
    bonePointOffsetInPlace(actor, 1, from, fly.from); // exe 0x5e1970 (shifts the table in place, as the exe)
    PatchedPoint& to = patchedRecordFor<PatchedPoint>(sparkTo); // the target offsets: patched points too
    bonePointOffsetInPlace(actor, 1, to, fly.to);
    fly.ticks = int16_t(ticks);
    fly.flag = kNoFlyFlag;
}

// the rattle's offset: the timer when odd, else 0
int16_t rattle(const MachineFields& machine) {
    const uint16_t t = machine.timer;
    return (t & 1) ? int16_t(t) : int16_t(0);
}

// the companion kept at the machine, dy below it
void carryCompanion(AnimatedActor& actor, MachineFields& machine, int32_t dy) {
    if (!machine.companion) return;
    StaticActor& c = *machine.companion;
    c.pos.x = actor.pos.x;
    c.pos.y = int32_t(actor.pos.y + dy);
    c.pos.z = actor.pos.z;
}

// exe 0x4e7f50: the drop (variant 0)
void machineDrop(AnimatedActor& actor, MachineFields& machine) {
    if (scriptState().stepSub != 1) return;
    switch (DropSub(actor.subState)) {
    case DropSub::Fall:
        actor.pos.y += uint32_t(kDropStep);
        if (actor.pos.y > kDropTop) break;
        actor.pos.y = kDropTop;
        machine.timer = kShudderTicks;
        for (TableId place : kDropDust) dustAt(actor, dataRecord<DustPlace>(stateAddress(place), DustPlace::kBytes), kDustVariant);
        actor.subState = uint8_t(DropSub::Shudder);
        break;
    case DropSub::Shudder:
        actor.pos.y.whole = int16_t((int32_t(rattle(machine)) >> 2) + kDropY);
        machine.timer -= 1u;
        if (machine.timer == 0) {
            actor.rotation.y &= 0xfff;
            actor.pos.y.whole = kDropY;
            actor.spin.y = 1;
            actor.subState = uint8_t(DropSub::Swing);
        }
        break;
    case DropSub::Swing: {
        const uint16_t s = uint16_t(actor.spin.y);
        actor.rotation.y -= s;
        actor.spin.y = int16_t(int16_t(s + 2) > 0x18 ? 0x18 : uint16_t(s + 2));
        if (actor.rotation.y < kDropYaw) {
            actor.subState = uint8_t(DropSub::Rattle);
            actor.rotation.y = kDropYaw;
            machine.timer = kRattleTicks;
        }
        break;
    }
    case DropSub::Rattle:
        actor.rotation.y = int16_t(int32_t(rattle(machine)) + kDropYaw);
        machine.timer -= 1u;
        if (machine.timer == 0) {
            machine.timer = kShudderTicks;
            actor.rotation.y = kDropYaw;
            actor.subState = uint8_t(DropSub::Wait);
        }
        break;
    case DropSub::Wait:
        machine.timer -= 1u;
        if (machine.timer != 0) break;
        openWithBursts(actor, stateAddress(TableId::St19StateDropSparkTo), 8);
        actor.subState = uint8_t(DropSub::Opening);
        break;
    case DropSub::Opening:
        if (actor.keyIndex == kOpenShakeFrame) {
            cameraShake(1, 0x400, 0x40); // exe 0x40ea80
            actor.subState = uint8_t(DropSub::Open);
        }
        break;
    default: break;
    }
    carryCompanion(actor, machine, kCompanionAbove);
}

// exe 0x4e81f0: the slide (variant 1)
void machineSlide(AnimatedActor& actor, MachineFields& machine) {
    switch (SlideSub(actor.subState)) {
    case SlideSub::WaitCue:
        if (scriptState().stepTick != kSlideCueTick) return;
        actor.subState = uint8_t(SlideSub::Slide);
        [[fallthrough]];
    case SlideSub::Slide:
        actor.velocity.z -= 8u;
        if (actor.velocity.z < kSlideMaxSpeed) actor.velocity.z = kSlideMaxSpeed;
        integrateVelocity(actor, 0, 0, actor.velocity.z); // exe 0x5e3690
        if (actor.pos.x.whole >= kSlideEndX) return;
        actor.pos.x.whole = kSlideEndX;
        machine.timer = kShudderTicks;
        cameraShake(1, 0x200, 0x20);
        actor.rotation.y &= 0xfff;
        actor.spin.y = 1;
        actor.subState = uint8_t(SlideSub::Settle);
        return;
    case SlideSub::Settle:
        machine.timer -= 1u;
        if (machine.timer == 0) actor.subState = uint8_t(SlideSub::Swing);
        return;
    case SlideSub::Swing: {
        const uint16_t s = uint16_t(actor.spin.y);
        actor.rotation.y -= s;
        actor.spin.y = int16_t(int16_t(s + 3) > 0x24 ? 0x24 : uint16_t(s + 3));
        if (actor.rotation.y < kSlideTiltStart) {
            machine.partTilt -= uint32_t(kSlideTiltStep);
            if (machine.partTilt <= 0) machine.partTilt = 0;
        }
        if (actor.rotation.y < kSlideYaw) {
            actor.rotation.y = kSlideYaw;
            machine.timer = kRattleTicks;
            actor.subState = uint8_t(SlideSub::Rattle);
        }
        return;
    }
    case SlideSub::Rattle:
        actor.rotation.y = int16_t(int32_t(rattle(machine)) + kSlideYaw);
        machine.timer -= 1u;
        if (machine.timer == 0) {
            machine.timer = kShudderTicks;
            actor.rotation.y = kSlideYaw;
            actor.subState = uint8_t(SlideSub::Wait);
        }
        return;
    case SlideSub::Wait:
        machine.timer -= 1u;
        if (machine.timer != 0) return;
        openWithBursts(actor, stateAddress(TableId::St19StateSlideSparkTo), 0xc);
        actor.subState = uint8_t(SlideSub::Opening);
        return;
    case SlideSub::Opening:
        if (actor.keyIndex == kOpenShakeFrame) {
            cameraShake(1, 0x400, 0x40);
            actor.subState = uint8_t(SlideSub::Open);
        }
        return;
    default: return;
    }
}

// exe 0x4e8410: the rise (variant 2)
void machineRise(AnimatedActor& actor, MachineFields& machine) {
    if (RiseSub(actor.subState) == RiseSub::Rise) {
        actor.pos.y += uint32_t(kRiseStep);
        if (actor.pos.y >= kRiseTop) {
            actor.pos.y = kRiseTop;
            machine.timer = kShudderTicks;
            actor.subState = uint8_t(RiseSub::Shudder);
        }
    } else if (RiseSub(actor.subState) == RiseSub::Shudder) {
        actor.pos.y.whole = int16_t((int32_t(rattle(machine)) >> 2) + kRiseY);
        machine.timer -= 1u;
        if (machine.timer == 0) {
            actor.rotation.y &= 0xfff;
            actor.pos.y.whole = kRiseY;
            actor.spin.y = 1;
            actor.subState = uint8_t(RiseSub::Done);
        }
    }
    carryCompanion(actor, machine, kCompanionBelow);
}

// exe 0x4e7f30: the variant's routine
void machineRun(AnimatedActor& actor, MachineFields& machine) {
    switch (MachineVariant(actor.variant)) {
    case MachineVariant::Drop: machineDrop(actor, machine); return;
    case MachineVariant::Slide: machineSlide(actor, machine); return;
    case MachineVariant::Rise: machineRise(actor, machine); return;
    case MachineVariant::Stand: return; // exe 0x52ef00
    default: notPortedAt(kStoryPort, "0x4e7f30 variant (table 0x896400)", actor.variant);
    }
}

// the init's companion: a pool-0x60 still model of `variant` at the machine, dy below it
void spawnCompanion(AnimatedActor& actor, MachineFields& machine, uint8_t variant, int32_t dy) {
    const uint32_t e = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!e) return;
    StaticActor& c = staticActorAt(e);
    c.flags = kActorLiveDrawn;
    c.loopFlags |= kActorSweepGroup2;
    c.type = kCompanionType;
    c.subtype = 0;
    c.variant = variant;
    c.pos.x = actor.pos.x;
    c.pos.y = int32_t(actor.pos.y + dy);
    c.pos.z = actor.pos.z;
    c.rotation.y = 0;
    machine.companion = &c;
}

// exe 0x4e7de0: the model, hit points, the box, the class block cleared; the variant's companion / tilt / pose
void machineInit(AnimatedActor& actor, MachineFields& machine) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);           // exe 0x5e1490
        return;
    }
    actor.anim = 0;
    actor.prevAnim = 0xff;
    const int16_t hp = hasAnimatedStats(actor.type) ? int16_t(animatedStats(actor.type).hitPoints) : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.velocity.clearWords();
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.collisionBox.pointAt(machineBox.stateAddress());
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock(); // exe 0x40dc40
    switch (MachineVariant(actor.variant)) {
    case MachineVariant::Drop: spawnCompanion(actor, machine, 0, kCompanionAbove); break;
    case MachineVariant::Slide: machine.partTilt = kSlideTilt; break;
    case MachineVariant::Rise:
        actor.anim = kAnimRisen;
        actor.rotation.z = 0x800;
        spawnCompanion(actor, machine, 3, 0);
        break;
    default: break;
    }
    actor.enterState(uint8_t(MachineState::Run));
}

// exe 0x4e7d20
void machine(AnimatedActor& actor) {
    auto&& machine = classState<MachineFields>(actor);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (MachineState(actor.state)) {
    case MachineState::Init: machineInit(actor, machine); break;
    case MachineState::Run: machineRun(actor, machine); break;
    case MachineState::Drop: machineDrop(actor, machine); break;
    case MachineState::Slide: machineSlide(actor, machine); break;
    case MachineState::Rise: machineRise(actor, machine); break;
    case MachineState::Idle: break;
    default: notPortedAt(kStoryPort, "0x4e7d20 state (table 0x8963f8)", actor.state); return;
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) animStep(actor); // exe 0x5e1ea0
    else animSet(actor, actor.anim, 0);                // exe 0x5e1c50
    decodedFrame().entries[2].x += uint16_t(machine.partTilt); // part 1's pitch
    animPose(actor); // exe 0x5e2d90
    if (MachineVariant(actor.variant) == MachineVariant::Stand)
        actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0); // exe 0x414050(a, 1, -1, 0)
}

} // namespace

void st19MachineUpdate(const PlayerEnv&, uint32_t address) { machine(animatedActorAt(address)); }

} // namespace od::port
