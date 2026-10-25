// The pirates' shots of the gate rooms (ST41 / ST42 pool 0x40 type 0x31, exe 0x5d0f40; hypothesis names).
// States [0x8d1864]: 0 start 0x5d0f80, 1 fly 0x5d0fe0 (by variant [0x8d1874]: 0 the falling bomb 0x5d1020, 1 the rising
// homing shot 0x5d1120, 2 the thrown homing shot 0x5d1320), 2 end 0x5d1540, 3 freed 0x5d0bc0.
// The homing shots level at the player's height - 0x64, then bob around it; they turn toward the player after a delay,
// fly 0xf0 forward, end on a wall, on a hit or when their life runs out (fading 7 ticks). The bomb falls until it
// meets the floor (a shockwave, sound 0x2e6). Each draws a 0x2c billboard.
#include "content/saul_kada/st41_pirate_shot.hpp"
#include "content/saul_kada/st41_pirate_shot_state.hpp"

#include "game/loaded_data.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/collision.hpp"
#include "game/draw_commands.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/shockwave.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st41_pirate_shot_state;

namespace {

enum class ShotState : uint8_t { Start = 0, Fly = 1, End = 2, Free = 3 };
enum class HomingPhase : uint8_t { Start = 0, Level = 1, Bob = 2 };
// The shots' damages (the animated stats' row of type 0x7f): the bomb's, the homing shots'
struct ShotDamages : DataRecord {
    static constexpr uint32_t kBytes = 0xb;
    uint8_t bomb = 0; // +0x9
    uint8_t homing = 0; // +0xa
    template <class R> static ShotDamages load(const R& r) {
        ShotDamages v;
        v.bomb = r.template read<uint8_t>(0x9);
        v.homing = r.template read<uint8_t>(0xa);
        return v;
    }
};
constexpr uint8_t kShotDamagesRow = 0x7f;
constexpr uint8_t kImpactVariant = 0xe; // the bomb's shockwave on the floor
constexpr uint16_t kImpactSound = 0x2e6;
constexpr uint32_t kHitAttackTraced4000 = 0x4000; // an attack bit of the rising shot (its use not traced)
constexpr uint32_t kRisingAttack = kHitAttackStrong | kHitAttackThrown | kHitAttackTraced4000;
constexpr uint32_t kThrownAttack = kHitAttackStrong | kHitAttackThrown | kHitAttackBurning;
constexpr uint16_t kRisingClut = 0x7dd1, kThrownClut = 0x3c38;

ShotDamages damages() { return dataRecord<ShotDamages>(statTableRow(actorStatTables().animated, kShotDamagesRow), ShotDamages::kBytes); }
// the y position moved by a speed (the 16.16 y += speed << 12)
void riseBy(ProjectileActor& actor, int16_t speed) {
    actor.pos.y = int32_t(uint32_t(actor.pos.y) + (uint32_t(int32_t(speed)) << 12));
}

// exe 0x5d1590: the bomb's billboard (its u scrolling 0x20 a tick through 0..0xa0)
void bombBillboard(ProjectileActor& actor, ShotFields& shot) {
    const uint32_t u = shot.u;
    dc::emit({0, 0x2c808080u, uint32_t(uint16_t(actor.pos.x.whole)) | uint32_t(int32_t(actor.pos.y.whole)) << 16,
              u | 0x7c908000u, uint32_t(uint16_t(actor.pos.z.whole)), (u + 0x1f) | 0x2f8000u, 0x3cu, u | 0x9f00u, 0u,
              (u + 0x1f) | 0x9f00u},
             actor.at, false);
    shot.u += 0x20u;
    if (shot.u >= 0xc0) shot.u = 0;
}
// exe 0x5d1630: the homing shot's semi-transparent billboard (its CLUT)
void homingBillboard(const ProjectileActor& actor, const ShotFields& shot) {
    const uint32_t u = shot.u;
    dc::emit({0, 0x2e808080u, uint32_t(uint16_t(actor.pos.x.whole)) | uint32_t(int32_t(actor.pos.y.whole)) << 16,
              (uint32_t(shot.clut) << 16) | u | 0xc000u, uint32_t(uint16_t(actor.pos.z.whole)), (u + 0x1f) | 0x2ec000u,
              0x3cu, u | 0xdf00u, 0u, (u + 0x1f) | 0xdf00u},
             actor.at, false);
}
// exe 0x5d1020: the falling bomb
void bomb(const PlayerEnv& env, ProjectileActor& actor, ShotFields& shot) {
    actor.velocity.y += 0x40u;
    riseBy(actor, actor.velocity.y);
    if (int32_t(actorTestMove(env, actor)) < 0 && (actorPushOut(env, actor) & kWallOnY)) { // exe 0x40f570 / 0x410960
        actor.enterState(uint8_t(ShotState::End));
        const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
        if (!slot) return;
        ProjectileActor& impact = projectileActorAt(slot);
        ShockwaveParams params(projectileActorAt(slot));
        impact.flags = kActorLive;
        impact.loopFlags |= kActorSweepGroup2;
        impact.type = kShockwaveStageType;
        impact.variant = kImpactVariant;
        impact.spawnByte = 0;
        impact.pos = actor.pos;
        params.hitMode = kShockHitEdge;
        params.downward = 0;
        soundPlayAtPos(kImpactSound, actor.pos);
        return;
    }
    const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    hitListSubmit(pos, shot.hitBox, hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x3c),
                  uint32_t(damages().bomb) | kHitAttackHarmless | kHitAttackStrong);
    bombBillboard(actor, shot);
}
// the bob around the level height
void bob(ProjectileActor& actor, ShotFields& shot) {
    const uint16_t phase = shot.bobPhase;
    actor.pos.y.whole =
        int16_t(uint16_t(((int32_t(fixedSin(int(phase & 0xfff))) * int32_t(shot.bobAmplitude)) >> 12) + uint16_t(shot.levelY)));
    shot.bobPhase = uint16_t(uint16_t(actor.spin.y) + phase);
}
// the level height reached: held, the bob started
void level(ProjectileActor& actor, ShotFields& shot) {
    ++actor.subState;
    actor.pos.y.whole = shot.levelY;
    actor.spin.y = 0x20;
    shot.bobPhase = 0;
    shot.bobAmplitude = 0x28;
}
// exe 0x5d120e / 0x5d143c: turned to the player once the delay is out, forward 0xf0; its life, a wall or a hit end it
void homingFlight(const PlayerEnv& env, ProjectileActor& actor, ShotFields& shot, uint32_t attackBits) {
    if (shot.turnDelay == 0) turnYawTo(actor.rotation.y, uint32_t(angleOf(player().pos, actor.pos)), 0x80);
    else shot.turnDelay -= 1u;
    integrateVelocity(actor, 0, 0, -0xf0);
    shot.life -= 1u;
    if (shot.life == 0) actor.enterState(uint8_t(ShotState::End));
    if (int32_t(actorTestMove(env, actor)) < 0 && (actorPushOut(env, actor) & 0x3f00)) actor.enterState(uint8_t(ShotState::End));
    if (shot.hitBox.result & (kHitAttackBump | kHitAttackByPlayer)) {
        actor.enterState(uint8_t(ShotState::End));
        homingBillboard(actor, shot);
        return;
    }
    const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    hitListSubmit(pos, shot.hitBox, hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x30),
                  uint32_t(damages().homing) | attackBits);
    homingBillboard(actor, shot);
}
void homingStart(ProjectileActor& actor, ShotFields& shot, int16_t vy, uint16_t clut) {
    ++actor.subState;
    const uint32_t r = crtRand(); // exe 0x611443
    actor.velocity.y = vy;
    actor.rotation.y = int16_t(uint16_t((r & 0xff) << 4));
    shot.life = 0xf0;
    shot.levelY = int16_t(uint16_t(player().pos.y.whole) - 0x64u);
    shot.turnDelay = 0x3c;
    shot.clut = clut;
}
// exe 0x5d1120: rising from below to the player's height - 0x64
void rising(const PlayerEnv& env, ProjectileActor& actor, ShotFields& shot) {
    switch (HomingPhase(actor.subState)) {
    case HomingPhase::Start: homingStart(actor, shot, 0x40, kRisingClut); break;
    case HomingPhase::Level:
        riseBy(actor, actor.velocity.y);
        if (actor.pos.y.whole >= shot.levelY) level(actor, shot);
        break;
    case HomingPhase::Bob: bob(actor, shot); break;
    default: break;
    }
    homingFlight(env, actor, shot, kRisingAttack);
}
// exe 0x5d1320: thrown up (-0x40, gravity 2, at most 0x40) down to the height
void thrown(const PlayerEnv& env, ProjectileActor& actor, ShotFields& shot) {
    switch (HomingPhase(actor.subState)) {
    case HomingPhase::Start:
        homingStart(actor, shot, int16_t(0xffc0), kThrownClut);
        actor.acceleration.y = 2;
        break;
    case HomingPhase::Level: {
        const uint16_t vy = uint16_t(actor.velocity.y);
        riseBy(actor, int16_t(vy));
        const uint16_t next = uint16_t(uint16_t(actor.acceleration.y) + vy);
        actor.velocity.y = int16_t(next);
        if (int16_t(next) > 0 && actor.pos.y.whole >= shot.levelY) level(actor, shot);
        if (actor.velocity.y > 0x40) actor.velocity.y = 0x40;
        break;
    }
    case HomingPhase::Bob: bob(actor, shot); break;
    default: break;
    }
    homingFlight(env, actor, shot, kThrownAttack);
}
// exe 0x5d1540: the bomb freed next tick; the homing shots fade 7 ticks (their u scrolling)
void end(ProjectileActor& actor, ShotFields& shot) {
    const PirateShotVariant variant = PirateShotVariant(actor.variant);
    if (variant == PirateShotVariant::Bomb) {
        ++actor.state;
        return;
    }
    if (uint8_t(variant) > uint8_t(PirateShotVariant::Thrown)) return;
    if (actor.subState == 0) {
        actor.subState = 1;
        shot.turnDelay = 7;
    }
    shot.turnDelay -= 1u;
    if (shot.turnDelay == 0) ++actor.state;
    shot.u += 0x20u;
    homingBillboard(actor, shot);
}
void pirateShot(const PlayerEnv& env, ProjectileActor& actor) { // exe 0x5d0f40
    ShotFields& shot = classState<ShotFields>(actor);
    actor.savePreviousPose();
    switch (ShotState(actor.state)) {
    case ShotState::Start: // exe 0x5d0f80
        actor.onPlatform = 0;
        actor.platform = nullptr;
        actor.actorHit = 0;
        actor.velocity.y = 0;
        actor.collisionBox.pointAt(stateAddress(TableId::St41PirateShotBoxes) + uint32_t(actor.variant) * 0xc);
        shot.clearBytes();
        shot.hitBox.previousX = actor.pos.x.whole;
        shot.hitBox.previousZ = actor.pos.z.whole;
        actor.enterState(uint8_t(ShotState::Fly));
        return;
    case ShotState::Fly: // exe 0x5d0fe0
        switch (PirateShotVariant(actor.variant)) { // [0x8d1874]
        case PirateShotVariant::Bomb: bomb(env, actor, shot); break;
        case PirateShotVariant::Rising: rising(env, actor, shot); break;
        case PirateShotVariant::Thrown: thrown(env, actor, shot); break;
        default: notPortedAt(kStoryPort, "0x5d0fe0 variant (table 0x8d1874)", actor.variant);
        }
        shot.hitBox.result = 0;
        shot.hitBox.previousX = actor.prevPos.x.whole;
        shot.hitBox.previousZ = actor.prevPos.z.whole;
        return;
    case ShotState::End: end(actor, shot); return;
    case ShotState::Free: freeActor(actor); return; // 0x5d0bc0 -> exe 0x5e1500
    default: notPortedAt(kStoryPort, "0x5d0f40 state (table 0x8d1864)", actor.state);
    }
}

} // namespace

void pirateShotUpdate(const PlayerEnv& env, uint32_t actor) { pirateShot(env, projectileActorAt(actor)); }

} // namespace od::port
