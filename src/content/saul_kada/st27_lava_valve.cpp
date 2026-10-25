// The ST27 lava valve / pump (static type 0x3b, cast 0 of scripts 0x40 / 0x41, story 13, exe 0x5af970; hypothesis
// names), one routine per exe routine.
//
// Its hit points: its row's * 2, one regenerated every 0x3c ticks. Hits (the player's) only count through a fraction:
// the damage taken is undone and added to it; each wrap below 0 takes 1 hit point (weak hits: +0x4f per hit point; a
// thrown hit: +4 per hit point, looped, and a shake). Below 0 hit points it sinks. The script's cue kValveShake shakes
// it (+-0x20 for 0x59 ticks) at the fixed point (-0x400, -0x100, -0x600); dying it shakes, then sinks while above y 0.
// A capsule hit volume (radius 0x18 / 0x4b) from y -0x40 to y -0x180.
// States [0x8cf9f4]: 0 bind 0x5af9c0, 1 run 0x5afa50 (the sub-states [0x8cfa00]: 0 idle 0x5afbd0, 1 sink 0x5afcf0),
// 2 freed 0x5afdd0, 3 / 4 the sub-states' routines.
#include "content/saul_kada/st27_lava_valve.hpp"
#include "content/saul_kada/st27_lava_valve_state.hpp"

#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/camera_state.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st27_lava_valve_state;

namespace {

enum class ValveState : uint8_t { Bind = 0, Run = 1, Free = 2, Idle = 3, Sink = 4 };
// [0x8cfa00]
enum class ValveSub : uint8_t { Idle = 0, Sink = 1 };
enum class ShakeStep : uint8_t { Start = 0, Shake = 1, Fall = 2 };
constexpr uint16_t kRegenTicks = 0x3c;
constexpr uint8_t kShakeTicks = 0x59;
constexpr int16_t kShakeSpeed = 0x20, kSinkGravity = 0xc0;
constexpr int8_t kWeakHitsPerPoint = 0x4f, kThrownHitsPerPoint = 4;
// the lava valve's place (16.16)
constexpr int32_t kValveX = int32_t(0xfc000000u), kValveY = int32_t(0xff000000u), kValveZ = int32_t(0xfa000000u);
// its capsule: a body (group 2) with a capsule of one step, hurting nothing, solid
constexpr uint32_t kValveShape = hitGroup(kHitGroupBodies) | hitCapsule(1);
constexpr uint32_t kValveAttack = kHitAttackHarmless | kHitAttackSolid;

// the shake's step shared by exe 0x5afbd0 / 0x5afcf0: 0x20 right for steps 3..4, 0x20 left for 5..8, back to 1
void shake(StaticActor& actor, ValveFields& valve) {
    const uint8_t b = actor.stepCounter;
    if (b < 5) {
        actor.velocity.x = kShakeSpeed;
        actor.stepCounter = uint8_t(b + 1u);
        --valve.shakeTicks;
    } else if (b < 9) {
        actor.velocity.x = -kShakeSpeed;
        actor.stepCounter = uint8_t(b + 1u);
        --valve.shakeTicks;
        if (actor.stepCounter > 8) actor.stepCounter = 1;
    }
}
void startShake(StaticActor& actor, ValveFields& valve) {
    actor.step = uint8_t(ShakeStep::Shake);
    actor.stepCounter = 3;
    actor.velocity.y = 0;
    actor.acceleration.y = kSinkGravity;
    actor.velocity.x = 0;
    valve.shakeTicks = kShakeTicks;
}
// exe 0x5afbd0: the regeneration, and the script's shake at the valve's place
void idle(StaticActor& actor, ValveFields& valve) {
    if (valve.regenTicks == 0) {
        if (actor.maxHitPoints > actor.hitPoints) ++actor.hitPoints;
        valve.regenTicks = kRegenTicks;
    } else {
        --valve.regenTicks;
    }
    if (!(valve.cues & kValveShake)) return;
    switch (ShakeStep(actor.step)) {
    case ShakeStep::Start:
        startShake(actor, valve);
        actor.pos.x = kValveX;
        actor.pos.y = kValveY;
        actor.pos.z = kValveZ;
        [[fallthrough]];
    case ShakeStep::Shake:
        actor.rotation.y = int16_t(uint16_t(cameraState().viewAngles.y) + 0x800u);
        integrateVelocity(actor, actor.velocity.x, 0, 0); // exe 0x5e3690
        actor.rotation.y = valve.homeYaw;
        shake(actor, valve);
        if (valve.shakeTicks == 0) {
            actor.step = uint8_t(ShakeStep::Start);
            valve.cues &= uint8_t(~kValveShake);
        }
        return;
    default: return;
    }
}
// exe 0x5afcf0: shakes, then sinks while above y 0
void sink(StaticActor& actor, ValveFields& valve) {
    switch (ShakeStep(actor.step)) {
    case ShakeStep::Start:
        startShake(actor, valve);
        [[fallthrough]];
    case ShakeStep::Shake:
        integrateVelocity(actor, actor.velocity.x, 0, 0);
        shake(actor, valve);
        if (valve.shakeTicks == 0) ++actor.step;
        return;
    case ShakeStep::Fall:
        if (actor.pos.y.whole < 0) {
            integrateVelocity(actor, 0, actor.velocity.y, 0);
            actor.velocity.y += uint16_t(actor.acceleration.y);
        }
        return;
    default: return;
    }
}
void startSinking(StaticActor& actor) {
    actor.hitPoints = -1;
    actor.subState = uint8_t(ValveSub::Sink);
    actor.step = 0;
}
// exe 0x5afa50
void run(StaticActor& actor, ValveFields& valve) {
    const uint32_t hit = valve.hitBox.result;
    if (hit & kHitAttackByPlayer) {
        const uint16_t hpBefore = uint16_t(actor.hitPoints);
        hitPointsAdd(actor, -int(hit & kHitDamageMask)); // exe 0x5e52f0
        valve.damageFraction =
            int8_t(uint8_t(uint8_t(valve.damageFraction) + uint8_t(uint8_t(actor.hitPoints) - uint8_t(hpBefore))));
        actor.hitPoints = int16_t(hpBefore);
        if (hit & kHitAttackThrown) {
            valve.cues |= kValveShake;
            actor.step = 0;
            while (valve.damageFraction < 0) {
                --actor.hitPoints;
                valve.damageFraction = int8_t(uint8_t(valve.damageFraction) + uint8_t(kThrownHitsPerPoint));
                if (actor.hitPoints < 0) startSinking(actor);
            }
        } else if (valve.damageFraction < 0) {
            actor.hitPoints = int16_t(hpBefore - 1u);
            valve.damageFraction = int8_t(uint8_t(valve.damageFraction) + uint8_t(kWeakHitsPerPoint));
            if (actor.hitPoints < 0) startSinking(actor);
        }
    }
    if (ValveSub(actor.subState) == ValveSub::Idle) idle(actor, valve);
    else if (ValveSub(actor.subState) == ValveSub::Sink) sink(actor, valve);
    else notPortedAt(kStoryPort, "0x5afa50 sub-state (table 0x8cfa00)", actor.subState);
    valve.hitBox.previousX = actor.prevPos.x.whole;
    valve.hitBox.previousZ = actor.prevPos.z.whole;
    valve.hitBox.result = 0;
    if (!hasStaticStats(actor.type)) {
        notPorted(kStoryPort, "0x5afa50: no type row (the exe reads through null)");
        return;
    }
    const StaticActorStats stats = staticStats(actor.type, 0);
    const int16_t top[3] = {actor.pos.x.whole, int16_t(uint16_t(actor.pos.y.whole) - 0x180u), actor.pos.z.whole};
    const int16_t low[3] = {actor.pos.x.whole, int16_t(uint16_t(actor.pos.y.whole) - 0x40u), actor.pos.z.whole};
    hitListSubmitCapsule(low, 0x18, top, 0x4b, valve.hitBox,
                         hitOwner(actor.slot) | hitHeightLevel(stats.hitHeight) | kValveShape,
                         uint32_t(stats.damage) | kValveAttack); // exe 0x5e4190
}
// exe 0x5af9c0
void bind(StaticActor& actor, ValveFields& valve) {
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
        freeActor(actor);
        return;
    }
    actor.flags |= kActorDrawn;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.wallHit = 0;
    actor.actorHit = 0;
    const uint16_t hp = hasStaticStats(actor.type) ? uint16_t(staticStats(actor.type, 0).hitPoints * 2) : uint16_t(0);
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    valve.damageFraction = 0;
    valve.regenTicks = kRegenTicks;
    valve.hitBox.result = 0;
    valve.homeYaw = actor.rotation.y;
    ++actor.state;
}

// exe 0x5af970
void lavaValve(StaticActor& actor) {
    ValveFields& valve = classState<ValveFields>(actor);
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (ValveState(actor.state)) {
    case ValveState::Bind: bind(actor, valve); return;
    case ValveState::Run: run(actor, valve); return;
    case ValveState::Free: freeActor(actor); return; // 0x5afdd0
    case ValveState::Idle: idle(actor, valve); return;
    case ValveState::Sink: sink(actor, valve); return;
    default: notPortedAt(kStoryPort, "0x5af970 state (table 0x8cf9f4)", actor.state);
    }
}

} // namespace

void valveUpdate(const PlayerEnv&, uint32_t actor) { lavaValve(staticActorAt(actor)); }

} // namespace od::port
