// The ST41 pod carrier (pool 0x20 type 0x7c, exe 0x506d10; hypothesis names), one routine per exe routine. Render
// output (the shadow exe 0x5f20f0) is left out.
//
// It bobs (its height = the rest height + cos(phase) * 20 >> 12), waits until the player is within 0x200 (planar), opens
// (animation 1) and drops a pod (static type 0x4f) for each of its live pods (their parts hidden, sound 0x2de); when the
// pods are all back it closes (animation 2, sound 0x2e0, the parts shown) and waits again; no pod left -> it dies (the
// kill counted, a death explosion, 0x28 ticks blinking, its drops). Two lights (stage effect type 6) ride it. Its first
// parameter's bit 7: the flag [0x897e8c + (parameter & 0x7f) * 2] set while it lives, cleared when it is freed.
// States [0x897ea0]: 0 bind 0x506e20, 1 run 0x506f50 (the sub-states [0x897ebc]: 0 wait 0x506fa0, 1 drop the pods
// 0x506ff0, 2 close 0x507110), 2 dying 0x507140, 3 freed 0x507250.
#include "content/saul_kada/st41_carrier.hpp"
#include "content/saul_kada/st41_carrier_pod_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/cutscene_actor.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/table_records.hpp"

namespace od::port {

namespace {

// [0x897ea0]
enum class CarrierState : uint8_t { Bind = 0, Run = 1, Dying = 2, Free = 3 };
// [0x897ebc]
enum class CarrierSub : uint8_t { Wait = 0, Drop = 1, Close = 2 };
enum class DropStep : uint8_t { Start = 0, Open = 1, Out = 2 };
enum class DyingStep : uint8_t { Start = 0, Burst = 1, Blink = 2 };
enum CarrierAnim : uint8_t { kAnimIdle = 0, kAnimOpen = 1, kAnimClose = 2, kAnimDie = 3 };
// The carrier's parameter: its flag (bit 7: flagged, & 0x7f the flag's entry)
struct PodCarrierParams {
    explicit PodCarrierParams(const AnimatedActor& actor) : PodCarrierParams(const_cast<AnimatedActor&>(actor)) {}
    explicit PodCarrierParams(AnimatedActor& actor) : flag(actor.params[0]) {}
    FieldAlias<int8_t, uint8_t> flag;
};
// One of its lights (exe 0x897eb0: s16 x / y / z)
const DataTable<RecordTable<ShortPoint, 2, 6>> lightPoints{TableId::St41CarrierLightPoints};
const DataTable<ScalarTable<uint16_t, 0x80>> carrierFlags{TableId::St41CarrierFlags};
constexpr int8_t kFlagged = int8_t(0x80);
constexpr int16_t kCullWidth = 0x300;
constexpr uint8_t kLightType = 6, kLightVariant = 2;
constexpr uint8_t kLightStep = 8, kLightStepCounter = 3;
constexpr uint8_t kLightOff = 1; // the light's sub-state when its carrier dies
constexpr uint16_t kOpenSound = 0x2de, kCloseSound = 0x2e0;
constexpr uint16_t kBlinkTicks = 0x28, kExplosionSpread = 0x80;
constexpr uint16_t kBlinkTint = 0x4214;
constexpr int kNearDistance = 0x200;
// its hit entry: a body (radius 0x94, heights compared, not the lock-on's) that deflects the player's shots
constexpr uint32_t kCarrierShape = hitGroup(kHitGroupBodies) | kHitCompareHeights | kHitNoLockOn | hitRadius(0x94);

// a pod's part: part 1 + k's flags (hidden while the pod is out)
uint8_t& podPartFlags(AnimatedActor& actor, uint32_t k) { return actor.parts[1 + k].flags; }
void carrierFlag(const AnimatedActor& actor, bool set) {
    const int8_t flag = PodCarrierParams(actor).flag;
    if (!(flag & kFlagged)) return;
    const uint16_t n = carrierFlags[uint8_t(flag) & 0x7f];
    if (set) setStoryFlag(n); // exe 0x510e10
    else clearStoryFlag(n);   // exe 0x510e30
}

// exe 0x506fa0: until the player comes within 0x200
void waitForPlayer(AnimatedActor& actor, PodCarrierFields& carrier) {
    if (actor.step == 0) {
        carrier.hitOff = 0;
        actor.enterStep(1);
        actor.anim = kAnimIdle;
    } else if (actor.step != 1) {
        return;
    }
    if (planarDistance(actor.pos, player().pos) < kNearDistance) { // exe 0x5e3b00
        actor.subState = uint8_t(CarrierSub::Drop);
        actor.restartStep();
    }
}
// exe 0x506ff0: opens, drops the live pods, waits for them all back, closes
void dropPods(AnimatedActor& actor, PodCarrierFields& carrier) {
    switch (DropStep(actor.step)) {
    case DropStep::Start:
        carrier.hitOff = 0;
        actor.enterStep(uint8_t(DropStep::Open));
        actor.anim = kAnimOpen;
        [[fallthrough]];
    case DropStep::Open:
        if (!actor.animEnded()) return;
        actor.enterStep(uint8_t(DropStep::Out));
        carrier.podsSignalled = 0;
        carrier.podsBack = 0;
        for (uint32_t k = 0; k < kCarrierPods; ++k) {
            if (!(carrier.pods & (1u << k))) continue;
            podPartFlags(actor, k) |= kPartHidden;
            const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
            if (!slot) continue;
            StaticActor& pod = staticActorAt(slot);
            pod.flags = kActorLive;
            pod.type = kCarrierPodType;
            pod.subtype = 0;
            pod.variant = uint8_t(k);
            classState<st41_carrier_pod_state::PodFields>(pod).carrier = &actor;
        }
        soundPlayAtPos(kOpenSound, actor.pos); // exe 0x5d5380
        return;
    case DropStep::Out: {
        const uint32_t pods = carrier.pods;
        if (pods == 0) {
            actor.enterState(uint8_t(CarrierState::Dying));
            return;
        }
        if (pods != carrier.podsBack) return;
        actor.subState = uint8_t(CarrierSub::Close);
        actor.restartStep();
        actor.anim = kAnimClose;
        soundPlayAtPos(kCloseSound, actor.pos);
        for (uint32_t k = 0; k < kCarrierPods; ++k)
            if (carrier.pods & (1u << k)) podPartFlags(actor, k) &= uint8_t(~kPartHidden);
        return;
    }
    default: return;
    }
}
// exe 0x507110: closes, then waits again
void closeUp(AnimatedActor& actor, PodCarrierFields& carrier) {
    if (actor.step == 0) {
        carrier.hitOff = 0;
        actor.enterStep(1);
    } else if (actor.step != 1) {
        return;
    }
    if (actor.animEnded()) {
        actor.subState = uint8_t(CarrierSub::Wait);
        actor.restartStep();
    }
}
// exe 0x507140: the lights off, a death explosion, 0x28 ticks blinking, the kill counted and the drops
void dying(AnimatedActor& actor, PodCarrierFields& carrier) {
    actor.tint = kTintFlash;
    switch (DyingStep(actor.subState)) {
    case DyingStep::Start:
        carrier.hitOff = 1;
        ++actor.subState;
        actor.anim = kAnimDie;
        actorHeaderAt(carrier.lights[0]).subState = kLightOff;
        return;
    case DyingStep::Burst: {
        ++actor.subState;
        actor.velocity.x = 0;
        actor.velocity.y = 0;
        actor.velocity.z = 0;
        carrier.blinkTicks = kBlinkTicks;
        if (const uint32_t slot = allocActor60(0)) { // exe 0x5e1300
            StaticActor& explosion = staticActorAt(slot);
            explosion.flags = kActorLive;
            explosion.loopFlags |= kActorSweepGroup2;
            explosion.type = typeByte(GlobalStaticType::Explosion);
            explosion.variant = 0;
            explosion.pos.x.whole = actor.pos.x.whole;
            explosion.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) - 0x100u);
            explosion.pos.z.whole = actor.pos.z.whole;
            ExplosionParams params(actorHeaderAt(slot));
            params.spread = kExplosionSpread;
            params.flashes = carrier.blinkTicks;
        }
        [[fallthrough]];
    }
    case DyingStep::Blink:
        if (carrier.blinkTicks & 1) actor.tint = kBlinkTint;
        --carrier.blinkTicks;
        if (carrier.blinkTicks != 0) return;
        countDefeat();                                                // exe 0x5e53e0
        spawnDrops(actor.pos, animatedStats(actor.type).dropTable); // exe 0x5f1270
        actor.enterState(uint8_t(CarrierState::Free));
        return;
    default: return;
    }
}
// exe 0x506e20
void bind(AnimatedActor& actor, PodCarrierFields& carrier) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        actor.enterState(uint8_t(CarrierState::Free));
        return;
    }
    actor.anim = kAnimIdle;
    actor.prevAnim = kNoAnimShown;
    actor.cullWidth = kCullWidth;
    actor.collisionBox.pointAt(TableId::St41CarrierBox);
    actor.airborne = 0;
    actor.onPlatform = 0;
    carrier.clearBytes(); // exe 0x40dc40
    carrier.hitBox.previousX = actor.pos.x.whole;
    carrier.hitBox.previousZ = actor.pos.z.whole;
    carrier.hitBox.result = 0;
    carrier.pods = kAllPods;
    carrier.restY = uint16_t(uint16_t(actor.pos.y.whole) - 0x100u);
    actor.enterState(uint8_t(CarrierState::Run));
    carrierFlag(actor, true);
    for (uint32_t i = 0; i < 2; ++i) {
        const uint32_t slot = allocActorA0(kActorStageClass); // exe 0x5e13a0
        if (!slot) continue;
        EffectActor& light = effectActorAt(slot);
        light.flags = kActorLive;
        light.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
        light.type = kLightType;
        light.variant = kLightVariant;
        light.step = kLightStep;
        light.stepCounter = kLightStepCounter;
        light.setParamsWord(actor.at); // its owner
        const ShortPoint point = lightPoints[i];
        light.pos.x = point.x;
        light.pos.y = point.y;
        light.setRecordYaw(0);
        light.pos.z = point.z;
        carrier.lights[i] = slot;
    }
}

// exe 0x506d10
void podCarrier(AnimatedActor& actor) {
    PodCarrierFields& carrier = classState<PodCarrierFields>(actor);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (CarrierState(actor.state)) { // [0x897ea0]
    case CarrierState::Bind: bind(actor, carrier); break;
    case CarrierState::Run: { // 0x506f50
        actor.tint = kTintNeutral;
        const uint8_t phase = uint8_t(carrier.bobPhase + 4u);
        carrier.bobPhase = phase;
        actor.pos.y.whole = int16_t(uint32_t((int32_t(fixedCos(int(uint32_t(phase) << 4))) * 20) >> 12) + carrier.restY);
        switch (CarrierSub(actor.subState)) { // [0x897ebc]
        case CarrierSub::Wait: waitForPlayer(actor, carrier); break;
        case CarrierSub::Drop: dropPods(actor, carrier); break;
        case CarrierSub::Close: closeUp(actor, carrier); break;
        default: notPortedAt(kStoryPort, "0x506f50 sub-state (table 0x897ebc runs into other code)", actor.subState);
        }
        break;
    }
    case CarrierState::Dying: dying(actor, carrier); break;
    case CarrierState::Free: // 0x507250
        carrierFlag(actor, false);
        freeActor(actor); // exe 0x5e1490
        return;
    default: notPortedAt(kStoryPort, "0x506d10 state (table 0x897ea0)", actor.state); return;
    }
    if (!(actor.flags & kActorLive)) return;
    animateAsked(actor, 0); // exe 0x5e1e80 / 0x5e1c30
    const AnimatedActorStats stats = animatedStats(actor.type);
    carrier.hitBox.result = 0;
    carrier.hitBox.previousX = actor.prevPos.x.whole;
    carrier.hitBox.previousZ = actor.prevPos.z.whole;
    if (!(carrier.hitOff & 1)) {
        const ShortVec4& point = bonePoint(actor, 0, kStoryPort); // exe 0x5e1810 (none: the port gap)
        const int16_t pos[3] = {point.x, point.y, point.z};
        hitListSubmit(pos, carrier.hitBox, hitOwner(actor.slot) | hitHeightLevel(stats.hitHeight) | kCarrierShape,
                      uint32_t(stats.attack) | kHitAttackDeflects); // exe 0x5e4120
    }
    // exe 0x5f20f0(a, 0x78, 0x100, 0): the shadow (render output)
}

} // namespace

void podCarrierUpdate(const PlayerEnv&, uint32_t actor) { podCarrier(animatedActorAt(actor)); }

} // namespace od::port
