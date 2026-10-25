// ST43's roller (static type 7, exe 0x5ad7b0; hypothesis names). Render output (the shadow exe 0x5f20f0(actor, 0x40,
// 0x100, 0), drawn unless carried) is left out.
//
// States [0x8cf6d8]: 0 bind 0x5ad7f0, 1 rise 0x5ad8e0, 2 move 0x5ad980 (sub-states [0x8cf6ec]: 0 wander 0x5adc20,
// 1 turn to the player 0x5add80, 2 roll over 0x5adf40), 3 lifted 0x5ae000 (sub-states [0x8cf6f8]: 0 carried 0x5ae020,
// 1 thrown 0x5ae110, 2 dropped 0x5ae230), 4 death 0x5ae420. Entries 5..10 of 0x8cf6d8 (and 3..5 of 0x8cf6ec) are the
// bytes of the next tables: notPortedAt.
#include "content/late_game/st43_roller.hpp"
#include "content/late_game/st43_roller_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/carry.hpp"
#include "game/explosions.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st43_roller_state;

namespace {

enum class RollerState : uint8_t { Bind = 0, Rise = 1, Move = 2, Lifted = 3, Death = 4 };
enum class RollerMove : uint8_t { Wander = 0, TurnToPlayer = 1, RollOver = 2 };
enum class RollerLift : uint8_t { Carried = 0, Thrown = 1, Dropped = 2 };
enum class RollOverStep : uint8_t { Over = 0, Wait = 1, Back = 2 };

// the speeds by variant and the turn steps (exe data)
const DataTable<ScalarTable<uint16_t, 4>> rollerSpeeds{TableId::St43RollerSpeeds};
const DataTable<ScalarTable<uint8_t, 4>> rollerTurnSteps{TableId::St43RollerTurnSteps};
constexpr int16_t kRollOverAngle = 0x600, kRollOverStep = 0x60;
constexpr uint32_t kBodyEntry = hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x40);
constexpr uint32_t kThrownEntry = hitGroup(kHitGroupPlayerAttacks) | kHitNoLockOn | hitRadius(0x40);
constexpr uint16_t kBlastSize = 0x80;

uint16_t speedOf(const StaticActor& actor) { return rollerSpeeds[actor.variant]; }

// its spawner's bit (4 + variant of the spawner's first parameter) cleared, the death
void detach(StaticActor& actor, RollerFields& roller) {
    ControllerActor& spawner = *roller.spawner;
    spawner.params[0] = uint8_t(spawner.params[0] & ~(1u << ((actor.variant + 4u) & 0x1f)) & 0xffu);
    actor.state = uint8_t(RollerState::Death);
}
// its hit entry: the result cleared, the previous x / z, the entry at its position
void submitHit(const StaticActor& actor, RollerFields& roller, uint32_t entry, uint32_t attack) {
    roller.hitBox.result = 0;
    roller.hitBox.previousX = actor.prevPos.x.whole;
    roller.hitBox.previousZ = actor.prevPos.z.whole;
    const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    hitListSubmit(p, roller.hitBox, hitOwner(actor.slot) | entry, attack); // exe 0x5e4120
}
// exe 0x410740 on the stack triple {0, travel yaw, 0}: the travel yaw reflected off the walls of push-out result r
void reflectTravelYaw(RollerFields& roller, uint32_t r) {
    int16_t pitch = 0, yaw = roller.travelYaw;
    reflectRotationOffWalls(pitch, yaw, r);
    roller.travelYaw = yaw;
}
// the travel yaw into [-0x800, 0x800)
void wrapTravelYaw(RollerFields& roller) {
    if (roller.travelYaw >= 0x800) roller.travelYaw = int16_t(uint16_t(roller.travelYaw) - 0x1000u);
    if (roller.travelYaw < -0x800) roller.travelYaw = int16_t(uint16_t(roller.travelYaw) + 0x1000u);
}
// below its speed: accelerating, else held at it
void speedRamp(StaticActor& actor) {
    if (actor.velocity.z < int16_t(speedOf(actor))) {
        actor.acceleration.z = 0x10;
    } else {
        actor.acceleration.z = 0;
        actor.velocity.z = int16_t(speedOf(actor));
    }
}
// the wander again: not paused, for 60 or 120 ticks
void restartWander(RollerFields& roller) {
    roller.pauseOrTarget = 0;
    roller.timer = int16_t(((rngStep() & 1u) + 1u) * 60u);
}
// enough damage taken: the roll over
bool rollOverOnDamage(StaticActor& actor, RollerFields& roller) {
    if (roller.damageTaken < int16_t(actor.maxHitPoints >> 2)) return false;
    roller.damageTaken = 0;
    actor.subState = uint8_t(RollerMove::RollOver);
    return true;
}

// ---- the move's sub-states [0x8cf6ec] ------------------------------------------------------------------------------
// exe 0x5adc20: drifting along its travel yaw, pausing now and then; the player near its height -> turn to him
void wander(StaticActor& actor, RollerFields& roller) {
    if (roller.pauseOrTarget == 0)
        roller.travelYaw = int16_t(uint16_t(roller.travelYaw) + uint16_t(int16_t(roller.yawDrift)));
    wrapTravelYaw(roller);
    actor.rotation.y = roller.travelYaw;
    if (player().pos.y.whole >= -0x80) actor.subState = uint8_t(RollerMove::TurnToPlayer);
    else rollOverOnDamage(actor, roller);
    if (roller.pauseOrTarget == 0) speedRamp(actor);
    roller.timer = int16_t(roller.timer - 1);
    if (roller.timer != 0) return;
    if (roller.pauseOrTarget == 0) {
        roller.pauseOrTarget = 1;
        const uint32_t r = rngStep();
        actor.velocity.z = 0;
        roller.timer = int16_t(((r & 1u) * 15u + actor.variant) * 4u + 0x1eu);
    } else {
        restartWander(roller);
        actor.velocity.z = int16_t(speedOf(actor));
    }
}
// exe 0x5add80: turned toward the angle of itself seen from the player (0x80 above his feet) by its variant's step; the
// player off its height -> the wander again
void turnToPlayer(StaticActor& actor, RollerFields& roller) {
    const Player& p = player();
    const int32_t from[3] = {p.pos.x, int32_t(uint32_t(p.pos.y) + 0xff800000u), p.pos.z};
    const int32_t to[3] = {actor.pos.x, actor.pos.y, actor.pos.z};
    const uint16_t ang = uint16_t(angleOf(from[0], from[2], to[0], to[2])); // exe 0x5e3bc0
    roller.pauseOrTarget = int16_t(ang);
    if (int16_t(ang) >= 0x800) roller.pauseOrTarget = int16_t(ang - 0x1000u);
    const uint8_t step = rollerTurnSteps[actor.variant];
    roller.yawDrift = int8_t(step);
    const uint16_t cur = uint16_t(roller.travelYaw), tgt = uint16_t(roller.pauseOrTarget);
    uint16_t d = uint16_t(cur - tgt);
    if (int16_t(d) >= 0x800) d = uint16_t(d - 0x1000u);
    if (int16_t(d) < -0x800) d = uint16_t(d + 0x1000u);
    const int16_t s = int16_t(int8_t(step));
    if (int16_t(d) >= s) roller.travelYaw = int16_t(uint16_t(cur - uint16_t(s)));
    else if (int(int16_t(d)) > -int(int8_t(step))) roller.travelYaw = int16_t(tgt);
    else roller.travelYaw = int16_t(uint16_t(cur + uint16_t(s)));
    wrapTravelYaw(roller);
    actor.rotation.y = roller.travelYaw;
    speedRamp(actor);
    if (int16_t(uint32_t(from[1]) >> 16) < -0x80) {
        restartWander(roller);
        actor.acceleration.z = 0;
        actor.velocity.z = int16_t(speedOf(actor));
        roller.yawDrift = 0;
        actor.subState = uint8_t(RollerMove::Wander);
    } else {
        rollOverOnDamage(actor, roller);
    }
}
// exe 0x5adf40: braking, rolled over to 0x600, 0xf0 ticks, back to 0, the wander again
void rollOver(StaticActor& actor, RollerFields& roller) {
    if (actor.velocity.z < 0) {
        actor.acceleration.z = 0x10;
    } else {
        actor.acceleration.z = 0;
        actor.velocity.z = 0;
    }
    switch (RollOverStep(actor.step)) {
    case RollOverStep::Over:
        actor.rotation.z = int16_t(uint16_t(actor.rotation.z) + uint16_t(kRollOverStep));
        if (actor.rotation.z == kRollOverAngle) {
            actor.step = uint8_t(RollOverStep::Wait);
            roller.timer = 0xf0;
        }
        return;
    case RollOverStep::Wait:
        roller.timer = int16_t(roller.timer - 1);
        if (roller.timer == 0) actor.step = uint8_t(RollOverStep::Back);
        return;
    case RollOverStep::Back:
        actor.rotation.z = int16_t(uint16_t(actor.rotation.z) - uint16_t(kRollOverStep));
        if (actor.rotation.z != 0) return;
        restartWander(roller);
        actor.acceleration.z = 0;
        roller.yawDrift = 0;
        actor.subState = uint8_t(RollerMove::Wander);
        roller.damageTaken = 0;
        actor.velocity.z = int16_t(speedOf(actor));
        return;
    default: return;
    }
}

// ---- the lift's sub-states [0x8cf6f8] ------------------------------------------------------------------------------
// the velocity accelerated and integrated (exe 0x5e3690)
void fall(StaticActor& actor) {
    actor.velocity.y = int16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y));
    actor.velocity.z = int16_t(uint16_t(actor.velocity.z) + uint16_t(actor.acceleration.z));
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
}
// exe 0x5ae110: flying; a wall or a deflecting / solid body -> the death; else its thrown hit entry
void thrown(const PlayerEnv& env, StaticActor& actor, RollerFields& roller) {
    actor.savePreviousPose();
    fall(actor);
    if (int32_t(actorTestMove(env, actor)) < 0) { // exe 0x40f570
        detach(actor, roller);
        return;
    }
    if (roller.hitBox.result & (kHitAttackDeflects | kHitAttackSolid)) {
        detach(actor, roller);
        return;
    }
    submitHit(actor, roller, kThrownEntry, kHitAttackByPlayer | kHitAttackThrown | 0x40);
}
// exe 0x5ae230: falling until back at its floor (0x20 - variant * 0x20), bouncing off the walls; a wall below or a
// deflecting body -> the death; at its floor the wander again
void dropped(const PlayerEnv& env, StaticActor& actor, RollerFields& roller) {
    actor.savePreviousPose();
    fall(actor);
    const uint32_t v = actor.variant;
    const int floorY = int(0x20 - int32_t(v << 5));
    if (actor.pos.y.whole < floorY) {
        if (int32_t(actorTestMove(env, actor)) < 0) {
            const uint32_t r = actorPushOut(env, actor); // exe 0x410960
            if (r & 0xf00) reflectTravelYaw(roller, r);
            if (r & 0x1000) {
                detach(actor, roller);
                return;
            }
        }
        if (roller.hitBox.result & kHitAttackDeflects) {
            detach(actor, roller);
            return;
        }
    } else { // back in: the move's wander
        actor.pos.y.whole = int16_t(0x20u - (v << 5));
        actor.velocity.setXY(0, 0);
        actor.velocity.z = int16_t(speedOf(actor));
        actor.acceleration.y = 0;
        roller.wobble = int8_t((v + 4u) << 4);
        roller.yawDrift = 0;
        roller.travelYaw = actor.rotation.y;
        roller.pauseOrTarget = 0;
        actor.enterStateBytes(uint8_t(RollerState::Move));
        roller.timer = int16_t((v << 4) + 0x3cu);
    }
    submitHit(actor, roller, kThrownEntry, 0x40);
}
// exe 0x5ae020: wobbling at the player's hands until released: thrown (along his aim) or dropped
void carried(const PlayerEnv& env, StaticActor& actor, RollerFields& roller) {
    const int8_t e = int8_t(roller.wobble - 1);
    roller.wobble = e;
    if (e < 8) actor.rotation.x = int16_t(trig64Tables().sine[size_t(int32_t(e) * 8)] >> 5);
    if (e == 0) roller.wobble = 0x18;
    carryAtHands(actor, 0, -0x10, 0); // exe 0x573ab0
    const int r = int(carryReleased()); // exe 0x573950
    if (r == 0) return;
    if (r == 1) {
        int32_t v[3];
        playerAimVector(0, -0x100, -0x400, v); // exe 0x5e3e50
        actor.velocity.x = 0;
        actor.velocity.y = int16_t(v[1]);
        actor.velocity.z = int16_t(v[2]);
        actor.acceleration.y = 0x40;
        actor.rotation.x = 0;
        carryDropFromPlayer(actor); // exe 0x573990
        actor.subState = uint8_t(RollerLift::Thrown);
        thrown(env, actor, roller);
        return;
    }
    actor.velocity.y = int16_t(0xfe00);
    actor.velocity.z = int16_t(0xff00);
    actor.acceleration.y = 0x40;
    actor.rotation.x = 0;
    carryDropFromPlayer(actor);
    actor.subState = uint8_t(RollerLift::Dropped);
    dropped(env, actor, roller);
}

// ---- the states [0x8cf6d8] -----------------------------------------------------------------------------------------
// exe 0x5ad7f0: the model, the hit points (the row's << 4), its boxes (liftable); a rise first when its spawn asks
void bind(StaticActor& actor, RollerFields& roller) {
    if (!bindActorModel(actor, actor.spawnByte)) { // exe 0x5e0bc0(actor, actor + 7)
        freeActor(actor);                          // exe 0x5e1540
        return;
    }
    const StaticActorStats row = staticStats(actor.type, 0);
    actor.clearMotion();       // exe 0x40dc40
    roller.clearMoveBlockBytes();
    const int16_t yaw = actor.rotation.y;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    const int16_t h = int16_t(uint16_t(row.hitPoints << 4));
    actor.maxHitPoints = h;
    actor.hitPoints = h;
    roller.yawDrift = 0;
    roller.wobble = int8_t((actor.variant + 4u) << 4);
    roller.travelYaw = yaw;
    roller.pauseOrTarget = 0;
    const uint8_t wait = roller.riseWait;
    roller.damageTaken = 0;
    actor.collisionBox.pointAt(TableId::St43RollerBox);
    actor.interactionBox.pointAt(TableId::St43RollerLiftBox);
    actor.interaction = kInteractLift;
    if (wait == 0) {
        actor.state = uint8_t(RollerState::Move);
        actor.velocity.z = int16_t(speedOf(actor));
        roller.timer = int16_t((uint32_t(actor.variant) << 4) + 0x3cu);
    } else {
        actor.state = uint8_t(RollerState::Rise);
        actor.velocity.z = 0;
        actor.scale.y = 0;
        roller.timer = 0x3c;
    }
}
// exe 0x5ad8e0: 60 ticks growing (scale y 0 -> 0x200) and rising (y 0x40 -> 0x40 - (variant + 1) * 32)
void rise(StaticActor& actor, RollerFields& roller) {
    roller.timer = int16_t(roller.timer - 1);
    const int16_t t = roller.timer;
    if (t == 0) {
        actor.state = uint8_t(RollerState::Move);
        actor.velocity.z = int16_t(speedOf(actor));
        roller.timer = int16_t((uint32_t(actor.variant) << 4) + 0x3cu);
        return;
    }
    const int32_t k = 0x3c - int32_t(t);
    actor.scale.y = int16_t(int32_t(uint32_t(k) << 9) / 60);
    const int32_t dy = int32_t((uint32_t(actor.variant) + 1u) * uint32_t(k) << 5) / 60;
    actor.pos.y.whole = int16_t(0x40 - dy);
}
// exe 0x5ad980: the sub-state, the speed, the wobble's yaw sway, moved along the travel yaw (bouncing off the walls),
// hurt by the player's hits (killed: the death), killed by a thrown body, pushed back by bumps and the player's
// shield; liftable
void move(const PlayerEnv& env, StaticActor& actor, RollerFields& roller) {
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    switch (RollerMove(actor.subState)) {
    case RollerMove::Wander: wander(actor, roller); break;
    case RollerMove::TurnToPlayer: turnToPlayer(actor, roller); break;
    case RollerMove::RollOver: rollOver(actor, roller); break;
    default: notPortedAt(kStoryPort, "0x5ad980 sub-state (table 0x8cf6ec runs into table 0x8cf6f8)", actor.subState);
    }
    actor.velocity.z = int16_t(uint16_t(actor.velocity.z) + uint16_t(actor.acceleration.z));
    actor.velocity.y = int16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y));
    if (RollerMove(actor.subState) != RollerMove::RollOver) {
        const int8_t e = int8_t(roller.wobble - 1);
        roller.wobble = e;
        if (e < 0x10)
            actor.rotation.y = int16_t(uint16_t(actor.rotation.y) +
                                       uint16_t(int16_t(trig64Tables().sine[size_t(int32_t(e) * 4)] >> 4)));
        if (e == 0) {
            roller.wobble = 0x40;
            roller.yawDrift = int8_t((rngStep() & 0x1fu) - 0x10u);
        }
    }
    moveAlongYaw(actor.pos, roller.travelYaw, 0, -int(actor.velocity.y), -int(actor.velocity.z)); // exe 0x5e36f0
    if (int32_t(actorTestMove(env, actor)) < 0) {
        const uint32_t r = actorPushOut(env, actor);
        if (r & 0xf00) reflectTravelYaw(roller, r);
    }
    const uint32_t box = roller.hitBox.result;
    if (box & kHitAttackByPlayer) {
        actor.tint = kTintFlash;
        const uint32_t dmg = box & kHitDamageMask;
        actor.velocity.z = int16_t(-int(int16_t(speedOf(actor))));
        if (dmg != 0) {
            if (RollerMove(actor.subState) != RollerMove::RollOver)
                roller.damageTaken = int16_t(uint16_t(roller.damageTaken) + dmg);
            if (hitPointsAdd(actor, -int(dmg)) != 0) { // exe 0x5e52f0
                detach(actor, roller);
                return;
            }
        }
    }
    if (box & kHitAttackThrown) {
        detach(actor, roller);
        return;
    }
    if (box & kHitAttackBump) actor.velocity.z = int16_t(-int(int16_t(speedOf(actor))));
    if (roller.shieldCooldown != 0) {
        roller.shieldCooldown = int16_t(roller.shieldCooldown - 1);
    } else if (playerShieldBlocks(actor, 0x40)) { // exe 0x5737b0(actor, 0x40)
        roller.shieldCooldown = 4;
        actor.velocity.z = int16_t(-int(int16_t(speedOf(actor))));
    }
    if (playerLiftTargetCheck(actor, 0)) { // exe 0x573a60(actor, 0)
        actor.enterState(uint8_t(RollerState::Lifted));
        actor.rotation.z = 0;
    }
    submitHit(actor, roller, kBodyEntry, kHitAttackSolid | 2);
}
// exe 0x5ae000
void lifted(const PlayerEnv& env, StaticActor& actor, RollerFields& roller) {
    switch (RollerLift(actor.subState)) {
    case RollerLift::Carried: carried(env, actor, roller); return;
    case RollerLift::Thrown: thrown(env, actor, roller); return;
    case RollerLift::Dropped: dropped(env, actor, roller); return;
    default: notPortedAt(kStoryPort, "0x5ae000 sub-state (table 0x8cf6f8 runs into other data)", actor.subState);
    }
}
// exe 0x5ae420: a blast (static global type 7), the drops, freed
void die(StaticActor& actor) {
    const StaticActorStats row = staticStats(actor.type, 0);
    const uint32_t slot = allocActor60(0); // exe 0x5e1300
    if (slot) {
        StaticActor& blast = staticActorAt(slot);
        blast.flags = kActorLive;
        blast.loopFlags |= kActorSweepGroup2;
        blast.type = typeByte(GlobalStaticType::Blast);
        blast.variant = 0;
        blast.pos = actor.pos;
        BlastParams params(actorHeaderAt(slot));
        params.size = kBlastSize;
        params.attack = 0;
    }
    spawnDrops(actor.pos, row.dropTable); // exe 0x5f1270
    freeActor(actor);                     // exe 0x5e1540
}
// exe 0x5ad7b0
void rollerTick(const PlayerEnv& env, StaticActor& actor) {
    RollerFields& fields = classState<RollerFields>(actor);
    switch (RollerState(actor.state)) {
    case RollerState::Bind: bind(actor, fields); break;
    case RollerState::Rise: rise(actor, fields); break;
    case RollerState::Move: move(env, actor, fields); break;
    case RollerState::Lifted: lifted(env, actor, fields); break;
    case RollerState::Death: die(actor); break;
    default:
        notPortedAt(kStoryPort, "0x5ad7b0 state (table 0x8cf6d8 runs into tables 0x8cf6ec / 0x8cf6f8)", actor.state);
    }
    // exe 0x5f20f0(actor, 0x40, 0x100, 0) unless carried: the shadow (render output)
}

} // namespace

void rollerUpdate(const PlayerEnv& env, uint32_t actor) { rollerTick(env, staticActorAt(actor)); }

} // namespace od::port
