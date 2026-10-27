// The pirates' tank of the Saul Kada pirate attack (ST29 / ST3B; pool 0x20 type 0xd, exe 0x46a290; hypothesis names).
// States [0x89015c]: 0 bind 0x46a460, 1 run 0x46a530 (by variant [0x890174]: 0 the full run 0x46a5d0; the table
// overlaps the sub-state table, so variants 1..8 run sub-state variant - 1 directly), 2 destroyed 0x46ade0 (sub-state
// 0: 0x46ae00, else nothing), 3 freed 0x468fa0.
// The full run: the player standing on it (unless not rideable) -> ridden; a hit (unless no damage): strong -> the
// recoil, dead -> destroyed; then its sub-state [0x890178], its turret (exe 0x46b360), its move (0x46af30) and its tilt
// to the ground (0x4bdeb0).
// Sub-states: 0 reset 0x46a650 (-> 1), 1 drive until the player is within 0x1770 0x46a740, 2 drive and pick 0x46a880
// (the cannon when facing and on screen, the machine gun within 0x640, back past 0x1838), 3 machine-gun bursts
// 0x46a8e0 (the turret on the player, the burst cycle 0x46b850), 4 the cannon 0x46a9a0 (brake, the turret turned,
// the shell 0x46b630, rolled back, 3 times), 5 the recoil of a hit 0x46ac30, 6 ridden 0x46acf0 (braking until he
// gets off), 7 parked until its attack-lock bit 0x46ad80. Below a quarter of its hit points it smokes. The shadow is
// render output.
#include "content/saul_kada/pirate_tank.hpp"
#include "content/saul_kada/pirate_tank_state.hpp"

#include "content/saul_kada/pirate_servbots.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/enemy_motion.hpp"
#include "game/enemy_shot.hpp"
#include "game/explosions.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/lobbed_shot.hpp"
#include "game/matrix.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/room_grid.hpp"
#include "game/spark_emitter.hpp"
#include "game/spark_emitter_state.hpp"
#include "game/story.hpp"
#include "game/visibility.hpp"

namespace od::port {

using namespace pirate_tank_state;

namespace {

enum class TankState : uint8_t { Bind = 0, Run = 1, Destroyed = 2, Free = 3 };
enum class TankSub : uint8_t { Reset = 0, Drive = 1, Pick = 2, Guns = 3, Cannon = 4, Recoil = 5, Ridden = 6, Parked = 7 };
enum class TurretState : uint8_t {
    Still = 0, SwingStart = 1, Swing = 2, Settle = 3, StepToPlayer = 4, StepToTarget = 5, Stepping = 6, Track = 7,
    Center = 8, TrackAway = 9
};
enum class BurstState : int8_t { Off = 0, Start = 1, Fire = 2, Pause = 3 };
enum class CannonStep : uint8_t { Fire = 0, Recoil = 1, Wait = 2 };
// its status
constexpr uint8_t kStatusTurretLimit = 0x01, kStatusWall = 0x02, kStatusProbed = 0x04;
// its flags
constexpr uint8_t kTankFireShell = 0x01, kTankBoneTweak = 0x02, kTankProbeMode = 0x04, kTankNoDamage = 0x08,
                  kTankFireBullet = 0x10, kTankNoLockOn = 0x20, kTankNotRideable = 0x40, kTankNoHit = 0x80;
constexpr uint8_t kTankFired = kTankFireShell | kTankFireBullet; // cleared once fired
// its treads' bits: rolling, the bits kept as it stops, the bits kept as it brakes
constexpr uint8_t kTreadsRolling = 0x01, kTreadsKept = 0x3e, kTreadsBrakeKept = 0xe7;
const DataTable<ScalarTable<uint8_t, 0x10>> modelVariants{TableId::PirateTankModelVariants}; // by variant
const DataTable<ScalarTable<uint8_t, 0x10>> fireTimers{TableId::PirateTankFireTimers}, turnTimers{TableId::PirateTankTurnTimers}, stepTimers{TableId::PirateTankStepTimers}; // by an RNG roll & 0xf
constexpr uint16_t kHurtSound = 0x8d, kStrongHurtSound = 0x8e;
constexpr int16_t kTurretLimit = 0x600;
constexpr uint16_t kExplosionTint = 0x4214;
constexpr uint32_t kRiderMatch = 0x1fffff;

uint8_t fireTimer() { return fireTimers[rngStep() & 0xf]; } // the inlined [0x890128 + (rng & 0xf)]
int8_t burstTicks(int8_t index) { return dataScalar<int8_t>(stateAddress(TableId::PirateTankBurstSequence) + uint32_t(int32_t(index))); }
uint8_t recoilTimer(int8_t shotsLeft) { return dataScalar<uint8_t>(stateAddress(TableId::PirateTankRecoilTimers) - uint32_t(int32_t(shotsLeft))); }
void sound(uint16_t id, const AnimatedActor& actor) { soundPlayAtPos(id, actor.pos); } // exe 0x5d5380
// its row: its type's, by its variant (+0 hit points / 4, +1 its solid's attack, +3 the bullets', +4 its entry's
// height, +5 its drops); the shell reads +2 of the variant-0 row
AnimatedActorStats tankStats(const AnimatedActor& actor) { return animatedStats(actor.type, actor.variant); }
bool onScreenOrNear(const AnimatedActor& actor) { return pointVisibleOrNear(actor.pos, 0) == 0; } // exe 0x5e59a0
// exe 0x59beb0: the player standing on it
bool ridden(const AnimatedActor& actor) {
    const Player& p = player();
    return p.onPlatform != 0 && ((slotIdentityWord(p.platform) ^ slotIdentityWord(actor)) & kRiderMatch) == 0;
}
int turretAngle(const AnimatedActor& actor, const TankFields& tank) { return int(tank.turretYaw) + int(actor.rotation.y); }
int angleToPlayer(const AnimatedActor& actor) { return angleOf(player().pos, actor.pos); }

// exe 0x4bfda0(actor, limit, step): its speed toward the limit (reached: -limit), turned to its drive's yaw, moved;
// true once facing it
bool driveTurn(AnimatedActor& actor, const TankFields& tank, int limit, int step) {
    if (limit >= 0 && speedToward(actor, limit) != 0) actor.velocity.z = int16_t(-limit);
    const int d = turnYawTo(actor.rotation.y, uint32_t(int32_t(tank.driveYaw)), step); // exe 0x5e3df0
    integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z);         // exe 0x5e3690
    return !(uint32_t(step * 2) < uint32_t(d + step));
}
// exe 0x4bdeb0: its pitch toward the pitch target, its roll toward the roll target (their steps growing)
void tilt(AnimatedActor& actor, TankFields& tank) {
    {
        const int s = actor.spin.x;
        const int d = angleDiff(uint32_t(int32_t(actor.rotation.x)), uint32_t(int32_t(tank.pitchTarget)));
        if (uint32_t(d + s) <= uint32_t(s + s)) {
            actor.rotation.x = tank.pitchTarget;
        } else {
            const uint16_t step = uint16_t(actor.spin.x);
            actor.rotation.x = int16_t(d > 0 ? uint16_t(uint16_t(actor.rotation.x) + step) : uint16_t(uint16_t(actor.rotation.x) - step));
            actor.spin.x = int16_t(uint16_t(uint16_t(actor.spin.z) + step));
        }
    }
    const int s = actor.spin.y;
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.z)), uint32_t(int32_t(tank.rollTarget)));
    if (uint32_t(d + s) <= uint32_t(s + s)) {
        actor.rotation.z = tank.rollTarget;
        return;
    }
    const uint16_t step = uint16_t(actor.spin.y);
    actor.rotation.z = int16_t(d > 0 ? uint16_t(uint16_t(actor.rotation.z) + step) : uint16_t(uint16_t(actor.rotation.z) - step));
    actor.spin.y = int16_t(uint16_t(uint16_t(tank.rollGrowth) + step));
}
// exe 0x4699a0: a hit of the player's: the white flash, its sound (a strong one: 1), its hit points (dead: 2)
int takeHit(AnimatedActor& actor, const TankFields& tank) {
    const uint32_t result = tank.hitBox.result, damage = result & kHitDamageMask;
    if (!(result & kHitAttackByPlayer)) return 0;
    int outcome = 0;
    actor.tint = kTintFlash;
    if (result & kHitAttackStrongPush) {
        outcome = 1;
        sound(kStrongHurtSound, actor);
    } else {
        sound(kHurtSound, actor);
    }
    if (hitPointsAdd(actor, -int(damage)) != 0) return 2; // exe 0x5e52f0
    return outcome;
}

// ---- the weapons -------------------------------------------------------------------------------------------------
// exe 0x5e3970(out, {pitch, yaw + the turret's, roll}, 0, y, z): the muzzle's offset through the hull and the turret
void muzzle(const AnimatedActor& actor, const TankFields& tank, int y, int z, int32_t out[3]) {
    const int16_t rot[3] = {actor.rotation.x, int16_t(uint16_t(uint16_t(tank.turretYaw) + uint16_t(actor.rotation.y))),
                            actor.rotation.z};
    rotateVector(rot, 0, y, z, out);
}
// exe 0x469e60(actor, yaw): a machine-gun bullet (the enemy shot, variant 0) 0x140 above the hull at the muzzle,
// pitched from the player, its yaw the turret's + 0x800
void fireBullet(const AnimatedActor& actor, const TankFields& tank, int yaw) {
    int32_t o[3];
    muzzle(actor, tank, -0x1000, 0xf00, o);
    const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!slot) return;
    ProjectileActor& bullet = projectileActorAt(slot);
    EnemyShotFields shot(projectileActorAt(slot));
    bullet.flags = kActorLive;
    bullet.loopFlags |= kActorSweepGroup2;
    bullet.type = kEnemyShotType;
    bullet.variant = 0;
    bullet.pos.x = int32_t(uint32_t(actor.pos.x) + uint32_t(o[0]));
    bullet.pos.y = int32_t(uint32_t(actor.pos.y) - 0x1400000u);
    bullet.pos.z = int32_t(uint32_t(actor.pos.z) + uint32_t(o[2]));
    bullet.rotation.x = int16_t(0x800 - yaw - pitchOf(player().pos, bullet.pos)); // exe 0x5e3ca0(player, bullet)
    bullet.rotation.z = 0;
    shot.ticks = 0x2d;
    bullet.rotation.y = int16_t(uint16_t(uint16_t(tank.turretYaw) + uint16_t(actor.rotation.y) + 0x800u));
    shot.attack = tankStats(actor).shotAttack;
}
// exe 0x46b630(actor, near): the cannon shell (the lobbed shot, variant 0) from the muzzle, lobbed at the player 0x5a
// above his feet (jittered by up to +-0x400 unless an RNG mask test says otherwise) or, near, a short fixed lob; the
// muzzle flash (the global hit spark, variant 3)
void fireShell(const AnimatedActor& actor, const TankFields& tank, int8_t nearLob) {
    int32_t o[3];
    muzzle(actor, tank, -0x1000, -0xe00, o);
    const Player& p = player();
    int32_t target[3] = {p.pos.x, int32_t((uint32_t(p.pos.y) & 0xffffu) | uint32_t(uint16_t(uint16_t(p.pos.y.whole) - 0x5au)) << 16),
                         p.pos.z};
    const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!slot) return;
    ProjectileActor& shell = projectileActorAt(slot);
    LobbedShotParams params(projectileActorAt(slot));
    shell.flags = kActorLive;
    shell.loopFlags |= kActorSweepGroup2;
    shell.type = kLobbedShotStageType;
    shell.variant = 0;
    shell.pos.x = int32_t(uint32_t(actor.pos.x) + uint32_t(o[0]));
    shell.pos.y = int32_t(uint32_t(actor.pos.y) + uint32_t(o[1]));
    shell.pos.z = int32_t(uint32_t(actor.pos.z) + uint32_t(o[2]));
    shell.rotation.x = 0;
    shell.rotation.z = 0;
    shell.rotation.y = int16_t(uint16_t(uint16_t(tank.turretYaw) + uint16_t(actor.rotation.y)));
    if (nearLob == 0) {
        const uint32_t mask = onScreenOrNear(actor) ? 0x1552900u : 0u;
        const uint32_t r = rngStep();
        if (int32_t(mask << (r & 0x1f)) >= 0) { // jittered
            const uint16_t dx = uint16_t((int32_t(rngStep() & 0xf) - 8) << 7);
            target[0] = int32_t((uint32_t(target[0]) & 0xffffu) | uint32_t(uint16_t((uint32_t(target[0]) >> 16) + dx)) << 16);
            const uint16_t dz = uint16_t((int32_t(rngStep() & 0xf) - 8) << 7);
            target[2] = int32_t((uint32_t(target[2]) & 0xffffu) | uint32_t(uint16_t((uint32_t(target[2]) >> 16) + dz)) << 16);
        }
        const int32_t from[3] = {shell.pos.x, shell.pos.y, shell.pos.z};
        shell.velocity.x = 0;
        shell.velocity.y = int16_t(launchSpeed(from, target, 0x600, 0x30, 0x1770)); // exe 0x5e3ee0
        shell.velocity.z = int16_t(0xfa00);
    } else {
        shell.velocity.x = 0;
        shell.velocity.y = int16_t(0xff00);
        shell.velocity.z = int16_t(0xfc00);
    }
    shell.acceleration.y = 0x30;
    params.size = 0x20;
    params.damage = int16_t(animatedStats(actor.type).strongAttack);
    params.launchSound = 0xad;
    if (const uint32_t flashSlot = allocActorA0(0)) { // exe 0x5e13b0
        EffectActor& flash = effectActorAt(flashSlot);
        flash.flags = kActorLiveDrawn;
        flash.type = typeByte(GlobalEffectType::HitSpark);
        flash.variant = 3;
        flash.loopFlags |= kActorSweepGroup2;
        flash.pos.x = shell.pos.x.whole;
        flash.pos.y = shell.pos.y.whole;
        flash.pos.z = shell.pos.z.whole;
        classState<HitSparkFields>(effectActorAt(flashSlot)).size = 0xc0;
    }
}

// ---- the ground --------------------------------------------------------------------------------------------------
// exe 0x4693e0(actor, point, mode): the centre of the grid cell under the point (its integer parts); a floor cell or
// one without segments: its floor height, no flag; else the probed status and the flag (mode != 0 would probe at the
// cell's centre)
struct Ground {
    int16_t x, y, z, flag;
};
Ground cellGround(TankFields& tank, int16_t px, int16_t pz, int mode) {
    tank.status &= uint8_t(~kStatusProbed);
    const int32_t cz = (int32_t(pz) + 0x8100) >> 9, cx = (int32_t(px) + 0x8100) >> 9;
    const GridCell cell(gridCellAtIndex(uint32_t(cx), uint32_t(cz)));
    Ground g{int16_t(uint16_t(uint32_t(cx - 0x40) << 9)), 0, int16_t(uint16_t(uint32_t(cz - 0x40) << 9)), 0};
    if (((uint32_t(cell.head) ^ 0x800u) & 0x4800u) != 0) {
        g.y = int16_t(uint16_t(-int32_t((uint32_t(cell.corners) >> 12) & 0xff0)));
        return g;
    }
    // mode != 0 probes at the cell centre with the y of an uninitialised stack word (never set in this class: its
    // probe-mode flag is not written by exe 0x46a290)
    if (mode != 0)
        notPorted(kStoryPort, "0x4693e0: the ground probe of mode 1 (+0x164 & 4) reads an uninitialised stack word");
    g.flag = 1; // y: an uninitialised stack word, replaced by the caller (mode 0)
    tank.status |= kStatusProbed;
    return g;
}
// exe 0x46afd0 (the pitch: the ground ahead / behind) / 0x46b190 (the roll: left / right): two ground points 3 * 64
// from the hull; the angle atan(their height difference, their distance); bit 31: a probed cell
int groundSlope(const AnimatedActor& actor, TankFields& tank, int mode, int yawA, int yawB, bool roll) {
    const Trig64Tables& trig = trig64Tables();
    auto side = [&](int yaw, int16_t& y) {
        const uint32_t i = uint32_t(yaw >> 6) & 0x3f;
        const int32_t c = trig.sine[i], s = trig.cosine[i];
        const int16_t px = int16_t(uint16_t((-(c * 3) << 6) >> 12) + uint16_t(actor.pos.x.whole));
        const int16_t pz = int16_t(uint16_t((-(s * 3) << 6) >> 12) + uint16_t(actor.pos.z.whole));
        Ground g = cellGround(tank, px, pz, mode);
        y = g.y;
        if ((mode ^ 1) & g.flag) y = actor.pos.y.whole;
        return g;
    };
    int16_t y1 = 0, y2 = 0;
    const Ground g1 = side(yawA, y1);
    const Ground g2 = side(yawB, y2);
    const int dx = int(g1.x) - int(g2.x), dz = int(g1.z) - int(g2.z);
    const int d = isqrt(int32_t(uint32_t(dz * dz) + uint32_t(dx * dx))); // exe 0x40b980
    int r = 0;
    if (y1 > y2) r = atanAngle(float(int(y1) - int(y2)), float(d)); // exe 0x40b960
    if (y1 < y2) r = -atanAngle(float(int(y2) - int(y1)), float(d));
    uint32_t out = uint32_t(r) & 0xffff;
    if (roll ? (g1.flag | g2.flag) != 0 : g1.flag != 0) out |= 0x80000000u;
    return int(out);
}
// exe 0x46af30: the walls (pushed out, back on a failure; a wall noted), on the ground + 0x10, its pitch / roll targets
void move(const PlayerEnv& env, AnimatedActor& actor, TankFields& tank) {
    tank.status &= uint8_t(~kStatusWall);
    if (int32_t(actorTestMove(env, actor)) < 0) {        // exe 0x40f570
        const uint32_t result = actorPushOut(env, actor); // exe 0x410960
        if (int32_t(result) < 0) actor.pos = actor.prevPos;
        if (result & 0xf00) tank.status |= kStatusWall;
    }
    actor.pos.y.whole = int16_t(uint16_t(uint32_t(actorGround(env, actor) + 0x10))); // exe 0x40f190
    const int yaw = actor.rotation.y;
    if (!(tank.flags & kTankProbeMode)) {
        tank.pitchTarget = int16_t(uint16_t(uint32_t(groundSlope(actor, tank, 0, yaw, yaw - 0x800, false))));
        tank.rollTarget = int16_t(uint16_t(uint32_t(groundSlope(actor, tank, 0, yaw - 0x400, yaw + 0x400, true))));
        return;
    }
    tank.pitchTarget = int16_t(uint16_t(uint32_t(groundSlope(actor, tank, 1, yaw, yaw - 0x800, false))));
}

// ---- the turret --------------------------------------------------------------------------------------------------
// exe 0x469960: the turret moved by its step; true once within 0x40 of its target
bool turretStepToward(const AnimatedActor& actor, TankFields& tank) {
    tank.turretYaw = int16_t(uint16_t(uint16_t(tank.turretYaw) + uint16_t(int16_t(tank.turretStep))));
    const int d = angleDiff(uint32_t(turretAngle(actor, tank)), uint32_t(int32_t(tank.turretTarget)));
    return uint32_t(d + 0x40) <= 0x80u;
}
// the +-0x20 step toward the angle, or the snap onto it (relative to the hull)
void turretToward(const AnimatedActor& actor, TankFields& tank, int angle, int d) {
    if (uint32_t(d + 0x20) > 0x40u) {
        tank.turretYaw = int16_t(uint16_t(uint16_t(tank.turretYaw) + (d > 0 ? 0x20u : 0xffe0u)));
        return;
    }
    tank.turretYaw = int16_t(angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angle)));
}
// exe 0x46b360: the turret by its state: swinging toward the player (accelerating, overshooting, settling), stepping
// at +-0x20 toward a target, tracking the player, back to the front, tracking the point opposite the player; clamped
// to +-0x600 (the limit noted)
void turret(const AnimatedActor& actor, TankFields& tank) {
    switch (TurretState(tank.turretState)) {
    case TurretState::SwingStart: {
        const int angle = angleToPlayer(actor);
        tank.turretTarget = int16_t(angle);
        const int d = angleDiff(uint32_t(turretAngle(actor, tank)), uint32_t(int32_t(int16_t(angle))));
        if (d == 0) {
            tank.turretState = uint8_t(TurretState::Still);
            break;
        }
        tank.turretState = uint8_t(TurretState::Swing);
        tank.turretStep = int8_t(d <= 0 ? 0xf0 : 0x10);
        tank.turretAccel = tank.turretStep;
        [[fallthrough]];
    }
    case TurretState::Swing: {
        const int8_t step = int8_t(uint8_t(uint8_t(tank.turretStep) + uint8_t(tank.turretAccel)));
        tank.turretStep = step;
        if (uint32_t(int(step) + 0x40) > 0x80u) tank.turretStep = int8_t(step <= 0 ? 0xc0 : 0x40);
        if (turretStepToward(actor, tank)) {
            tank.turretState = uint8_t(TurretState::Settle);
            tank.turretAccel = int8_t(-tank.turretAccel);
        }
        break;
    }
    case TurretState::Settle: {
        tank.turretYaw = int16_t(uint16_t(uint16_t(tank.turretYaw) + uint16_t(int16_t(tank.turretStep))));
        const int8_t step = int8_t(uint8_t(uint8_t(tank.turretStep) + uint8_t(tank.turretAccel)));
        tank.turretStep = step;
        if (uint32_t(int(step) + 0x10) <= 0x20u) {
            tank.turretStep = 0;
            tank.turretState = uint8_t(TurretState::StepToTarget);
        }
        break;
    }
    case TurretState::StepToPlayer: tank.turretTarget = int16_t(angleToPlayer(actor)); [[fallthrough]];
    case TurretState::StepToTarget: {
        const int d = angleDiff(uint32_t(turretAngle(actor, tank)), uint32_t(int32_t(tank.turretTarget)));
        if (d == 0) {
            tank.turretState = uint8_t(TurretState::Still);
            break;
        }
        tank.turretState = uint8_t(TurretState::Stepping);
        tank.turretStep = int8_t(d <= 0 ? 0xe0 : 0x20);
        [[fallthrough]];
    }
    case TurretState::Stepping:
        if (turretStepToward(actor, tank)) {
            tank.turretState = uint8_t(TurretState::Still);
            tank.turretAccel = int8_t(uint8_t(uint8_t(tank.turretTarget) - uint8_t(actor.rotation.y)));
        }
        break;
    case TurretState::Track: {
        const int angle = angleToPlayer(actor);
        turretToward(actor, tank, angle, angleDiff(uint32_t(turretAngle(actor, tank)), uint32_t(angle)));
        break;
    }
    case TurretState::Center: {
        const int16_t yaw = tank.turretYaw;
        if (uint32_t(int(yaw) + 0x40) > 0x80u) {
            tank.turretYaw = int16_t(yaw > 0 ? uint16_t(yaw - 0x40) : uint16_t(yaw + 0x40));
        } else {
            tank.turretYaw = 0;
            tank.turretState = uint8_t(TurretState::Still);
        }
        break;
    }
    case TurretState::TrackAway: {
        const int angle = angleToPlayer(actor) + 0x800;
        turretToward(actor, tank, angle, angleDiff(uint32_t(turretAngle(actor, tank)), uint32_t(angle)));
        break;
    }
    default: break;
    }
    tank.status &= uint8_t(~kStatusTurretLimit);
    if (tank.turretYaw > kTurretLimit) {
        tank.turretYaw = kTurretLimit;
        tank.status |= kStatusTurretLimit;
    }
    if (tank.turretYaw < -kTurretLimit) {
        tank.turretYaw = int16_t(-kTurretLimit);
        tank.status |= kStatusTurretLimit;
    }
}

// ---- the sub-states [0x890178] -----------------------------------------------------------------------------------
void runSub(const PlayerEnv& env, AnimatedActor& actor, TankFields& tank, TankSub sub);
void enterSub(AnimatedActor& actor, TankSub sub) {
    actor.subState = uint8_t(sub);
    actor.restartStep();
}
// exe 0x46abe0: the next sub-state by the player's distance (past 0x1770 drive, past 0x640 the guns, else pick)
void pick(AnimatedActor& actor) {
    const int d = planarDistance(actor.pos, player().pos);
    if (d > 0x1770) enterSub(actor, TankSub::Drive);
    else if (d > 0x640) enterSub(actor, TankSub::Guns);
    else enterSub(actor, TankSub::Pick);
}
void pickAfter(AnimatedActor& actor, TankFields& tank) { // the inlined "its timer from the fire timers, exe 0x46abe0"
    tank.timer = fireTimer();
    pick(actor);
}
// exe 0x46a670: stopped, its accelerations / tilt steps, its timers, the guns idle
void reset(AnimatedActor& actor, TankFields& tank) {
    actor.anim = 0;
    actor.velocity.x = 0;
    actor.velocity.y = 0;
    actor.velocity.z = 0;
    actor.acceleration.x = 0;
    actor.acceleration.z = int16_t(0xfff8);
    actor.spin.x = 4;
    actor.spin.z = 2;
    actor.spin.y = 8;
    tank.rollGrowth = 0;
    tank.timer = fireTimer();
    tank.turnTimer = turnTimers[rngStep() & 0xf];
    tank.resetMark = 1;
    tank.swayDir = uint16_t(rngStep() & 1);
    tank.burst = int8_t(BurstState::Off);
    tank.treads &= kTreadsKept;
}
// exe 0x46a770: driving -- the hull kept turned away from the player (within 0x300 its turn timer restarts); the timer
// out: turned toward that yaw (exe 0x4bfda0 with its step timer as the step), stopped
void drive(AnimatedActor& actor, TankFields& tank) {
    switch (actor.step) {
    case 0:
        actor.anim = 0;
        tank.treads = uint8_t((tank.treads & kTreadsKept) | kTreadsRolling);
        actor.velocity.z = 0;
        tank.flags = 0;
        tank.turretState = uint8_t(TurretState::Still);
        actor.enterStep(1);
        [[fallthrough]];
    case 1: {
        const int angle = angleToPlayer(actor);
        if (uint32_t(angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angle)) + 0x300) < 0x600u) {
            tank.turnTimer = turnTimers[rngStep() & 0xf];
            return;
        }
        tank.turnTimer -= 1u;
        if (tank.turnTimer != 0) return;
        tank.driveYaw = int16_t(angle);
        tank.stepTimer = stepTimers[rngStep() & 0xf];
        actor.enterStep(2);
        return;
    }
    case 2:
        if (driveTurn(actor, tank, 0, int8_t(tank.stepTimer))) actor.enterStep(1);
        return;
    default: return;
    }
}
// exe 0x467b30(actor, distance): the cannon: after its timer, the player within 0x1770 and in front (+-0x600), the tank
// on screen (or by an RNG mask test)
bool cannonReady(const AnimatedActor& actor, TankFields& tank, int distance) {
    if (tank.timer != 0) {
        tank.timer -= 1u;
        return false;
    }
    if (distance >= 0x1770) return false;
    if (uint32_t(angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleToPlayer(actor))) + 0x600) > 0xc00u)
        return false;
    if (onScreenOrNear(actor)) return true;
    return int32_t(0x15a400u << (rngStep() & 0x1f)) < 0;
}
// exe 0x467bc0(actor, distance): within 0x800, the turret on the player (+-0x200) and no burst running -> a burst
void gunReady(const AnimatedActor& actor, TankFields& tank, int distance) {
    if (distance >= 0x800) return;
    const int angle = angleToPlayer(actor);
    if (uint32_t(angleDiff(uint32_t(turretAngle(actor, tank) + 0x800), uint32_t(angle)) + 0x200) > 0x400u) return;
    if (tank.burst != 0) return;
    tank.burst = int8_t(BurstState::Start);
}
// exe 0x46b850: the burst (a bullet every 4th tick with a spread from the sequence, then a pause; the sequence ends at
// a 0 -> off with a new timer)
void burst(const AnimatedActor& actor, TankFields& tank) {
    switch (BurstState(tank.burst)) {
    case BurstState::Start:
        tank.burst = int8_t(BurstState::Fire);
        tank.count = 0;
        tank.timer = uint8_t(burstTicks(0));
        [[fallthrough]];
    case BurstState::Fire:
        if (!(tank.timer & 3)) {
            int spread = burstTicks(tank.count);
            if (!onScreenOrNear(actor)) spread = ((-int(int8_t(tank.timer))) >> 2) + 8;
            else spread = ((spread >> 1) - int(int8_t(tank.timer))) >> 2;
            tank.bulletYaw = int16_t(spread);
            tank.flags |= kTankFireBullet;
        }
        tank.timer -= 1u;
        if (tank.timer != 0) return;
        tank.count += 1u;
        tank.burst = int8_t(BurstState::Pause);
        tank.timer = uint8_t(burstTicks(tank.count));
        return;
    case BurstState::Pause: {
        tank.timer -= 1u;
        if (tank.timer != 0) return;
        tank.count += 1u;
        const uint8_t ticks = uint8_t(burstTicks(tank.count));
        tank.timer = ticks;
        if (ticks != 0) {
            tank.burst = int8_t(BurstState::Fire);
            return;
        }
        tank.burst = int8_t(BurstState::Off);
        tank.timer = fireTimer();
        return;
    }
    default: return;
    }
}
// exe 0x46a650
void resetSub(const PlayerEnv& env, AnimatedActor& actor, TankFields& tank) {
    reset(actor, tank);
    enterSub(actor, TankSub::Drive);
    runSub(env, actor, tank, TankSub::Drive);
}
// exe 0x46a740
void driveSub(AnimatedActor& actor, TankFields& tank) {
    drive(actor, tank);
    if (planarDistance(actor.pos, player().pos) < 0x1770) enterSub(actor, TankSub::Pick);
}
// exe 0x46a880
void pickSub(AnimatedActor& actor, TankFields& tank) {
    drive(actor, tank);
    const int d = planarDistance(actor.pos, player().pos);
    if (cannonReady(actor, tank, d)) enterSub(actor, TankSub::Cannon);
    if (d < 0x640) enterSub(actor, TankSub::Guns);
    if (d > 0x1838) enterSub(actor, TankSub::Drive);
}
// exe 0x46a8e0: the machine gun
void guns(AnimatedActor& actor, TankFields& tank) {
    if (actor.step == 0) {
        actor.anim = 0;
        tank.turretState = uint8_t(TurretState::TrackAway);
        tank.flags = 0;
        tank.treads = uint8_t((tank.treads & kTreadsKept) | kTreadsRolling);
        tank.burst = int8_t(BurstState::Off);
        actor.enterStep(1);
    }
    const int d = planarDistance(actor.pos, player().pos);
    if (d > 0x708) {
        tank.timer = fireTimer();
        enterSub(actor, TankSub::Pick);
    }
    if (tank.status & kStatusTurretLimit) {
        tank.burst = int8_t(BurstState::Off);
        tank.turretState = uint8_t(TurretState::Center);
    }
    if (tank.turretState == 0) tank.turretState = uint8_t(TurretState::TrackAway);
    gunReady(actor, tank, d);
    burst(actor, tank);
}
// exe 0x46a9a0: the cannon
void cannon(AnimatedActor& actor, TankFields& tank) {
    switch (actor.step) {
    case 0:
        tank.turretState = uint8_t(TurretState::SwingStart);
        tank.treads = uint8_t((tank.treads & kTreadsKept) | kTreadsRolling);
        actor.acceleration.z = int16_t(0xfff8);
        tank.flags = 0;
        tank.swayDir = uint16_t(tank.swayDir ^ (rngStep() & 1));
        actor.enterStep(1);
        [[fallthrough]];
    case 1:
        if (speedToward(actor, 0) != 0) {
            actor.velocity.z = 0;
            tank.treads &= kTreadsBrakeKept;
            actor.enterStep(2);
        }
        break;
    case 2:
        if (tank.turretState == 0) {
            tank.count = 3;
            actor.enterStep(3);
        }
        break;
    case 3:
        switch (CannonStep(actor.stepCounter)) {
        case CannonStep::Fire: { // the shell this tick, a recoil step back
            tank.turretState = uint8_t(TurretState::Still);
            tank.shellNear = planarDistance(actor.pos, player().pos) <= 0x640 ? 1 : 0;
            tank.flags |= kTankFireShell;
            tank.stepTimer = 4;
            const uint16_t yaw = uint16_t(uint16_t(tank.turretYaw) + uint16_t(actor.rotation.y));
            tank.driveYaw = int16_t(yaw);
            moveAlongYaw(actor.pos, int16_t(yaw), 0, 0, 0x200); // exe 0x5e36f0
            actor.stepCounter = uint8_t(CannonStep::Recoil);
            break;
        }
        case CannonStep::Recoil:
            moveAlongYaw(actor.pos, tank.driveYaw, 0, 0, -0x80);
            tank.stepTimer -= 1u;
            if (tank.stepTimer == 0) {
                tank.stepTimer = recoilTimer(tank.count);
                actor.stepCounter = uint8_t(CannonStep::Wait);
                tank.turretState = uint8_t(TurretState::Track);
            }
            break;
        case CannonStep::Wait:
            tank.stepTimer -= 1u;
            if (tank.stepTimer != 0) break;
            tank.count -= 1u;
            if (tank.count != 0) actor.stepCounter = uint8_t(CannonStep::Fire);
            else pickAfter(actor, tank);
            break;
        default: break;
        }
        break;
    default: break;
    }
    if (tank.status & kStatusTurretLimit) pickAfter(actor, tank);
}
// exe 0x46ac30: the recoil of a hit (animation 1 until it ends)
void recoil(AnimatedActor& actor, TankFields& tank) {
    if (actor.step == 0) {
        tank.flags = kTankNoDamage | kTankNotRideable;
        tank.treads &= kTreadsKept;
        tank.swayDir = uint16_t(tank.swayDir ^ (rngStep() & 1));
        actor.anim = 1;
        actor.enterStep(1);
        integrateVelocity(actor, 0, 0, actor.velocity.z); // exe 0x5e3690
        return;
    }
    if (actor.animEnded()) pickAfter(actor, tank);
    integrateVelocity(actor, 0, 0, actor.velocity.z);
}
// exe 0x46acf0: ridden -- braking; he got off -> the next sub-state
void riddenSub(AnimatedActor& actor, TankFields& tank) {
    switch (actor.step) {
    case 0:
        tank.treads = uint8_t((tank.treads & kTreadsKept) | kTreadsRolling);
        tank.flags = kTankNotRideable;
        tank.turretState = uint8_t(TurretState::Still);
        actor.enterStep(1);
        [[fallthrough]];
    case 1:
        actor.velocity.z += 8u;
        if (actor.velocity.z >= 0) {
            actor.enterStep(2);
            actor.velocity.z = 0;
        }
        break;
    default: break;
    }
    if (!ridden(actor)) pickAfter(actor, tank);
}
// exe 0x46ad80: parked (not a lock-on target) until the attack-lock bit of its first parameter is set
void parked(AnimatedActor& actor, TankFields& tank) {
    if (actor.step == 0) {
        reset(actor, tank);
        tank.flags |= kTankNoLockOn;
        actor.enterStep(1);
    }
    if ((encounterState().attackLockWordsRecent() & (1u << (actor.params[0] & 0x1f))) != 0) enterSub(actor, TankSub::Drive);
}
void runSub(const PlayerEnv& env, AnimatedActor& actor, TankFields& tank, TankSub sub) { // [0x890178]
    switch (sub) {
    case TankSub::Reset: resetSub(env, actor, tank); return;
    case TankSub::Drive: driveSub(actor, tank); return;
    case TankSub::Pick: pickSub(actor, tank); return;
    case TankSub::Guns: guns(actor, tank); return;
    case TankSub::Cannon: cannon(actor, tank); return;
    case TankSub::Recoil: recoil(actor, tank); return;
    case TankSub::Ridden: riddenSub(actor, tank); return;
    case TankSub::Parked: parked(actor, tank); return;
    default: notPortedAt(kStoryPort, "0x46a5d0 sub-state (table overrun)", uint32_t(sub));
    }
}

// ---- the states [0x89015c] ---------------------------------------------------------------------------------------
// exe 0x46ae00: two explosions, a defeat counted, blinking 0x1e ticks (no lock-on below 0xf); then its riders off, its
// drops, a Servbot jumps out, freed
void destroyed(AnimatedActor& actor, TankFields& tank) {
    switch (actor.step) {
    case 0:
        actor.enterStep(1);
        actor.anim = 1;
        tank.stepTimer = 0x1e;
        for (int k = 1; k >= 0; --k) {
            const uint32_t slot = allocActor60(0); // exe 0x5e1300
            if (!slot) continue;
            StaticActor& explosion = staticActorAt(slot);
            explosion.flags = kActorLive;
            explosion.loopFlags |= kActorSweepGroup2;
            explosion.type = typeByte(GlobalStaticType::Explosion);
            explosion.variant = uint8_t(k);
            explosion.pos.x.whole = actor.pos.x.whole;
            explosion.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) - 0x60u);
            explosion.pos.z.whole = actor.pos.z.whole;
            ExplosionParams params(actorHeaderAt(slot));
            params.spread = 0xc0;
            params.flashes = 0x1e;
        }
        countDefeat(); // exe 0x5e53e0
        [[fallthrough]];
    case 1: {
        if (tank.stepTimer & 1) actor.tint = kExplosionTint;
        const int8_t left = int8_t(tank.stepTimer);
        if (left < 0xf) tank.flags |= kTankNoLockOn;
        tank.stepTimer = uint8_t(left - 1);
        if (uint8_t(left - 1) != 0) return;
        actorDetachRiders(actor);                           // exe 0x414e70
        spawnDrops(actor.pos, tankStats(actor).dropTable); // exe 0x5f1270
        if (const uint32_t slot = allocActor20()) {          // exe 0x5e1160: a Servbot jumps out
            AnimatedActor& servbot = animatedActorAt(slot);
            servbot.flags = kActorLive;
            servbot.loopFlags |= kActorSweepGroup2;
            servbot.type = kPirateServbotType;
            servbot.subtype = kServbotOnFootSubtype;
            servbot.variant = 0;
            servbot.pos.x = actor.pos.x;
            servbot.pos.z = actor.pos.z;
            servbot.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) - 0xc8u);
        }
        freeActor(actor); // exe 0x5e1490
        return;
    }
    default: return;
    }
}
void destroyedState(AnimatedActor& actor, TankFields& tank) { // exe 0x46ade0
    if (actor.subState == 0) destroyed(actor, tank);
    // else 0x52ef00: nothing
}
// exe 0x46a5d0 (variant 0)
void fullRun(const PlayerEnv& env, AnimatedActor& actor, TankFields& tank) {
    if (!(tank.flags & kTankNotRideable) && ridden(actor)) enterSub(actor, TankSub::Ridden);
    if (!(tank.flags & kTankNoDamage)) {
        const int outcome = takeHit(actor, tank);
        if (outcome == 1) {
            enterSub(actor, TankSub::Recoil);
        } else if (outcome == 2) {
            actor.enterState(uint8_t(TankState::Destroyed));
            destroyedState(actor, tank);
            return;
        }
    }
    runSub(env, actor, tank, TankSub(actor.subState));
    turret(actor, tank);    // exe 0x46b360
    move(env, actor, tank); // exe 0x46af30
    tilt(actor, tank);      // exe 0x4bdeb0
}
// exe 0x46a530
void run(const PlayerEnv& env, AnimatedActor& actor, TankFields& tank) {
    const uint8_t variant = actor.variant;
    if (variant == 0) fullRun(env, actor, tank);
    else if (variant <= 8) runSub(env, actor, tank, TankSub(variant - 1)); // [0x890174 + v * 4]: the sub-state table
    else notPortedAt(kStoryPort, "0x46a530 variant (table overrun)", variant);
    // smoke below a quarter of its hit points: every 8th tick, by an RNG mask test, 0x80 above it
    if (actor.hitPoints >= int16_t(actor.maxHitPoints >> 2)) return;
    if (gameState().frame & 7) return;
    if (int16_t(uint16_t(0x2bc4u << (rngStep() & 0xf))) >= 0) return;
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!slot) return;
    StaticActor& smoke = staticActorAt(slot);
    smoke.flags = kActorLive;
    smoke.type = kSparkEmitterStageType;
    smoke.variant = 0;
    smoke.loopFlags |= kActorSweepGroup2;
    smoke.pos.x = actor.pos.x;
    smoke.pos.y = int32_t(uint32_t(actor.pos.y) - 0x800000u);
    smoke.pos.z = actor.pos.z;
    spark_emitter_state::SparkEmitterFields& params = classState<spark_emitter_state::SparkEmitterFields>(staticActorAt(slot));
    params.driftX = 0;
    params.driftZ = 0;
    params.lifetime = 7;
}
// exe 0x46a460: its model (by its variant), the hit points of its row x 4, its box, its part 2 hidden, its bone scales
void bind(AnimatedActor& actor, TankFields& tank) {
    if (!bindActorModel(actor, modelVariants[actor.variant])) { // exe 0x5e0bc0
        freeActor(actor);                                       // exe 0x5e1490
        return;
    }
    actor.anim = 0;
    actor.prevAnim = 0xff;
    const uint16_t hp = uint16_t(uint32_t(tankStats(actor).hitPoints) * 4);
    actor.velocity.setXY(0, 0);
    actor.velocity.setZSpare(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    tank.turretYaw = 0;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.lastMover = nullptr;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.collisionBox.pointAt(TableId::PirateTankBox);
    actor.clearClassBlock(); // exe 0x40dc40
    actor.parts[2].flags |= kPartHidden;
    resetBoneScales(actor); // exe 0x5e3220
    actor.parts[2].flags |= kPartHidden;
    actor.enterState(uint8_t(TankState::Run));
}
void pirateTank(const PlayerEnv& env, AnimatedActor& actor) { // exe 0x46a290
    TankFields& tank = classState<TankFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.tint = kTintNeutral;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (TankState(actor.state)) { // [0x89015c]
    case TankState::Bind: bind(actor, tank); break;
    case TankState::Run: run(env, actor, tank); break;
    case TankState::Destroyed: destroyedState(actor, tank); break;
    case TankState::Free: freeActor(actor); break; // 0x468fa0
    default: notPortedAt(kStoryPort, "0x46a290 state (table overrun)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) animStep(actor); // exe 0x5e1ea0
    else animSet(actor, actor.anim, 0);                // exe 0x5e1c50
    LaneVec4 turretEntry = decodedFrame().entries[2]; // the turret bone's yaw
    turretEntry.y = int16_t(uint16_t(uint16_t(turretEntry.y) + uint16_t(tank.turretYaw)));
    if (tank.flags & kTankBoneTweak) {
        LaneVec4 hullEntry = decodedFrame().entries[1];
        hullEntry.x = int16_t(uint16_t(uint16_t(hullEntry.x) - 0x10u));
        tank.flags &= uint8_t(~kTankBoneTweak);
    }
    animPose(actor); // exe 0x5e2d90
    if (tank.flags & kTankFireShell) fireShell(actor, tank, tank.shellNear);   // exe 0x46b630
    if (tank.flags & kTankFireBullet) fireBullet(actor, tank, tank.bulletYaw); // exe 0x469e60
    const uint8_t flags = uint8_t(tank.flags & ~kTankFired);
    tank.flags = flags;
    if (!(actor.flags & kActorDrawn)) return;
    tank.hitBox.previousX = actor.prevPos.x.whole;
    tank.hitBox.previousZ = actor.prevPos.z.whole;
    tank.hitBox.result = 0;
    const AnimatedActorStats stats = tankStats(actor);
    if (flags & kTankNoHit) return;
    const int16_t pos[3] = {actor.pos.x.whole, int16_t(uint16_t(actor.pos.y.whole) - 0x80u), actor.pos.z.whole};
    const uint32_t shape = hitOwner(actor.slot) | hitHeightLevel(stats.hitHeight) | (uint32_t(flags & kTankNoLockOn) << 0xe) |
                           hitGroup(kHitGroupBodies) | hitRadius(0xc0);
    hitListSubmit(pos, tank.hitBox, shape, actor.hitPoints >= 0 ? kHitAttackSolid : kHitAttackBump); // exe 0x5e4120
    actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, uint32_t(stats.attack) | kHitAttackSolid); // 0x414050
}

} // namespace

void pirateTankUpdate(const PlayerEnv& env, uint32_t actor) { pirateTank(env, animatedActorAt(actor)); }

} // namespace od::port
