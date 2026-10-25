// The swimming reaverbots of the Nino ruin (pool 0x20 type 0x77) and their torpedo, one routine per exe routine; the
// names are hypotheses from the observed behaviour.
//   0x4fda00  sub 0: the big swimmer (states [0x897b04]: 0x4fdc80 init, 0x4fdd80 swimming (subs [0x897b14]: 0x4fde10
//             wander, 0x4fdf70 turn away, 0x4fe020 the group attack), 0x4fe7a0 hopping (0x4fe7e0), 0x4fe8c0 down;
//             0x4feaa0 the hit check, 0x4feb40 / 0x4fec00 the collision, 0x4fe720 / 0x4fe760 / 0x4fea60 the group)
//   0x4fec40  sub 1: the small swimmer (states [0x897b54]: 0x4fedb0 init, 0x4feeb0 swimming (subs [0x897b64]: 0x4fef40
//             wander, 0x4ff000 turn away, 0x4ff0b0 the lunge), 0x4ff300 hopping (0x4ff350), 0x4ff470 down; 0x4ff650 the
//             hit check, 0x4ff6c0 / 0x4ff780 the collision, 0x4ff5f0 the group)
//   0x5cac60  pool 0x40 stage type 0x1e: the big swimmer's torpedo (states [0x8d15dc]: 0x5cac80 init, 0x5cad30 flight
//             (0x5cafb0), 0x5cb0b0 carried (0x5cb0d0 / 0x5cb150), 0x5cb180 its blast, 0x5cb1e0 freed; 0x5caf20 its
//             checks)
// The group of a type-0x77 spawn: each one's first parameter counts the others still up, the second is its turn in the
// group's attack. Render output left out as in the other ports: the shadows (exe 0x5f20f0).
#include "content/nino/ruin_swimmers.hpp"
#include "content/nino/ruin_swimmers_state.hpp"
#include "content/nino/water.hpp"
#include "game/water_splash_state.hpp"

#include <cstdlib>

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
#include "game/carry.hpp"
#include "game/collision.hpp"
#include "game/distances.hpp"
#include "game/draw_commands.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"
#include "game/territory.hpp"

namespace od::port {

using namespace ruin_swimmers_state;

namespace {

int16_t t16(int v) { return int16_t(uint16_t(uint32_t(v))); }

constexpr uint8_t kSwimmerType = 0x77;
// The group's parameters of a swimmer
struct SwimmerParams {
    explicit SwimmerParams(const AnimatedActor& actor) : SwimmerParams(const_cast<AnimatedActor&>(actor)) {}
    explicit SwimmerParams(AnimatedActor& actor) : othersLeft(actor.params[0]), attackOrder(actor.params[1]) {}
    FieldAlias<int8_t, uint8_t> othersLeft;
    FieldAlias<int8_t, uint8_t> attackOrder; // its turn in the group's attack (0: none)
};
enum class SwimmerState : uint8_t { Init = 0, Swim = 1, Hop = 2, Down = 3 };
enum class HopSub : uint8_t { Hop = 0 };
// the contact bits of the moves (the walls on x / z, the floor)
constexpr uint8_t kTouchedXWall = 0x01, kTouchedZWall = 0x02, kTouchedFloor = 0x04;
constexpr uint8_t kTouchedWalls = kTouchedXWall | kTouchedZWall;
// the mode bits
constexpr uint8_t kSwimmerShotsOnly = 0x02; // only a strong push hurts it; its body deflects the player's shots
constexpr uint8_t kSwimmerAttacking = 0x04; // a wall does not turn it away (the big one: no neighbour avoidance)
constexpr uint8_t kSwimmerLeaping = 0x10;   // out of the water: its body hurts instead of being a solid to stand on
constexpr uint8_t kSwimmerCharging = 0x20;  // the lunge: its jaw is a thrown attack
constexpr uint8_t kSwimmerNoLockOn = 0x80;  // hopping / dying: no lock-on
constexpr uint32_t kSwimmerHitKinds = kHitAttackByPlayer | kHitAttackThrown | kHitAttackStrong;
constexpr uint16_t kHurtSound = 0x8d;
constexpr uint16_t kTintDying = 0x4214;

// exe 0x4fec00 / 0x4ff780: the actor collision pass's result into the contact bits
void contactFromBodies(const Actor& actor, uint8_t& contact) {
    const uint32_t r = actor.actorHit;
    if (r == 0) return;
    if (r & (kWallOnXPlus | kWallOnXMinus)) contact |= kTouchedXWall;
    if (r & (kWallOnZPlus | kWallOnZMinus)) contact |= kTouchedZWall;
    if (r & kWallOnY) contact |= kTouchedFloor;
}
// exe 0x4feb40 / 0x4ff6c0: the move against the room (pushed out, the position back when that fails) or the territory;
// the contact bits from scratch; then a mover of the actor collision pass
void collide(const PlayerEnv& env, Actor& actor, uint8_t& contact) {
    contact = 0;
    if (int32_t(actorTestMove(env, actor)) < 0) {   // exe 0x40f570
        const uint32_t r = actorPushOut(env, actor); // exe 0x410960
        if (int32_t(r) < 0) actor.pos = actor.prevPos;
        if (r & (kWallOnXPlus | kWallOnXMinus)) contact |= kTouchedXWall;
        if (r & (kWallOnZPlus | kWallOnZMinus)) contact |= kTouchedZWall;
        if (r & kWallOnY) contact |= kTouchedFloor;
    } else if (const int rect = territoryViolated(actor.pos)) { // exe 0x5e6ab0
        const uint32_t r = territoryPushBack(actor, rect);        // exe 0x5e6b40
        if (r & (kWallOnXPlus | kWallOnXMinus)) contact |= kTouchedXWall;
        if (r & (kWallOnZPlus | kWallOnZMinus)) contact |= kTouchedZWall;
    }
    actorCollisionSubmitMover(actor);
}
int angleFromPlayer(const Actor& actor) { return angleOf(player().pos, actor.pos); } // exe 0x5e3bc0(player, actor)
int playerDistance(const Actor& actor) { return planarDistance(player().pos, actor.pos); } // exe 0x5e3b00
void enterSub(AnimatedActor& actor, uint8_t sub) {
    actor.subState = sub;
    actor.restartStep();
}
// the start of a move: its mode, its animation, standing still but for the z speed
void startMove(AnimatedActor& actor, uint8_t& mode, uint8_t& startKey, uint8_t m, uint8_t anim,
               int16_t speed) {
    mode = m;
    actor.step = 1;
    actor.anim = anim;
    startKey = 0;
    actor.velocity.setXY(0, 0);
    actor.velocity.z = speed;
}
// exe 0x4fe720 / 0x4fe760: the group's attack order -- the first othersLeft + 1 live swimmers numbered n, n - 1, .. (or
// all cleared)
void orderGroup(const AnimatedActor& actor, bool clear) {
    int n = int(SwimmerParams(actor).othersLeft) + 1;
    if (n == 0) return;
    AnimatedPool& pool = animatedPool();
    for (uint32_t i = 0; i < kAnimatedFieldSlots; ++i) {
        AnimatedActor& e = pool[i];
        if ((e.flags & kActorLiveDrawn) && e.type == kSwimmerType) {
            SwimmerParams(e).attackOrder = clear ? int8_t(0) : int8_t(n);
            --n;
        }
        if (n == 0) return;
    }
}
// exe 0x4fea60 / 0x4ff5f0 (withOrder): one of the group down -- the first othersLeft + 1 live swimmers count one fewer
// (and are numbered n, n - 1, ..)
void countGroupDown(const AnimatedActor& actor, bool withOrder) {
    int n = int(SwimmerParams(actor).othersLeft) + 1;
    if (n == 0) return;
    AnimatedPool& pool = animatedPool();
    for (uint32_t i = 0; i < kAnimatedFieldSlots; ++i) {
        AnimatedActor& e = pool[i];
        if ((e.flags & kActorLiveDrawn) && e.type == kSwimmerType) {
            SwimmerParams params(e);
            params.othersLeft -= 1;
            if (withOrder) params.attackOrder = int8_t(n);
            --n;
        }
        if (n == 0) return;
    }
}
// the lunge's vertical speed toward the player (exe 0x4fe160 / 0x4ff10d inline): over distance / (speed >> 4) ticks
// to 0x80 above the player's feet, x 16, within +-0x80
void lungeVy(AnimatedActor& actor) {
    const int d = playerDistance(actor);
    const int q = d / (int(actor.velocity.z) >> 4);
    if (q == 0) {
        actor.velocity.y = 0;
        return;
    }
    const int16_t vy = t16(((int(actor.pos.y.whole) - int(player().pos.y.whole) + 0x80) / q) << 4);
    actor.velocity.y = vy;
    if (std::abs(int(vy)) > 0x80) actor.velocity.y = vy <= 0 ? int16_t(-0x80) : int16_t(0x80);
}
// the bounce off the surface: a speed past 0x7f quartered, reversed
int16_t bounceVy(int16_t v) {
    if (uint16_t(v) & 0xff80u) v = int16_t(v >> 2);
    return t16(-v);
}
// a death explosion (the global static type 8) at the swimmer
void deathExplosion(const AnimatedActor& actor) {
    const uint32_t e = allocActor60(0); // exe 0x5e1300
    if (!e) return;
    StaticActor& boom = staticActorAt(e);
    ExplosionParams params(actorHeaderAt(e));
    boom.flags = kActorLive;
    boom.loopFlags |= kActorSweepGroup2;
    boom.type = typeByte(GlobalStaticType::Explosion);
    boom.variant = 0;
    boom.pos.x.whole = actor.pos.x.whole;
    boom.pos.y.whole = actor.pos.y.whole;
    boom.pos.z.whole = actor.pos.z.whole;
    params.spread = 0x30;
    params.flashes = 1;
}
// exe 0x4fe8c0 / 0x4ff470: down -- it stops (in water it sinks slowly, animation 8), the group counted down, explosions
// every few ticks, then it frees what stood on it, leaves its drops and is freed
void swimmerDown(const PlayerEnv& env, AnimatedActor& actor, int16_t& ticks, uint8_t& mode,
                 uint8_t& contact, bool withOrder, bool waterSound) {
    actor.tint = kTintFlash;
    const bool water = player().inWater == 1;
    if (actor.step == 0) {
        actor.step = 1;
        if (!water) {
            if (actor.airborne == 0) {
                actor.velocity.x = 0;
                actor.velocity.y = 0;
                actor.velocity.z = 0;
            }
        } else {
            const int16_t v = actor.velocity.y;
            if (std::abs(int(v)) > 0x40) actor.velocity.y = v >= 0 ? int16_t(0x40) : int16_t(-0x40);
            actor.anim = 8;
        }
        ticks = 0x14;
        countDefeat();
        countGroupDown(actor, withOrder);
    }
    if (ticks & 3) deathExplosion(actor);
    if (waterSound && player().inWater == 1 && !(gameState().frame & 0xf)) soundPlayAtPos(0x1d5, actor.pos);
    if (ticks & 1) actor.tint = kTintDying;
    const int16_t c = ticks;
    if (c < 5) mode |= kSwimmerNoLockOn;
    ticks = t16(c - 1);
    if (int16_t(c - 1) == 0) {
        actorDetachRiders(actor);                                                    // exe 0x414e70
        spawnDrops(actor.pos, animatedStats(actor.type, actor.spawnByte).dropTable); // exe 0x5f1270
        freeActor(actor);                                                            // exe 0x5e1490
        return;
    }
    integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z); // exe 0x5e3690
    if (actor.airborne != 0) actor.velocity.y = int16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y));
    collide(env, actor, contact);
    if (actor.wasAirborne != 0 && actor.airborne == 0) {
        actor.velocity.z = 0;
        actor.velocity.x = 0;
    }
}
// exe 0x4fdf70 / 0x4ff000: a wall -- a new heading (back round, at random), turned to before it swims on
void turnAway(AnimatedActor& actor, uint8_t& mode, uint8_t& startKey, int16_t& heading) {
    if (actor.step == 0) {
        mode = kSwimmerAttacking;
        actor.step = 1;
        actor.anim = 0;
        startKey = 0;
        actor.velocity.setXY(0, 0);
        actor.velocity.z = -0x80;
        const uint32_t r = rngStep();
        heading = int16_t(((r & 0x3ff) + uint16_t(actor.rotation.y) + 0x600u) & 0xfffu);
    }
    if (turnYawTo(actor.rotation.y, uint32_t(int32_t(heading)), 0x10) == 0) {
        actor.subState = 0;
        actor.rotation.y = heading;
        actor.restartStep();
    }
    moveAlongYaw(actor.pos, actor.rotation.y, 0, 0, actor.velocity.z); // exe 0x5e36f0
}
// the hop on dry land: a jump on every landing, its yaw / its direction at random
void hopLanding(AnimatedActor& actor, int16_t yawStep, uint16_t sound) {
    actor.rotation.y = int16_t(uint16_t(actor.rotation.y) + ((rngStep() & 1) ? uint16_t(yawStep) : uint16_t(-yawStep)));
    if (rngStep() & 1) actor.velocity.z = t16(-actor.velocity.z);
    soundPlayAtPos(sound, actor.pos);
}

// ==== 0x4fda00: the big swimmer =======================================================================================
enum class BigSwimSub : uint8_t { Wander = 0, TurnAway = 1, Attack = 2 };
enum class AttackStep : uint8_t { Start = 0, Approach = 1, Lunge = 3, Torpedo = 4 };
enum class LungePhase : uint8_t { Start = 0, Open = 1, Aim = 2, Leap = 3, Fall = 4 };
enum class TorpedoPhase : uint8_t { TurnAway = 0, Circle = 1, Face = 2, Fire = 3, Recoil = 4, Recover = 5 };
const DataTable<CollisionBoxRecord> bigSwimmerBox{TableId::RuinSwimmersBigSwimmerBox};
constexpr uint8_t kTorpedoType = 0x1e;
constexpr uint16_t kLungeSound = 0x1d3, kTorpedoSound = 0x1d4, kBigHopSound = 0x1d6;


// exe 0x4feaa0: the hits (the jaw's / the tail's): a hit point lost per damage of the kinds it takes (a flash, the
// hurt sound); dead -> down, -1; else how many boxes were hit
int bigSwimmerHit(AnimatedActor& actor, BigSwimmerFields& fields) {
    int n = (fields.tailHit.result & kHitDamageMask) ? 1 : 0;
    const uint32_t h = fields.hit.result;
    const uint32_t d = h & kHitDamageMask;
    if (d == 0) return n;
    ++n;
    uint32_t taken = 0;
    const bool hurts = !(fields.mode & kSwimmerShotsOnly) ? (h & kSwimmerHitKinds) != 0 : (h & kHitAttackStrongPush) != 0;
    if (hurts) {
        actor.tint = kTintFlash;
        soundPlayAtPos(kHurtSound, actor.pos);
        taken = d;
    }
    if (hitPointsAdd(actor, -int(taken)) != 0) { // exe 0x5e52f0
        actor.enterState(uint8_t(SwimmerState::Down));
        return -1;
    }
    return n;
}
// exe 0x4fdc80: the model, its row's hit points, the box; it swims (the player in water) or hops
void bigSwimmerInit(AnimatedActor& actor, BigSwimmerFields& fields) {
    if (!bindActorModel(actor, kAnyModelVariant)) { // exe 0x5e0bc0(a, 0)
        freeActor(actor);
        return;
    }
    actor.runFlags |= kActorRunMarked;
    actor.anim = 0;
    actor.prevAnim = 0xff;
    actor.rotation.x = 0;
    actor.rotation.z = 0;
    actor.velocity.x = 0;
    actor.velocity.y = 0;
    actor.velocity.z = 0;
    const int16_t hp =
        hasAnimatedStats(actor.type) ? int16_t(animatedStats(actor.type, actor.spawnByte).hitPoints) : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.collisionBox.pointAt(bigSwimmerBox.stateAddress());
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock(); // exe 0x40dc40
    fields.hit.previousX = actor.pos.x.whole;
    fields.hit.previousZ = actor.pos.z.whole;
    fields.hit.result = 0;
    fields.startKey = 0;
    fields.groundY = actor.pos.y.whole;
    actor.enterState(uint8_t(player().inWater == 1 ? SwimmerState::Swim : SwimmerState::Hop));
    registerEnemy(actor.slot, 0);
    resetModelScale(actor); // exe 0x4e5cd0
}
// exe 0x4fde10: it wanders (a small turn every 0x10.. ticks, away from its neighbours); the player close in front of it
// (within a quarter turn, 0x80..0x480 away) and moving or jumping -> the group's attack (its turn already set: too)
void bigWander(AnimatedActor& actor, BigSwimmerFields& fields) {
    SwimmerParams params(actor);
    if (actor.step == 0) {
        startMove(actor, fields.mode, fields.startKey, 0, 0, -0x80);
        fields.ticks = 0x10;
    }
    fields.ticks -= 1;
    if (fields.ticks == 0) {
        const uint32_t r = rngStep() & 0xff;
        fields.ticks = int16_t(r + 0x10);
        actor.rotation.y = int16_t(uint16_t(actor.rotation.y) + ((r & 1) ? 4u : 0xfffcu));
        fields.mode &= uint8_t(~(kSwimmerAttacking | kSwimmerCharging));
    }
    if (!(fields.mode & kSwimmerAttacking) && neighbourFind(actor, fields.neighbour, 0) != 0)
        actor.rotation.y = int16_t(uint16_t(actor.rotation.y) + 8u);
    bool attack = false;
    if (params.attackOrder == 0) {
        int e = int(uint16_t(actor.rotation.y) & 0xfff) - (angleFromPlayer(actor) & 0xfff);
        if (uint32_t(e + 0x800) > 0x1000u) e = int((uint32_t(-e) ^ 0xfffu) + 1u);
        if (uint32_t(e + 0x200) < 0x400u && uint32_t(playerDistance(actor) - 0x80) < 0x400u) {
            const Player& p = player();
            if (std::abs(int(p.velocity.z)) > 0x60 || p.airborne != 0) {
                orderGroup(actor, false); // exe 0x4fe720
                attack = true;
            }
        }
    } else {
        attack = true;
    }
    if (attack) enterSub(actor, uint8_t(BigSwimSub::Attack));
    integrateVelocity(actor, 0, 0, actor.velocity.z); // exe 0x5e3690
}
// the landing of the lunge / the torpedo: back to the approach (its ticks by the group's size, its speed by its turn)
void attackLand(AnimatedActor& actor, BigSwimmerFields& fields) {
    const SwimmerParams params(actor);
    fields.ticks = t16(int(int16_t(int16_t(params.othersLeft) + 1)) * 40);
    actor.pos.y.whole = fields.groundY;
    actor.velocity.z = t16((int(int16_t(params.attackOrder)) - 0x10) << 5);
    actor.rotation.x = 0;
    actor.enterStep(uint8_t(AttackStep::Approach));
}
// the torpedo (pool 0x40 stage type 0x1e) and its two bubble trails (splashes)
void fireTorpedo(const AnimatedActor& actor, uint32_t s) {
    soundPlayAtPos(kTorpedoSound, actor.pos);
    ProjectileActor& torpedo = projectileActorAt(s);
    torpedo.flags = kActorLiveDrawn;
    torpedo.loopFlags |= kActorSweepGroup2;
    torpedo.type = kTorpedoType;
    torpedo.subtype = 0;
    torpedo.variant = 0;
    torpedo.pos.x.whole = actor.pos.x.whole;
    torpedo.pos.y.whole = actor.pos.y.whole;
    torpedo.pos.z.whole = actor.pos.z.whole;
    torpedo.rotation.x = 0;
    torpedo.rotation.y = actor.rotation.y;
    torpedo.rotation.z = 0;
    classState<TorpedoFields>(torpedo).shooter = &actor;
    for (const int8_t sizeMask : {int8_t(0xf), int8_t(0x17)}) {
        const uint32_t e = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (!e) continue;
        StaticActor& splash = staticActorAt(e);
        WaterSplashFields& params = classState<WaterSplashFields>(splash);
        splash.flags = kActorLive;
        splash.loopFlags |= kSplashLoopFlags;
        splash.type = kSplashType;
        splash.subtype = 0;
        splash.variant = 0;
        splash.pos.x.whole = actor.pos.x.whole;
        splash.pos.y.whole = actor.pos.y.whole;
        splash.pos.z.whole = actor.pos.z.whole;
        splash.rotation.x = 0;
        splash.rotation.z = 0;
        splash.rotation.y = actor.rotation.y;
        params.speed = 0x80;
        params.sizeMask = sizeMask;
        const uint32_t r = rngStep();
        params.count = 7;
        params.particleLife = int8_t((r & 0xf) + 0x10u);
    }
}
// exe 0x4fe020: the group's attack -- it circles the player (0x380..0x480 away) for its turn's ticks, then (the last of
// the group, 3 in 4) torpedoes or the lunge; its turn cleared -> back to wandering
void bigAttack(AnimatedActor& actor, BigSwimmerFields& fields) {
    SwimmerParams params(actor);
    switch (AttackStep(actor.step)) {
    case AttackStep::Start: {
        fields.mode = kSwimmerAttacking;
        actor.anim = 0;
        fields.startKey = 0;
        actor.velocity.setXY(0, 0);
        const int order = int16_t(params.attackOrder);
        actor.velocity.z = t16((order - 0x10) << 5);
        fields.ticks = t16(order << 5);
        actor.step = uint8_t(AttackStep::Approach);
        [[fallthrough]];
    }
    case AttackStep::Approach: {
        int off;
        if (fields.contact & kTouchedWalls) {
            off = -0x200;
        } else {
            const int d = playerDistance(actor);
            off = d > 0x480 ? -0x40 : (d >= 0x380 ? 0 : 0x40);
        }
        turnYawTo(actor.rotation.y, uint32_t(angleFromPlayer(actor) + off + 0x400), 0x40);
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        fields.ticks -= 1;
        if (fields.ticks == 0) {
            if (params.othersLeft == 0 && (rngStep() & 3)) {
                fields.counter = 1;
                fields.ticks = 0x30;
                actor.velocity.z = -0x300;
                actor.enterStep(uint8_t(AttackStep::Torpedo));
            } else {
                actor.enterStep(uint8_t(AttackStep::Lunge));
            }
        }
        if (params.attackOrder == 0) enterSub(actor, uint8_t(BigSwimSub::Wander));
        return;
    }
    case AttackStep::Lunge: // the leap at the player, out of the water and back
        switch (LungePhase(actor.stepCounter)) {
        case LungePhase::Start:
            soundPlayAtPos(kLungeSound, actor.pos);
            actor.anim = 4;
            fields.ticks = 0x18;
            actor.velocity.z = -0x300;
            fields.mode |= kSwimmerLeaping | kSwimmerCharging;
            fields.lungeYaw = int16_t(angleFromPlayer(actor) - 0x40);
            lungeVy(actor);
            actor.stepCounter += 1u;
            [[fallthrough]];
        case LungePhase::Open:
            if (actor.animEnded()) {
                actor.anim = 5;
                actor.stepCounter += 1u;
            }
            [[fallthrough]];
        case LungePhase::Aim:
            if (turnYawTo(actor.rotation.y, uint32_t(int32_t(fields.lungeYaw)), 0x100) == 0) {
                actor.anim = 5;
                actor.stepCounter = uint8_t(LungePhase::Leap);
            }
            integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
            return;
        case LungePhase::Leap:
            fields.ticks -= 1;
            if (fields.ticks == 0) {
                actor.velocity.y = bounceVy(actor.velocity.y);
                actor.anim = 6;
                actor.stepCounter += 1u;
            }
            integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
            return;
        case LungePhase::Fall: {
            actor.velocity.z = int16_t(uint16_t(actor.velocity.z) + 0xcu);
            if (actor.velocity.z > -0x200) actor.velocity.z = -0x200;
            integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
            if (actor.animKey.flags != 0) actor.anim = 0;
            if (fields.contact & kTouchedWalls) actor.rotation.y = int16_t(uint16_t(actor.rotation.y) - 0x80u);
            const int16_t g = fields.groundY;
            if (actor.velocity.y > 0 ? actor.pos.y.whole < g : actor.pos.y.whole > g) return;
            fields.mode &= uint8_t(~(kSwimmerLeaping | kSwimmerCharging));
            attackLand(actor, fields);
            return;
        }
        default: return;
        }
    case AttackStep::Torpedo: // away, round, facing the player: the torpedo (the volleys its counter allows)
        switch (TorpedoPhase(actor.stepCounter)) {
        case TorpedoPhase::TurnAway: {
            const int t = turnYawTo(actor.rotation.y, uint32_t(angleFromPlayer(actor) + 0x800), 0x40);
            integrateVelocity(actor, 0, 0, actor.velocity.z);
            if (fields.ticks != 0) fields.ticks -= 1;
            if (fields.ticks == 0 || ((fields.contact & kTouchedWalls) && t == 0)) {
                fields.ticks = 0xc;
                actor.stepCounter += 1u;
            }
            return;
        }
        case TorpedoPhase::Circle:
            turnYawTo(actor.rotation.y, uint32_t(angleFromPlayer(actor) + 0xc00), 0x40);
            integrateVelocity(actor, 0, 0, actor.velocity.z);
            if (fields.ticks != 0) fields.ticks -= 1;
            if (fields.ticks == 0) {
                fields.ticks = 0x30;
                actor.stepCounter += 1u;
            }
            return;
        case TorpedoPhase::Face:
            if (turnYawTo(actor.rotation.y, uint32_t(angleFromPlayer(actor)), 0x40) <= 0) {
                actor.anim = 2;
                actor.stepCounter += 1u;
            } else {
                fields.ticks -= 1;
                if (fields.ticks == 0) {
                    if (fields.counter != 0) {
                        fields.ticks = 0x30;
                        fields.counter -= 1;
                        actor.stepCounter = uint8_t(TorpedoPhase::TurnAway);
                    } else {
                        actor.enterStep(uint8_t(AttackStep::Lunge));
                    }
                }
            }
            integrateVelocity(actor, 0, 0, actor.velocity.z);
            return;
        case TorpedoPhase::Fire: {
            if (!actor.animEnded()) return;
            const uint32_t s = allocActor40(kActorStageClass); // exe 0x5e1280
            if (!s) {
                actor.enterStep(uint8_t(AttackStep::Lunge));
                return;
            }
            fireTorpedo(actor, s);
            actor.velocity.z = 0x200;
            actor.stepCounter += 1u;
            return;
        }
        case TorpedoPhase::Recoil:
            actor.velocity.z = int16_t(uint16_t(actor.velocity.z) - 0x10u);
            if (actor.velocity.z < 0) {
                actor.velocity.z = 0;
                actor.anim = 3;
                actor.stepCounter = uint8_t(TorpedoPhase::Recover);
            }
            integrateVelocity(actor, 0, 0, actor.velocity.z);
            return;
        case TorpedoPhase::Recover:
            if (actor.animEnded()) attackLand(actor, fields);
            return;
        default: return;
        }
    default: return;
    }
}
void bigDown(const PlayerEnv& env, AnimatedActor& actor, BigSwimmerFields& fields) {
    swimmerDown(env, actor, fields.ticks, fields.mode, fields.contact, false, true); // exe 0x4fe8c0
}
// exe 0x4fdd80
void bigSwim(const PlayerEnv& env, AnimatedActor& actor, BigSwimmerFields& fields) {
    SwimmerParams params(actor);
    actor.tint = kTintNeutral;
    const int r = bigSwimmerHit(actor, fields);
    if (r < 0) {
        bigDown(env, actor, fields);
        return;
    }
    if (r > 0 && params.attackOrder == 0) {
        orderGroup(actor, false); // exe 0x4fe720
        enterSub(actor, uint8_t(BigSwimSub::Attack));
    }
    if (playerRides(actor)) orderGroup(actor, true); // exe 0x59beb0 -> 0x4fe760
    switch (BigSwimSub(actor.subState)) { // [0x897b14]
    case BigSwimSub::Wander: bigWander(actor, fields); break;
    case BigSwimSub::TurnAway: turnAway(actor, fields.mode, fields.startKey, fields.counter); break;
    case BigSwimSub::Attack: bigAttack(actor, fields); break;
    default: notPortedAt(kStoryPort, "0x4fda00 state 1 sub (table 0x897b14)", actor.subState);
    }
    collide(env, actor, fields.contact); // exe 0x4feb40
    if ((fields.contact & kTouchedWalls) && !(fields.mode & kSwimmerAttacking))
        enterSub(actor, uint8_t(BigSwimSub::TurnAway));
}
// exe 0x4fe7e0: it hops about on dry land (a jump of 0x300 on each landing)
void bigHop(AnimatedActor& actor, BigSwimmerFields& fields) {
    if (actor.step == 0) {
        fields.mode = kSwimmerNoLockOn | kSwimmerLeaping;
        actor.step = 1;
        actor.anim = 1;
        fields.startKey = 0;
        actor.velocity.x = 0;
        actor.velocity.z = -0x40;
        actor.velocity.y = -0x300;
        actor.acceleration.y = 0x80;
        actor.airborne = 1;
    }
    if (fields.contact & kTouchedFloor) {
        actor.velocity.y = -0x300;
        hopLanding(actor, 0x40, kBigHopSound);
    }
    moveAlongYaw(actor.pos, actor.rotation.y, 0, actor.velocity.y, actor.velocity.z);
    actor.velocity.y = int16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y));
}
// exe 0x4fe7a0
void bigHopState(const PlayerEnv& env, AnimatedActor& actor, BigSwimmerFields& fields) {
    actor.tint = kTintNeutral;
    if (bigSwimmerHit(actor, fields) < 0) {
        bigDown(env, actor, fields);
        return;
    }
    switch (HopSub(actor.subState)) { // [0x897b20]
    case HopSub::Hop: bigHop(actor, fields); break;
    default: notPortedAt(kStoryPort, "0x4fda00 state 2 sub (table 0x897b20)", actor.subState);
    }
    collide(env, actor, fields.contact);
}
// exe 0x4fda00
void torpedoSwimmer(const PlayerEnv& env, AnimatedActor& actor) {
    BigSwimmerFields& fields = classState<BigSwimmerFields>(actor);
    contactFromBodies(actor, fields.contact); // exe 0x4fec00
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (SwimmerState(actor.state)) { // [0x897b04]
    case SwimmerState::Init: bigSwimmerInit(actor, fields); break;
    case SwimmerState::Swim: bigSwim(env, actor, fields); break;
    case SwimmerState::Hop: bigHopState(env, actor, fields); break;
    case SwimmerState::Down: bigDown(env, actor, fields); break;
    default: notPortedAt(kStoryPort, "0x4fda00 state (table 0x897b04)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) animStep(actor); // exe 0x5e1ea0
    else animSet(actor, actor.anim, fields.startKey);  // exe 0x5e1c50
    animPose(actor);                                   // exe 0x5e2d90
    const AnimatedActorStats row = animatedStats(actor.type, actor.spawnByte);
    fields.hit.result = 0;
    fields.hit.previousX = actor.prevPos.x.whole;
    fields.tailHit.result = 0;
    fields.hit.previousZ = actor.prevPos.z.whole;
    fields.tailHit.previousX = actor.prevPos.x.whole;
    fields.tailHit.previousZ = actor.prevPos.z.whole;
    const uint8_t mode = fields.mode;
    static constexpr int16_t kJaw[3] = {0, -0x20, -0x40}, kTail[3] = {0, 0, -0x20};
    int16_t at[3];
    bonePointOffset(actor, 2, kJaw, at); // exe 0x5e1970
    const uint32_t noLockOn = (mode & kSwimmerNoLockOn) ? kHitNoLockOn : 0u;
    const uint32_t owner = hitOwner(actor.slot);
    const uint32_t jawShape = hitGroup(kHitGroupBodies) | hitRadius(0x50);
    if (SwimmerState(actor.state) == SwimmerState::Down) {
        hitListSubmit(at, fields.hit, owner | noLockOn | jawShape, kHitAttackBump); // exe 0x5e4120
    } else {
        uint32_t attack;
        if (mode & kSwimmerShotsOnly) attack = kHitAttackDeflects | row.attack;
        else if (mode & kSwimmerCharging) attack = kHitAttackThrown | kHitAttackSolid | row.strongAttack;
        else attack = kHitAttackSolid | row.attack;
        hitListSubmit(at, fields.hit, owner | noLockOn | jawShape, attack);
        bonePointOffset(actor, 6, kTail, at);
        const uint32_t tailAttack =
            kHitAttackDeflects | ((mode & kSwimmerCharging) ? kHitAttackThrown : 0u) | row.shotAttack;
        hitListSubmit(at, fields.tailHit, owner | kHitNoLockOn | jawShape, tailAttack);
        if (!(fields.mode & kSwimmerLeaping)) {
            actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0); // exe 0x414050(a, 1, 0xffff, 0)
            const ShortVec4& body = bonePoint(actor, 0, kStoryPort);          // exe 0x5e1810
            const int16_t b[3] = {body.x, body.y, body.z};
            hitListSubmit(b, fields.hit, owner | kHitNoLockOn | hitGroup(kHitGroupBodies) | hitRadius(0x64),
                          kHitAttackSolid);
        } else {
            const ShortVec4& body = bonePoint(actor, 0, kStoryPort);
            const int16_t b[3] = {body.x, body.y, body.z};
            const uint32_t lockOff =
                SwimmerState(actor.state) != SwimmerState::Hop ? kHitNoLockOn : 0u;
            hitListSubmit(b, fields.hit, owner | lockOff | hitGroup(kHitGroupBodies) | hitRadius(0x64),
                          row.shotAttack | kHitAttackThrown | kHitAttackSolid);
        }
    }
    // exe 0x5f20f0(a, 0x96, 0x2a0, 0): the shadow (render output)
}

// ==== 0x4fec40: the small swimmer =====================================================================================
enum class SmallSwimSub : uint8_t { Wander = 0, TurnAway = 1, Lunge = 2 };
enum class SmallLungeStep : uint8_t { Start = 0, Run = 1 };
const DataTable<CollisionBoxRecord> smallSwimmerBox{TableId::RuinSwimmersSmallSwimmerBox};
constexpr int16_t kSmallScale = 0x80;
constexpr uint8_t kSmallRowOffset = 2; // its rows follow the big one's (spawn byte + 2)
constexpr uint16_t kSmallLungeSound = 0x1d1, kSmallHopSound = 0x1d2;

// exe 0x4ff650: a hit: its damage by the kinds it takes (a flash, the hurt sound); dead -> down, -1; else 1
int smallSwimmerHit(AnimatedActor& actor, SmallSwimmerFields& fields) {
    const uint32_t h = fields.hit.result;
    const uint32_t d = h & kHitDamageMask;
    if (d == 0) return 0;
    uint32_t taken = 0;
    if (h & kSwimmerHitKinds) {
        actor.tint = kTintFlash;
        soundPlayAtPos(kHurtSound, actor.pos);
        taken = d;
    }
    if (hitPointsAdd(actor, -int(taken)) != 0) { // exe 0x5e52f0
        actor.enterState(uint8_t(SwimmerState::Down));
        return -1;
    }
    return 1;
}
// exe 0x4fedb0: the model, the row of its spawn byte + 2, the box; it swims or hops; half its model's size (exe
// 0x4ff630)
void smallSwimmerInit(AnimatedActor& actor, SmallSwimmerFields& fields) {
    if (!bindActorModel(actor, kAnyModelVariant)) { // exe 0x5e0bc0(a, 0)
        freeActor(actor);
        return;
    }
    actor.runFlags |= kActorRunMarked;
    const uint8_t row = uint8_t(actor.spawnByte + kSmallRowOffset);
    actor.anim = 0;
    actor.prevAnim = 0xff;
    actor.rotation.x = 0;
    actor.rotation.z = 0;
    actor.velocity.x = 0;
    actor.velocity.y = 0;
    actor.velocity.z = 0;
    actor.spawnByte = row;
    const int16_t hp = int16_t(animatedStats(actor.type, row).hitPoints);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.collisionBox.pointAt(smallSwimmerBox.stateAddress());
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock();
    fields.hit.previousX = actor.pos.x.whole;
    fields.hit.previousZ = actor.pos.z.whole;
    fields.hit.result = 0;
    fields.startKey = 0;
    fields.groundY = actor.pos.y.whole;
    actor.enterState(uint8_t(player().inWater == 1 ? SwimmerState::Swim : SwimmerState::Hop));
    registerEnemy(actor.slot, 0);
    actor.scale.x = kSmallScale;
    actor.scale.y = kSmallScale;
    actor.scale.z = kSmallScale;
}
// exe 0x4fef40: it wanders (a small turn every 0x10.. ticks, away from its neighbours); bumped -> it turns away
void smallWander(AnimatedActor& actor, SmallSwimmerFields& fields) {
    if (actor.step == 0) {
        startMove(actor, fields.mode, fields.startKey, 0, 0, -0x80);
        fields.ticks = 0x10;
    }
    fields.ticks -= 1;
    if (fields.ticks == 0) {
        const uint32_t r = rngStep() & 0xff;
        fields.ticks = int16_t(r + 0x10);
        actor.rotation.y = int16_t(uint16_t(actor.rotation.y) + ((r & 1) ? 4u : 0xfffcu));
    }
    if (neighbourFind(actor, fields.neighbour, 0) != 0) actor.rotation.y = int16_t(uint16_t(actor.rotation.y) + 0x10u);
    if (fields.hit.result & kHitAttackBump) enterSub(actor, uint8_t(SmallSwimSub::TurnAway));
    integrateVelocity(actor, 0, 0, actor.velocity.z);
}
// exe 0x4ff0b0: the lunge at the player (its turn in the group cleared): out of the water toward it, the bounce, the
// fall back to its depth
void smallLunge(AnimatedActor& actor, SmallSwimmerFields& fields) {
    switch (SmallLungeStep(actor.step)) {
    case SmallLungeStep::Start:
        fields.mode = kSwimmerAttacking;
        actor.anim = 0;
        fields.startKey = 0;
        actor.velocity.setXY(0, 0);
        SwimmerParams(actor).attackOrder = 0;
        actor.step = uint8_t(SmallLungeStep::Run);
        soundPlayAtPos(kSmallLungeSound, actor.pos);
        [[fallthrough]];
    case SmallLungeStep::Run: break;
    default: return;
    }
    switch (LungePhase(actor.stepCounter)) {
    case LungePhase::Start:
        actor.anim = 4;
        fields.ticks = 0x30;
        actor.velocity.z = -0x300;
        fields.mode |= kSwimmerCharging;
        lungeVy(actor);
        actor.stepCounter += 1u;
        [[fallthrough]];
    case LungePhase::Open:
        if (actor.animEnded()) {
            actor.anim = 5;
            actor.stepCounter += 1u;
        }
        [[fallthrough]];
    case LungePhase::Aim: {
        const int angle = angleFromPlayer(actor);
        fields.lungeYaw = int16_t(angle);
        const int t = turnYawTo(actor.rotation.y, uint32_t(int32_t(int16_t(uint16_t(angle)))), 0x100);
        if (t == 0 || (fields.hit.result & kHitAttackBump) || (fields.contact & kTouchedWalls)) {
            actor.anim = 5;
            actor.stepCounter = uint8_t(LungePhase::Leap);
        }
        integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
        return;
    }
    case LungePhase::Leap:
        fields.ticks -= 1;
        if (fields.ticks == 0 || (fields.contact & kTouchedWalls)) {
            actor.velocity.y = bounceVy(actor.velocity.y);
            actor.anim = 6;
            fields.mode &= uint8_t(~kSwimmerCharging);
            actor.stepCounter += 1u;
            actor.velocity.z = -0x80;
        }
        integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
        return;
    case LungePhase::Fall: {
        integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
        if (actor.animKey.flags != 0) actor.anim = 0;
        const int16_t g = fields.groundY;
        if (actor.velocity.y > 0) {
            if (actor.pos.y.whole < g) return;
            actor.rotation.x = 0;
            actor.restartStep();
            actor.pos.y.whole = g;
            actor.subState = uint8_t(SmallSwimSub::Wander);
        } else {
            if (actor.pos.y.whole > g) return;
            actor.pos.y.whole = g;
            actor.rotation.x = 0;
            actor.subState = uint8_t(SmallSwimSub::Wander);
            actor.restartStep();
        }
        return;
    }
    default: return;
    }
}
void smallDown(const PlayerEnv& env, AnimatedActor& actor, SmallSwimmerFields& fields) {
    swimmerDown(env, actor, fields.ticks, fields.mode, fields.contact, true, false); // exe 0x4ff470
}
// exe 0x4feeb0
void smallSwim(const PlayerEnv& env, AnimatedActor& actor, SmallSwimmerFields& fields) {
    actor.tint = kTintNeutral;
    if (!(fields.mode & kSwimmerShotsOnly)) {
        const int r = smallSwimmerHit(actor, fields);
        if (r < 0) {
            smallDown(env, actor, fields);
            return;
        }
        if (r > 0) enterSub(actor, uint8_t(SmallSwimSub::Lunge));
    }
    switch (SmallSwimSub(actor.subState)) { // [0x897b64]
    case SmallSwimSub::Wander: smallWander(actor, fields); break;
    case SmallSwimSub::TurnAway: turnAway(actor, fields.mode, fields.startKey, fields.heading); break;
    case SmallSwimSub::Lunge: smallLunge(actor, fields); break;
    default: notPortedAt(kStoryPort, "0x4fec40 state 1 sub (table 0x897b64)", actor.subState);
    }
    collide(env, actor, fields.contact); // exe 0x4ff6c0
    if ((fields.contact & kTouchedWalls) && !(fields.mode & kSwimmerAttacking))
        enterSub(actor, uint8_t(SmallSwimSub::TurnAway));
    if (SwimmerParams(actor).attackOrder != 0) enterSub(actor, uint8_t(SmallSwimSub::Lunge));
}
// exe 0x4ff350: it hops about on dry land (a jump of 0x100..0x200 on each landing)
void smallHop(AnimatedActor& actor, SmallSwimmerFields& fields) {
    if (actor.step == 0) {
        fields.mode = 0;
        actor.step = 1;
        actor.anim = 1;
        fields.startKey = 0;
        actor.velocity.x = 0;
        actor.velocity.z = -0x40;
        const uint32_t r = rngStep();
        actor.acceleration.y = 0x80;
        actor.airborne = 1;
        actor.velocity.y = int16_t(uint16_t((r & 0xff) - 0x200u));
    }
    if (fields.contact & kTouchedFloor) {
        actor.velocity.y = int16_t(uint16_t((rngStep() & 0xff) - 0x200u));
        hopLanding(actor, 0x80, kSmallHopSound);
    }
    moveAlongYaw(actor.pos, actor.rotation.y, 0, actor.velocity.y, 0);
    actor.velocity.y = int16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y));
}
// exe 0x4ff300
void smallHopState(const PlayerEnv& env, AnimatedActor& actor, SmallSwimmerFields& fields) {
    actor.tint = kTintNeutral;
    if (!(fields.mode & kSwimmerShotsOnly) && smallSwimmerHit(actor, fields) < 0) {
        smallDown(env, actor, fields);
        return;
    }
    switch (HopSub(actor.subState)) { // [0x897b70]
    case HopSub::Hop: smallHop(actor, fields); break;
    default: notPortedAt(kStoryPort, "0x4fec40 state 2 sub (table 0x897b70)", actor.subState);
    }
    collide(env, actor, fields.contact);
}
// exe 0x4fec40
void lungingSwimmer(const PlayerEnv& env, AnimatedActor& actor) {
    SmallSwimmerFields& fields = classState<SmallSwimmerFields>(actor);
    contactFromBodies(actor, fields.contact); // exe 0x4ff780
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (SwimmerState(actor.state)) { // [0x897b54]
    case SwimmerState::Init: smallSwimmerInit(actor, fields); break;
    case SwimmerState::Swim: smallSwim(env, actor, fields); break;
    case SwimmerState::Hop: smallHopState(env, actor, fields); break;
    case SwimmerState::Down: smallDown(env, actor, fields); break;
    default: notPortedAt(kStoryPort, "0x4fec40 state (table 0x897b54)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) animStep(actor);
    else animSet(actor, actor.anim, fields.startKey);
    animPose(actor);
    const AnimatedActorStats row = animatedStats(actor.type, actor.spawnByte);
    fields.hit.result = 0;
    const uint8_t mode = fields.mode;
    const uint32_t noLockOn = (mode & kSwimmerNoLockOn) ? kHitNoLockOn : 0u;
    fields.hit.previousX = actor.prevPos.x.whole;
    fields.hit.previousZ = actor.prevPos.z.whole;
    const ShortVec4& body = bonePoint(actor, 0, kStoryPort); // exe 0x5e1810
    const int16_t at[3] = {body.x, body.y, body.z};
    const uint32_t owner = hitOwner(actor.slot) | noLockOn | hitGroup(kHitGroupBodies) | hitRadius(0x28);
    if (SwimmerState(actor.state) == SwimmerState::Down) {
        hitListSubmit(at, fields.hit, owner, kHitAttackBump); // exe 0x5e4120
    } else {
        uint32_t attack;
        if (mode & kSwimmerShotsOnly) attack = kHitAttackDeflects | row.attack;
        else if (mode & kSwimmerCharging) attack = kHitAttackThrown | kHitAttackSolid | row.strongAttack;
        else attack = kHitAttackSolid | row.attack;
        hitListSubmit(at, fields.hit, owner, attack);
    }
    // exe 0x5f20f0(a, 0x28, 0x2a0, 0): the shadow (render output)
}

// ==== 0x5cac60: the torpedo ===========================================================================================
enum class TorpedoState : uint8_t { Init = 0, Fly = 1, Carried = 2, Burst = 3, Free = 4 };
enum class CarriedSub : uint8_t { Held = 0, Thrown = 1 };
const DataTable<CollisionBoxRecord> torpedoBoxes{TableId::RuinSwimmersTorpedoBoxes}; // 12 bytes per variant
const DataTable<ScalarTable<int16_t, 8>> exhaustSizes{TableId::RuinSwimmersExhaustSizes};
constexpr uint8_t kCompanionStage = 0x2f, kCompanionType = 0x68;
constexpr uint32_t kTorpedoBursts = 0xff0000; // any attack kind on it
constexpr int16_t kTorpedoSpeed = 0x460, kExhaustAhead = 0x700, kHoverHeight = 0x64;

// exe 0x5caf20: hit by any attack, or pushed out of a wall / a floor -> its blast (1); lifted by the player -> carried;
// else the hit box made ready
int torpedoCheck(const PlayerEnv& env, ProjectileActor& torpedo, TorpedoFields& fields) {
    if (fields.hit.result & kTorpedoBursts) {
        torpedo.enterState(uint8_t(TorpedoState::Burst));
        return 1;
    }
    if (int32_t(actorTestMove(env, torpedo)) < 0) {              // exe 0x40f570
        if (actorPushOut(env, torpedo) & (kWallOnSides | kWallOnY)) { // exe 0x410960
            torpedo.enterState(uint8_t(TorpedoState::Burst));
            return 1;
        }
    }
    if (playerLiftTargetCheck(torpedo, 0)) { // exe 0x573a60
        torpedo.enterState(uint8_t(TorpedoState::Carried));
        torpedo.runFlags |= kActorRunMarked;
    }
    fields.hit.result = 0;
    fields.hit.previousX = torpedo.prevPos.x.whole;
    fields.hit.previousZ = torpedo.prevPos.z.whole;
    // exe 0x5f20f0(a, 0x60, 0x200, 0): the shadow (render output)
    return 0;
}
// exe 0x5cafb0: it spins, a capsule of hit entries along its length, homing on the player for 0xb4 ticks
void torpedoFlight(ProjectileActor& torpedo, TorpedoFields& fields) {
    torpedo.rotation.z = int16_t(uint16_t(torpedo.rotation.z) + 0x100u);
    if (torpedo.step == 0) {
        torpedo.step = 1;
        fields.homingTicks = 0xb4;
    }
    int16_t nose[3], tail[3];
    nose[0] = tail[0] = torpedo.pos.x.whole;
    nose[1] = tail[1] = torpedo.pos.y.whole;
    nose[2] = tail[2] = torpedo.pos.z.whole;
    moveAlongYaw16(nose, torpedo.rotation.y, 0, 0, kTorpedoSpeed); // exe 0x5e3750
    moveAlongYaw16(tail, torpedo.rotation.y, 0, 0, -kTorpedoSpeed);
    hitListSubmitCapsule(nose, 0xc, tail, 0xc, fields.hit, hitOwner(torpedo.slot) | hitGroup(kHitGroupBodies) | hitCapsule(2),
                         kHitAttackHarmless | kHitAttackStrong | 0x1e); // exe 0x5e4190
    if (fields.homingTicks != 0) {
        fields.homingTicks -= 1;
        turnYawTo(torpedo.rotation.y, uint32_t(angleFromPlayer(torpedo)), 0x20);
    }
    integrateVelocity(torpedo, 0, 0, -kTorpedoSpeed); // exe 0x5e3690
}
// exe 0x5cad30: the flight, kept 0x64 above the ground (or the player's feet); on dry land an exhaust ring ahead of the
// nose (draw command 0xf0), under water a bubble trail
void torpedoFly(const PlayerEnv& env, ProjectileActor& torpedo, TorpedoFields& fields) {
    torpedo.savePreviousPose();
    if (torpedoCheck(env, torpedo, fields) == 0) {
        switch (torpedo.subState) { // [0x8d15f0]
        case 0: torpedoFlight(torpedo, fields); break;
        default: notPortedAt(kStoryPort, "0x5cac60 flight sub (table 0x8d15f0)", torpedo.subState);
        }
        int g = actorGround(env, torpedo); // exe 0x40f190
        if (g > player().pos.y.whole) g = player().pos.y.whole;
        const int d = g - torpedo.pos.y.whole - kHoverHeight;
        if (uint32_t(d + 10) > 0x14u) integrateVelocity(torpedo, 0, d < 0 ? -0x60 : 0x60, 0);
    }
    if (player().inWater == 0) {
        int16_t v[3] = {torpedo.pos.x.whole, torpedo.pos.y.whole, torpedo.pos.z.whole};
        moveAlongYaw16(v, torpedo.rotation.y, 0, 0, kExhaustAhead); // exe 0x5e3750
        const uint8_t k = fields.exhaustTick;
        fields.exhaustTick = uint8_t(k + 1u);
        const uint32_t w = uint32_t(int32_t(exhaustSizes[k & 7]) + 6);
        const uint32_t e = ((w << 5) + w) << 4;
        dc::emit({0x4000u, e | 0xf0000000u, uint32_t(uint16_t(v[0])) | uint32_t(uint16_t(v[1])) << 16,
                  (e | 0x11000000u) << 1, uint32_t(uint16_t(v[2])) | 0x30000u, 0x14u},
                 torpedo.at);
        return;
    }
    const uint32_t e = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!e) return;
    StaticActor& splash = staticActorAt(e);
    WaterSplashFields& params = classState<WaterSplashFields>(splash);
    splash.flags = kActorLive;
    splash.loopFlags |= kSplashLoopFlags;
    splash.type = kSplashType;
    splash.subtype = 0;
    splash.variant = 0;
    splash.pos = torpedo.pos;
    moveAlongYaw(splash.pos, torpedo.rotation.y, 0, 0, kExhaustAhead); // exe 0x5e36f0
    splash.rotation.x = 0;
    splash.rotation.z = 0;
    splash.rotation.y = int16_t(uint16_t(torpedo.rotation.y) + 0x800u);
    params.speed = 0x80;
    params.sizeMask = 7;
    const uint32_t r = rngStep();
    params.count = 3;
    params.particleLife = int8_t((r & 3) + 8u);
}
// exe 0x5cb0b0: carried at the player's hands until released (thrown along the player's facing, or dropped), then the
// throw's arc
void torpedoCarried(const PlayerEnv& env, ProjectileActor& torpedo, TorpedoFields& fields) {
    switch (CarriedSub(torpedo.subState)) { // [0x8d1604]
    case CarriedSub::Held: { // exe 0x5cb0d0
        carryAtHands(torpedo, 0, 0, 0); // exe 0x573ab0
        const CarryRelease release = carryReleased(); // exe 0x573950
        if (release == CarryRelease::Held) return;
        torpedo.velocity.x = 0;
        torpedo.runFlags &= uint8_t(~kActorRunMarked);
        if (release == CarryRelease::Thrown) {
            torpedo.velocity.y = -0x200;
            torpedo.velocity.z = -0x380;
            torpedo.rotation = player().rotation;
        } else {
            torpedo.velocity.y = -0x1e0;
            torpedo.velocity.z = -0x40;
        }
        carryDropFromPlayer(torpedo); // exe 0x573990
        torpedo.restartStep();
        torpedo.subState += 1u;
        return;
    }
    case CarriedSub::Thrown: // exe 0x5cb150
        torpedo.velocity.y = int16_t(uint16_t(torpedo.velocity.y) + 0x40u);
        integrateVelocity(torpedo, 0, torpedo.velocity.y, torpedo.velocity.z);
        torpedoCheck(env, torpedo, fields);
        return;
    default: notPortedAt(kStoryPort, "0x5cac60 carried sub (table 0x8d1604)", torpedo.subState);
    }
}
// exe 0x5cac80: its model, liftable, its boxes by its variant; on ST2F a companion that follows it
void torpedoInit(ProjectileActor& torpedo, TorpedoFields& fields) {
    if (!bindActorModel(torpedo, 0)) { // exe 0x5e0ba0
        torpedo.state = uint8_t(TorpedoState::Free);
        return;
    }
    torpedo.state += 1u;
    torpedo.flags |= kActorDrawn;
    const uint32_t v = torpedo.variant;
    torpedo.interaction = kInteractLift;
    torpedo.airborne = 0;
    torpedo.onPlatform = 0;
    torpedo.platform = nullptr;
    torpedo.collisionBox.pointAt(torpedoBoxes.stateAddress() + v * 12);
    torpedo.interactionBox.pointAt(stateAddress(TableId::RuinSwimmersTorpedoLiftBoxes) + v * 12);
    fields.clearBlockBytes(); // exe 0x40dc40(+0xbc, +0xd8)
    fields.hit.previousX = torpedo.pos.x.whole;
    fields.hit.previousZ = torpedo.pos.z.whole;
    if (gameState().stage == kCompanionStage) {
        const uint32_t e = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (e) {
            StaticActor& companion = staticActorAt(e);
            companion.flags = kActorLive;
            companion.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
            companion.type = kCompanionType;
            companion.subtype = 0;
            companion.variant = 0;
            classState<TorpedoCompanionFields>(companion).torpedo = &torpedo;
        }
    }
}
// exe 0x5cb180: the blast (the global static type 7) 0x14 above it, counted as a defeat
void torpedoBurst(ProjectileActor& torpedo) {
    torpedo.state = uint8_t(TorpedoState::Free);
    countDefeat();
    const uint32_t e = allocActor60(0); // exe 0x5e1300
    if (!e) return;
    StaticActor& blast = staticActorAt(e);
    BlastParams params(actorHeaderAt(e));
    blast.flags = kActorLive;
    blast.loopFlags |= kActorSweepGroup2;
    blast.type = typeByte(GlobalStaticType::Blast);
    blast.variant = 0;
    blast.pos.x.whole = torpedo.pos.x.whole;
    blast.pos.y.whole = int16_t(uint16_t(torpedo.pos.y.whole) - 0x14u);
    blast.pos.z.whole = torpedo.pos.z.whole;
    params.size = 0x50;
    params.attack = 5;
}
// exe 0x5cac60
void torpedoRun(const PlayerEnv& env, ProjectileActor& torpedo) {
    TorpedoFields& fields = classState<TorpedoFields>(torpedo);
    switch (TorpedoState(torpedo.state)) { // [0x8d15dc]
    case TorpedoState::Init: torpedoInit(torpedo, fields); return;
    case TorpedoState::Fly: torpedoFly(env, torpedo, fields); return;
    case TorpedoState::Carried: torpedoCarried(env, torpedo, fields); return;
    case TorpedoState::Burst: torpedoBurst(torpedo); return;
    case TorpedoState::Free: // exe 0x5cb1e0
        if (torpedo.subState != 0) {
            torpedo.subState += 1u;
            torpedo.flags &= uint8_t(~kActorDrawn);
            return;
        }
        freeActor(torpedo); // exe 0x5e1500
        return;
    default: notPortedAt(kStoryPort, "0x5cac60 state (table 0x8d15dc)", torpedo.state);
    }
}

} // namespace

void torpedoSwimmerUpdate(const PlayerEnv& env, uint32_t actor) { torpedoSwimmer(env, animatedActorAt(actor)); }
void lungingSwimmerUpdate(const PlayerEnv& env, uint32_t actor) { lungingSwimmer(env, animatedActorAt(actor)); }
void torpedoUpdate(const PlayerEnv& env, uint32_t actor) { torpedoRun(env, projectileActorAt(actor)); }

} // namespace od::port
