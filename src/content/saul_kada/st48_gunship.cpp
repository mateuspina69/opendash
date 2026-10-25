// The ST48 gunship (static pool 0x60, exe 0x593190; hypothesis names): the ST48 static table 0x909174 gives it as type
// 3 (sub 1) and type 6 (sub 0); the ST48 field encounters spawn it (their class records 0x9095c0: pool 0x60 type 6 sub 0
// variants 0x80 / 0x81, and a pool-0x20 type 0x13). A sibling of the Nino gunships (game/gunship_flight) with its own
// state machine.
//   Its row: staticStats(type, variant & 0xf) (+0 hit points, +1 the hit entry's damage, +2 the shots' damage, +4 the
//   hit entry's height, +5 the drops).
//   States [0x8cdf68]: 0 bind 0x593310 (its first sub-state: the byte [0x8cdf74 + start index + (variant & 0xf) * 4];
//   variant & 0x80: 0xe00 higher), 1 alive 0x5933c0, 2 shot down 0x594970 (spins and falls; at a wall: a blast, a
//   Servbot bailing out, the drops, freed).
//   Sub-states [0x8cdf80]: 0 start 0x593480, 1 cruise 0x5934c0 (a sound while facing the player within 3000, random
//   bursts), 2 rocked 0x593680 (6 swings), 3 0x593700 (-> the strafe), 4 strafe 0x593750 (keeps 0x4b0..0x640 off the
//   player, bursts; 0x3d ticks off screen -> the swing round), 5 swing round 0x593c00 (to a point 0x5780 ahead of /
//   behind the player), 6 climb over its cell 0x593d70, 7 climb above the player 0x593ec0, 8 / 9 wait 0x593fb0 /
//   0x594100 (its enemy id defeated / within 10000), then a turn / a loop, 0xa climb 800 0x5942f0, 0xb run in 0x5943f0
//   (0xc00, to within 7000), 0xc fire 0x594470 (0x1e ticks), 0xd climb away 0x594500 (freed past 0x3800), 0xe veer off a
//   wall 0x594570 (0x594590 / 0x594660 by its variant), 0xf straight run 0x5947b0, 0x10 run on until no wall 0x5948c0.
// The shadow (exe 0x5f20f0) is render output. Calls whose result the exe discards are not repeated: exe 0x413240 in
// 0x595390, exe 0x5e3bc0 in 0x593c00.
// Its stack points (0x593d70 / 0x594660) have only their high words of x / z written (the low halves are stale stack):
// taken as 0 here (as the Nino gunships).
// Exe quirks kept: 0x595240 marks the move's water bit in its contact and then overwrites the contact (so the free on
// that bit is never reached through it); 0x594f50 turns its vertical step round only when its low 16 bits equal the
// height error's.
#include "content/saul_kada/st48_gunship.hpp"
#include "content/saul_kada/st48_gunship_state.hpp"

#include "content/saul_kada/pirate_servbots.hpp"

#include "engine/data_source.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/collision.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/enemy_motion.hpp"
#include "game/enemy_shot.hpp"
#include "game/explosions.hpp"
#include "game/global_class_types.hpp"
#include "game/gunship_flight.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/matrix.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"
#include "game/territory.hpp"
#include "game/visibility.hpp"

namespace od::port {

using namespace st48_gunship_state;

namespace {

enum class ShipState : uint8_t { Bind = 0, Alive = 1, Down = 2 };
enum class ShipSub : uint8_t {
    Start = 0, Cruise = 1, Rocked = 2, StrafeStart = 3, Strafe = 4, SwingRound = 5, ClimbCell = 6, ClimbOver = 7,
    WaitTurn = 8, WaitLoop = 9, Climb = 0xa, RunIn = 0xb, RunFire = 0xc, ClimbAway = 0xd, Veer = 0xe, StraightRun = 0xf,
    RunOn = 0x10
};
enum class DownStep : uint8_t { Start = 0, Fall = 1, Crash = 2 };
// guns (+0xc4)
constexpr uint8_t kGunLeft = 0x01, kGunRight = 0x02, kShipNoRock = 0x04, kShipRocking = 0x08, kShipSway = 0x10,
                  kShipProbeSides = 0x20, kShipProbeMarksCell = 0x40, kShipNoLockOn = 0x80;
// contact (+0xc5)
constexpr uint8_t kContactTerritory = 0x01, kContactSideWall = 0x02, kContactClimbing = 0x04, kContactWall = 0x08,
                  kContactWater = 0x10;
// flags (+0xc8)
constexpr uint8_t kShipOut = 0x01, kShipIn = 0x02, kShipRanging = kShipOut | kShipIn, kShipHeightMove = 0x04,
                  kShipAhead = 0x08, kShipRocked = 0x10, kShipNoPushOut = 0x20;
const DataTable<ScalarTable<uint8_t, 16>> cruiseBursts{TableId::St48GunshipCruiseBursts},
    strafeBursts{TableId::St48GunshipStrafeBursts}, strafeCooldowns{TableId::St48GunshipStrafeCooldowns},
    straightRuns{TableId::St48GunshipStraightRuns};
const DataTable<ScalarTable<uint16_t, 8>> cruiseTurnLimits{TableId::St48GunshipCruiseTurnLimits};
constexpr uint16_t kEngineSound = 0xb0, kHurtSound = 0x8d, kStrongHurtSound = 0x8e;
constexpr uint32_t kHitByPlayerOrStrong = kHitAttackByPlayer | kHitAttackStrong;
constexpr uint8_t kShotVariant = 0, kStrongShotVariant = 4;
constexpr int16_t kShotTicks = 0x2d;
constexpr uint8_t kBailOutVariant = kServbotSingle; // a Servbot falling in (variant 0), the only one

StaticActorStats shipStats(const StaticActor& actor) { return staticStats(actor.type, actor.variant & 0xf); }
uint8_t shipKind(const StaticActor& actor) { return actor.variant & 0xf; }
int playerDistance(const StaticActor& actor) { return planarDistance(player().pos, actor.pos); } // exe 0x5e3b00
int bearingFromPlayer(const StaticActor& actor) { return angleOf(player().pos, actor.pos); }      // exe 0x5e3bc0
int yawTo(const StaticActor& actor, int angle) {
    return angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angle));
}
void enterSub(StaticActor& actor, ShipSub sub) {
    actor.subState = uint8_t(sub);
    actor.restartStep();
}
void addToYaw(StaticActor& actor, int turn) { actor.rotation.y = int16_t(actor.rotation.y + turn); }

// ---- the flight helpers (exe 0x593720 .. 0x595010) -----------------------------------------------------------------
// exe 0x593720: the motion stopped, the vertical step 8, level
void stopMotion(StaticActor& actor, ShipFields& ship) {
    actor.acceleration.setXY(0, 0);
    actor.velocity.setXY(0, 0);
    actor.acceleration.y = 8;
    actor.spin.x = 0;
    actor.spin.y = 0;
    ship.pitchTarget = 0x30;
}
// exe 0x594c90: the vertical speed += its step between -0x80 and 0x80 (bouncing by 8)
void bob(StaticActor& actor) {
    actor.velocity.y = int16_t(actor.velocity.y + actor.acceleration.y);
    if (actor.velocity.y > 0x80) {
        actor.velocity.y = 0x80;
        actor.acceleration.y = -8;
    }
    if (actor.velocity.y < -0x80) {
        actor.velocity.y = -0x80;
        actor.acceleration.y = 8;
    }
}
// exe 0x594cd0: the yaw += turn rate >> 4, the turn rate += its step within +-lim
void turnRate(StaticActor& actor, const ShipFields& ship, int lim) {
    addToYaw(actor, actor.spin.y >> 4);
    const int16_t r = int16_t(ship.turnStep + actor.spin.y);
    actor.spin.y = r;
    if (lim < r) actor.spin.y = int16_t(lim);
    if (actor.spin.y < -lim) actor.spin.y = int16_t(-lim);
}
// exe 0x594d20(actor, point, lim, acc): the turn step toward the bearing from the point, then turnRate
void steerTo(StaticActor& actor, ShipFields& ship, int bearing, int lim, int16_t acc) {
    const int d = yawTo(actor, bearing);
    ship.turnStep = 0;
    if (d < 0) ship.turnStep = int16_t(-acc);
    if (d > 0) ship.turnStep = acc;
    turnRate(actor, ship, lim);
}
void steerToPlayer(StaticActor& actor, ShipFields& ship, int lim, int16_t acc) { // exe 0x594da0
    steerTo(actor, ship, bearingFromPlayer(actor), lim, acc);
}
// exe 0x594dc0(actor, lim, acc, dz): steerToPlayer outside the dead zone +-dz; 0 inside, else the angle difference
int steerToPlayerOutside(StaticActor& actor, ShipFields& ship, int lim, int16_t acc, int dz) {
    const int d = yawTo(actor, bearingFromPlayer(actor));
    if (uint32_t(d + dz) <= uint32_t(dz * 2)) return 0;
    ship.turnStep = 0;
    if (d < 0) ship.turnStep = int16_t(-acc);
    if (d > 0) ship.turnStep = acc;
    turnRate(actor, ship, lim);
    return d;
}
// exe 0x5e5510 on its forward speed by its forward acceleration
int forwardToward(StaticActor& actor, int lim) {
    return stepTowardLimit(actor.velocity.z, uint16_t(actor.acceleration.z), lim);
}
// exe 0x594e50(actor, lim): the forward speed toward |lim| until within its stopping distance of the 0x578 ring around
// the player, then braked to 0 (1 once stopped)
int ringApproach(StaticActor& actor, int lim) {
    int s = stoppingDistance(actor.velocity.z, -int(actor.acceleration.z)) >> 16;
    int d;
    if (actor.acceleration.z >= 1) {
        d = -playerDistance(actor);
        s -= 0x578;
    } else {
        d = playerDistance(actor);
        s += 0x578;
    }
    if (s < d) {
        if (forwardToward(actor, lim) == 0) return 0;
        actor.velocity.z = int16_t(actor.acceleration.z <= 0 ? -lim : lim);
        return 0;
    }
    if (forwardToward(actor, 0) == 0) return 0;
    actor.velocity.z = 0;
    return 1;
}
// exe 0x594f50(actor, lim, dy): the vertical speed toward |lim| until within its stopping distance of the height error
// dy, then to 0 (1 once stopped)
int verticalSettle(StaticActor& actor, int lim, int dy) {
    if (((uint16_t(actor.acceleration.y) ^ uint32_t(dy)) & 0xffff) == 0)
        actor.acceleration.y = int16_t(-actor.acceleration.y);
    const int s = stoppingDistance(actor.velocity.y, -int(actor.acceleration.y)) >> 16;
    if (actor.acceleration.y >= 1) dy = -dy;
    if (s < dy) {
        if (verticalApproach(actor, lim) != 0) actor.velocity.y = int16_t(actor.acceleration.y <= 0 ? -lim : lim);
        return 0;
    }
    if (verticalApproach(actor, 0) == 0) return 0;
    actor.velocity.y = 0;
    return 1;
}
// the low hold's height (exe 0x594f00 / 0x593ab0): 600 over the player or 400 over its ground, the higher one
int lowHoldHeight(const ShipFields& ship) {
    const int g = ship.ground - 400, p = player().pos.y.whole - 600;
    return p < g ? p : g;
}
int settleLow(StaticActor& actor, const ShipFields& ship, int lim) { // exe 0x594f00
    return verticalSettle(actor, lim, actor.pos.y.whole - lowHoldHeight(ship));
}
int settleHigh(StaticActor& actor, const ShipFields& ship, int lim) { // exe 0x594fe0
    return verticalSettle(actor, lim, actor.pos.y.whole - ship.ground + 800);
}
// the end of a height move (exe 0x593ab0 / 0x593b70)
void endHeightMove(StaticActor& actor, ShipFields& ship) {
    ship.pitchTarget = 0x30;
    ship.flags &= uint8_t(~kShipHeightMove);
    actor.acceleration.y = int16_t(actor.acceleration.y <= 0 ? 8 : -8);
}
// a height move started toward the error's side
void startHeightMove(StaticActor& actor, ShipFields& ship, int16_t step) {
    actor.acceleration.y = step;
    ship.flags |= kShipHeightMove;
}
// exe 0x593ab0: bobbing at the low hold; more than 200 off it -> a height move
void holdLow(StaticActor& actor, ShipFields& ship) {
    if (ship.flags & kShipHeightMove) {
        if (settleLow(actor, ship, 0x200) == 0) return;
        endHeightMove(actor, ship);
        return;
    }
    if (!(ship.flags & kShipRanging)) bob(actor);
    const int d = actor.pos.y.whole - lowHoldHeight(ship);
    if (d >= 0xc9) startHeightMove(actor, ship, -0x10);
    if (d >= -200) return;
    startHeightMove(actor, ship, 0x10);
}
// exe 0x593b70: 800 over its ground (+-100)
void holdHigh(StaticActor& actor, ShipFields& ship) {
    if (ship.flags & kShipHeightMove) {
        if (settleHigh(actor, ship, 0x200) == 0) return;
        endHeightMove(actor, ship);
        return;
    }
    const int d = actor.pos.y.whole - ship.ground + 800;
    if (d >= 0x65) startHeightMove(actor, ship, -0x10);
    if (d >= -100) return;
    startHeightMove(actor, ship, 0x10);
}
// exe 0x595010: its loop again: variant kind 0 -> the cruise, else the strafe
void nextLoop(StaticActor& actor) { enterSub(actor, shipKind(actor) == 0 ? ShipSub::Cruise : ShipSub::Strafe); }
// a burst's guns: every other tick of its count, the sides alternating (not while rocked)
void burstGuns(ShipFields& ship, uint8_t count) {
    if (!(ship.flags & kShipRocked) && !(count & 1)) ship.guns |= uint32_t(((int8_t(count) >> 1) & 1) + 1);
}
// the grid cell's centre {cell x * 0x200 + 0x100, its y, cell z * 0x200 + 0x100} (the low halves of x / z taken as 0)
void cellCentre(const StaticActor& actor, const ShipFields& ship, int32_t out[3]) {
    out[0] = int32_t(uint32_t(uint16_t(int16_t(ship.cellX * 0x200 + 0x100))) << 16);
    out[1] = actor.pos.y;
    out[2] = int32_t(uint32_t(uint16_t(int16_t(ship.cellZ * 0x200 + 0x100))) << 16);
}
int bearingFrom(const StaticActor& actor, const int32_t point[3]) {
    return angleOf(point[0], point[2], actor.pos.x, actor.pos.z);
}
// ---- the sub-states [0x8cdf80] -------------------------------------------------------------------------------------
void cruise(StaticActor& actor, ShipFields& ship);
// exe 0x593480
void start(StaticActor& actor, ShipFields& ship) {
    ship.pitchTarget = 0x30;
    actor.velocity.setXY(0, 0);
    actor.velocity.z = int16_t(0xfc00);
    actor.acceleration.setXY(0, 0);
    actor.spin.x = 0;
    actor.spin.y = 0;
    enterSub(actor, ShipSub::Cruise);
    cruise(actor, ship);
}
// exe 0x5934c0: cruising high, steering to the player; facing him within 3000 a sound and now and then a burst
void cruise(StaticActor& actor, ShipFields& ship) {
    switch (actor.step) {
    case 0: {
        ship.guns = kShipProbeMarksCell | kShipProbeSides | kShipSway;
        ship.flags &= uint8_t(~kShipNoPushOut);
        actor.acceleration.z = -0x10;
        const uint32_t r = rngStep();
        const uint16_t limit = cruiseTurnLimits[r & 7];
        actor.enterStep(1);
        ship.heading = int16_t(limit);
        [[fallthrough]];
    }
    case 1:
        if (facingAndRange(actor, player(), 0x180, 3000) == 0) {
            soundPlayAtPos(kEngineSound, actor.pos); // exe 0x5d5380
            if (int32_t(0x28000u << (rngStep() & 0x1f)) < 0) actor.enterStep(2);
        }
        break;
    case 2:
        ship.count = cruiseBursts[rngStep() & 0xf];
        actor.enterStep(3);
        [[fallthrough]];
    case 3: {
        burstGuns(ship, ship.count);
        const uint8_t c = uint8_t(ship.count - 1);
        ship.count = c;
        if (c == 0) enterSub(actor, ShipSub::StraightRun);
        break;
    }
    default: break;
    }
    if (steerToPlayerOutside(actor, ship, ship.heading + 0x180, 0x10, 0x80) == 0) steerDamp(actor, 0x10);
    if (forwardToward(actor, 0x400) != 0) actor.velocity.z = int16_t(0xfc00);
    holdHigh(actor, ship);
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    if (ship.contact & kContactSideWall) enterSub(actor, ShipSub::Veer);
}
// exe 0x593680: rocked six swings
void rocked(StaticActor& actor, ShipFields& ship) {
    if (actor.step == 0) {
        ship.guns = kShipSway | kShipRocking | kShipNoRock;
        ship.flags &= uint8_t(~kShipNoPushOut);
        rockInit(actor);
        ship.count = 6;
        actor.enterStep(1);
    }
    if (rock(actor) != 0) {
        const uint8_t c = uint8_t(ship.count - 1);
        ship.count = c;
        if (c == 0) enterSub(actor, ShipSub::Cruise);
    }
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
}
void strafe(StaticActor& actor, ShipFields& ship);
// exe 0x593700
void strafeStart(StaticActor& actor, ShipFields& ship) {
    stopMotion(actor, ship);
    enterSub(actor, ShipSub::Strafe);
    strafe(actor, ship);
}
// exe 0x593750: the strafe: low, 0x4b0..0x640 off the player, bursts while facing him
void strafe(StaticActor& actor, ShipFields& ship) {
    switch (actor.step) {
    case 0:
        ship.guns = kShipProbeMarksCell | kShipProbeSides;
        ship.count = 0x78;
        ship.flags &= uint8_t(~(kShipRanging | kShipNoPushOut));
        ship.cooldown = 1;
        ship.pitchTarget = 0x30;
        actor.enterStep(1);
        [[fallthrough]];
    case 1:
        if (ship.cooldown != 0) {
            ship.cooldown -= 1u;
        } else if (facingAndRange(actor, player(), 0x200, 3000) == 0) {
            if (int32_t(0x28000u << (rngStep() & 0x1f)) < 0) actor.enterStep(2);
        }
        break;
    case 2:
        ship.cooldown = strafeBursts[rngStep() & 0xf];
        actor.enterStep(3);
        [[fallthrough]];
    case 3: {
        burstGuns(ship, ship.cooldown);
        const uint8_t c = uint8_t(ship.cooldown - 1);
        ship.cooldown = c;
        if (c == 0) {
            ship.cooldown = uint8_t(uint32_t(strafeCooldowns[rngStep() & 0xf]) << 1);
            actor.enterStep(1);
        }
        break;
    }
    default: break;
    }
    const int diff = yawTo(actor, bearingFromPlayer(actor));
    const int dist = playerDistance(actor);
    int lim, acc;
    if (ship.flags & kShipRanging) {
        lim = 0x180;
        acc = 0x10;
        ship.guns |= kShipSway;
        if (ringApproach(actor, 0x400) != 0) {
            ship.flags &= uint8_t(~kShipRanging);
            ship.guns &= uint8_t(~kShipSway);
        }
    } else {
        lim = 0x120;
        acc = 0x10;
        if (uint32_t(diff + 0x100) <= 0x1ff) {
            if (uint16_t(actor.spin.y + 0x60) <= 0xc0) {
                lim = 0x60;
                acc = 4;
            }
            if (actor.velocity.z != 0) {
                if (forwardToward(actor, 0) != 0) actor.velocity.z = 0;
            } else {
                if (dist >= 0x641) {
                    actor.acceleration.z = -0x10;
                    ship.flags |= kShipOut;
                }
                if (dist <= 0x4af) {
                    actor.acceleration.z = 0x10;
                    ship.flags |= kShipIn;
                }
            }
        }
    }
    holdLow(actor, ship);
    const uint16_t oldStep = uint16_t(ship.turnStep);
    steerToPlayer(actor, ship, lim, int16_t(acc));
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    if (((oldStep ^ uint16_t(ship.turnStep)) & 0x8000) || dist >= 0x640) {
        ship.count = 0x78;
    } else {
        const uint8_t c = uint8_t(ship.count - 1);
        ship.count = c;
        if (c == 0) enterSub(actor, actor.velocity.z >= 1 ? ShipSub::ClimbOver : ShipSub::StraightRun);
    }
    if (ship.offScreen >= 0x3d && actor.velocity.z == 0) {
        enterSub(actor, ShipSub::SwingRound);
        ship.flags &= uint8_t(~kShipAhead);
    }
    if ((ship.contact & kContactSideWall) && (ship.flags & kShipRanging))
        enterSub(actor, (ship.flags & kShipOut) ? ShipSub::Veer : ShipSub::ClimbOver);
}
// exe 0x593c00: to the point 0x5780 ahead of (kShipAhead) / behind the player's facing
void swingRound(StaticActor& actor, ShipFields& ship) {
    if (actor.step == 0) {
        ship.guns = kShipProbeMarksCell | kShipProbeSides | kShipSway;
        ship.flags = uint8_t((ship.flags & ~kShipNoPushOut) | kShipOut);
        actor.acceleration.z = -0x18;
        actor.enterStep(1);
    }
    const int z = (ship.flags & kShipAhead) ? 0x5780 : -0x5780;
    const Player& p = player();
    int32_t o[3];
    rotateByYaw(p.rotation.y, 0, 0, z, o); // exe 0x5e38d0
    const int32_t point[3] = {int32_t(uint32_t(p.pos.x) + uint32_t(o[0])), int32_t(uint32_t(p.pos.y) + uint32_t(o[1])),
                              int32_t(uint32_t(p.pos.z) + uint32_t(o[2]))};
    const int s = stoppingDistance(actor.velocity.z, -int(actor.acceleration.z));
    if (planarDistance(actor.pos, point) + 0x40 <= (s >> 16) || ship.offScreen == 0) {
        if (forwardToward(actor, 0) != 0) actor.velocity.z = 0;
    } else {
        if (forwardToward(actor, 0x480) != 0) actor.velocity.z = int16_t(0xfb80);
    }
    holdLow(actor, ship);
    steerTo(actor, ship, bearingFrom(actor, point), 0x200, 0x18);
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    if (actor.velocity.z == 0) {
        enterSub(actor, ShipSub::Strafe);
        ship.flags &= uint8_t(~kShipRanging);
    }
    if (ship.contact & kContactSideWall) enterSub(actor, ShipSub::Veer);
}
// exe 0x593d70: climbing to 900 over the ground of its cell, then its loop
void climbCell(const PlayerEnv& env, StaticActor& actor, ShipFields& ship) {
    if (actor.step == 0) {
        ship.guns = 0;
        ship.flags &= uint8_t(~kShipNoPushOut);
        actor.acceleration.y = -0x10;
        actor.acceleration.z = -0x10;
        ship.pitchTarget = int8_t(0xd0);
        actor.enterStep(1);
    }
    int32_t centre[3];
    cellCentre(actor, ship, centre);
    const int16_t p[3] = {int16_t(centre[0] >> 16), int16_t(centre[1] >> 16), int16_t(centre[2] >> 16)};
    const int g = probeGroundAt(env, p, CollisionBoxRef::record(stateAddress(TableId::St48GunshipProbeBox)), 0, 0xffff); // exe 0x40f1c0
    if (actor.pos.y.whole - (g - 900) <= (stoppingDistance(actor.velocity.y, -int(actor.acceleration.y)) >> 16)) {
        if (verticalApproach(actor, 0) != 0) actor.velocity.y = 0;
        if (forwardToward(actor, 0x400) != 0) actor.velocity.z = int16_t(0xfc00);
    } else {
        if (verticalApproach(actor, 0x300) != 0) actor.velocity.y = int16_t(0xfd00);
        if (forwardToward(actor, 0) != 0) actor.velocity.z = 0;
    }
    steerToPlayer(actor, ship, 0x180, 0x10);
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    if (actor.velocity.y == 0) nextLoop(actor);
}
// exe 0x593ec0: stopping, climbing to 1000 over the player -> the swing round ahead of him
void climbOver(StaticActor& actor, ShipFields& ship) {
    if (actor.step == 0) {
        ship.guns = 0;
        ship.flags &= uint8_t(~kShipNoPushOut);
        actor.acceleration.y = -0x10;
        actor.acceleration.z = -0x18;
        actor.enterStep(1);
    }
    const int above = (actor.pos.y.whole - player().pos.y.whole) + 1000;
    const int s = stoppingDistance(actor.velocity.y, -int(actor.acceleration.y));
    if (forwardToward(actor, 0) != 0) actor.velocity.z = 0;
    if (above <= (s >> 16)) {
        if (verticalApproach(actor, 0) != 0) actor.velocity.y = 0;
    } else if (verticalApproach(actor, 0x300) != 0) {
        actor.velocity.y = int16_t(0xfd00);
    }
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    if (actor.velocity.y != 0) return;
    actor.velocity.z = 0;
    enterSub(actor, ShipSub::SwingRound);
    ship.flags |= kShipAhead;
}
// the waits' wait (exe 0x593fb0 / 0x594100 step 1): its enemy alive (or the player 10000 away) while a wall is beside it
bool keepWaiting(const StaticActor& actor, const ShipFields& ship) {
    const int d = distance3(player().pos, actor.pos);
    const int8_t id = ship.enemyId;
    if (id != 0) return !enemyDefeated(uint32_t(int32_t(id))) && (ship.contact & kContactSideWall); // exe 0x5e6420
    return d >= 10000 && (ship.contact & kContactSideWall);
}
// exe 0x593fb0: waiting, then facing the player -> climbing over its cell, else settling low -> its loop
void waitTurn(StaticActor& actor, ShipFields& ship) {
    switch (actor.step) {
    case 0:
        ship.guns = kShipNoLockOn | kShipProbeMarksCell | kShipProbeSides;
        ship.flags &= uint8_t(~kShipNoPushOut);
        actor.enterStep(1);
        return;
    case 1:
        if (keepWaiting(actor, ship)) return;
        actor.enterStep(2);
        [[fallthrough]];
    case 2: {
        stopMotion(actor, ship);
        ship.guns &= uint8_t(~kShipNoLockOn);
        const int d = yawTo(actor, bearingFromPlayer(actor));
        if (uint32_t(d + 0x200) >= 0x401) {
            actor.enterStep(3);
            return;
        }
        enterSub(actor, ShipSub::ClimbCell);
        return;
    }
    case 3:
        actor.acceleration.y = -0x10;
        ship.pitchTarget = 0;
        ship.flags |= kShipHeightMove;
        actor.enterStep(4);
        [[fallthrough]];
    case 4:
        if (settleLow(actor, ship, 0x200) != 0) nextLoop(actor);
        integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
        return;
    default: return;
    }
}
// exe 0x594100: waiting, settling low, then (facing the player) a sideways loop out and back
void waitLoop(StaticActor& actor, ShipFields& ship) {
    switch (actor.step) {
    case 0:
        ship.guns = kShipNoLockOn | kShipProbeMarksCell | kShipProbeSides | kShipNoRock;
        ship.flags &= uint8_t(~kShipNoPushOut);
        actor.enterStep(1);
        return;
    case 1:
        if (keepWaiting(actor, ship)) return;
        actor.enterStep(2);
        [[fallthrough]];
    case 2:
        stopMotion(actor, ship);
        ship.guns &= uint8_t(~kShipNoLockOn);
        actor.acceleration.y = -0x10;
        ship.pitchTarget = 0;
        ship.flags |= kShipHeightMove;
        actor.enterStep(3);
        [[fallthrough]];
    case 3:
        if (settleLow(actor, ship, 0x200) != 0) {
            const int d = yawTo(actor, bearingFromPlayer(actor));
            if (uint32_t(d + 0x200) >= 0x401) {
                nextLoop(actor);
            } else if (d >= 0) {
                actor.acceleration.x = -0x10;
                actor.enterStep(4);
            } else {
                actor.acceleration.x = 0x10;
                actor.enterStep(4);
            }
        }
        integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
        return;
    case 4:
        actor.velocity.x = 0;
        actor.velocity.z = int16_t(0xfc00);
        ship.heading = actor.rotation.y;
        actor.enterStep(5);
        [[fallthrough]];
    case 5:
        actor.velocity.x = int16_t(actor.velocity.x + actor.acceleration.x);
        if (uint16_t(actor.velocity.x + 0x200) >= 0x400) actor.enterStep(6);
        break;
    case 6:
        actor.velocity.x = int16_t(actor.velocity.x - uint16_t(actor.acceleration.x));
        if ((uint16_t(actor.velocity.x) ^ uint16_t(actor.acceleration.x)) & 0x8000) {
            actor.acceleration.x = 0;
            actor.velocity.x = 0;
            actor.velocity.z = 0;
            nextLoop(actor);
        }
        break;
    default: return;
    }
    steerToPlayer(actor, ship, 0x180, 0x10);
    moveAlongYaw(actor.pos, ship.heading, actor.velocity.x, actor.velocity.y, 0); // exe 0x5e36f0
}
// exe 0x5942f0: climbing 800 (at a random roll), then running on
void climb(StaticActor& actor, ShipFields& ship) {
    if (actor.step == 0) {
        ship.guns = kShipProbeMarksCell | kShipProbeSides;
        stopMotion(actor, ship);
        ship.flags |= kShipNoPushOut;
        actor.acceleration.y = -0x10;
        actor.acceleration.z = -0x10;
        actor.velocity.z = 0;
        actor.rotation.z = int16_t((rngStep() & 0x3f) - 0x20);
        ship.pitchTarget = int8_t(0xd0);
        actor.enterStep(1);
        ship.heading = int16_t(actor.pos.y.whole - 800);
    }
    if (actor.pos.y.whole - ship.heading <= (stoppingDistance(actor.velocity.y, -int(actor.acceleration.y)) >> 16)) {
        if (verticalApproach(actor, 0) != 0) actor.velocity.y = 0;
    } else if (verticalApproach(actor, 0x300) != 0) {
        actor.velocity.y = int16_t(0xfd00);
    }
    integrateVelocity(actor, 0, actor.velocity.y, 0);
    if (actor.velocity.y != 0) return;
    enterSub(actor, ShipSub::RunOn);
}
// exe 0x5943f0: flying in at 0xc00 until within 7000 (a sound) -> firing
void runIn(StaticActor& actor, ShipFields& ship) {
    if (actor.step == 0) {
        stopMotion(actor, ship);
        ship.guns = kShipProbeSides;
        ship.flags |= kShipNoPushOut;
        actor.velocity.z = int16_t(0xf400);
        actor.enterStep(1);
    }
    integrateVelocity(actor, 0, 0, actor.velocity.z);
    if (playerDistance(actor) >= 7000) return;
    enterSub(actor, ShipSub::RunFire);
    soundPlayAtPos(kEngineSound, actor.pos); // exe 0x5d5380
}
// exe 0x594470: 0x1e ticks of shots (odd ticks), on until out of range -> climbing away
void runFire(StaticActor& actor, ShipFields& ship) {
    if (actor.step == 0) {
        ship.count = 0x1e;
        actor.enterStep(1);
    }
    if (!(ship.flags & kShipRocked) && (ship.count & 1)) ship.guns |= uint32_t(((int8_t(ship.count) >> 1) & 1) + 1);
    integrateVelocity(actor, 0, 0, actor.velocity.z);
    const uint8_t c = uint8_t(ship.count - 1);
    ship.count = c;
    if (c != 0 && playerDistance(actor) <= 7000) return;
    enterSub(actor, ShipSub::ClimbAway);
}
// exe 0x594500: climbing away; freed past 0x3800
void climbAway(StaticActor& actor, ShipFields& ship) {
    if (actor.step == 0) {
        ship.pitchTarget = int8_t(0xd0);
        actor.enterStep(1);
    }
    actor.velocity.y = int16_t(actor.velocity.y - 0x40);
    if (actor.velocity.y <= -0x801) actor.velocity.y = int16_t(0xf800);
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    if (playerDistance(actor) <= 0x3800) return;
    freeActor(actor);
}
// the turn done this tick, taken off the veer's heading (its 0x400 worth of turning)
void countTurn(ShipFields& ship, const StaticActor& actor) {
    const uint16_t r = uint16_t(actor.spin.y), sign = uint16_t(int16_t(r) >> 15);
    ship.heading = int16_t(ship.heading - int16_t(uint16_t((r ^ sign) - sign)));
}
// exe 0x594590 (variant kind 0): braking, turning on 0x400 worth of turn rate -> the cruise
void veerTurn(StaticActor& actor, ShipFields& ship) {
    if (actor.step == 0) {
        ship.guns = kShipProbeSides;
        ship.flags &= uint8_t(~kShipNoPushOut);
        actor.acceleration.z = -0x10;
        ship.turnStep = int16_t(actor.spin.y >= 1 ? 0x40 : 0xffc0);
        ship.heading = 0x400;
        actor.enterStep(1);
    }
    if (ship.contact & kContactSideWall) ship.heading = 0x400;
    else countTurn(ship, actor);
    if (forwardToward(actor, 0) != 0) {
        ship.flags &= uint8_t(~kShipRanging);
        actor.velocity.z = 0;
    }
    turnRate(actor, ship, 0x200);
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    if (ship.heading >= 1) return;
    enterSub(actor, ShipSub::Cruise);
}
// exe 0x594660 (variant kinds 1..): turning away from its cell -> the strafe
void veerCell(StaticActor& actor, ShipFields& ship) {
    if (actor.step == 0) {
        ship.guns = kShipProbeSides;
        ship.flags &= uint8_t(~kShipNoPushOut);
        int32_t centre[3];
        cellCentre(actor, ship, centre);
        actor.acceleration.z = -0x10;
        const int d = yawTo(actor, bearingFrom(actor, centre));
        if (d <= -1) ship.turnStep = 0x10;
        if (d >= 1) ship.turnStep = -0x10;
        ship.heading = 0x400;
        actor.enterStep(1);
    }
    if (ship.contact & kContactSideWall) {
        ship.heading = 0x400;
        if (forwardToward(actor, 0) != 0) {
            ship.flags &= uint8_t(~kShipRanging);
            actor.velocity.z = 0;
        }
    } else {
        ship.flags |= kShipOut;
        countTurn(ship, actor);
        if (forwardToward(actor, 0x180) != 0) actor.velocity.z = int16_t(0xfe80);
    }
    holdLow(actor, ship);
    turnRate(actor, ship, 0x180);
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    if (ship.heading >= 0) return;
    enterSub(actor, ShipSub::Strafe);
}
void veer(StaticActor& actor, ShipFields& ship) { // exe 0x594570
    if (shipKind(actor) == 0) veerTurn(actor, ship);
    else veerCell(actor, ship);
}
// exe 0x5947b0: a random time straight on -> its loop
void straightRun(StaticActor& actor, ShipFields& ship) {
    ship.flags |= kShipOut;
    if (actor.step == 0) {
        ship.guns = kShipProbeMarksCell | kShipProbeSides | kShipSway;
        ship.flags &= uint8_t(~kShipNoPushOut);
        actor.acceleration.z = -0x10;
        ship.count = straightRuns[rngStep() & 0xf];
        actor.enterStep(1);
    }
    steerDamp(actor, 0x10);
    if (forwardToward(actor, 0x400) != 0) actor.velocity.z = int16_t(0xfc00);
    const bool strafer = shipKind(actor) != 0;
    if (strafer) holdLow(actor, ship);
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    const uint8_t c = uint8_t(ship.count - 1);
    ship.count = c;
    if (c == 0) enterSub(actor, strafer ? ShipSub::Strafe : ShipSub::Cruise);
    if (ship.contact & kContactSideWall) enterSub(actor, ShipSub::Veer);
}
// exe 0x5948c0: on at full speed until no wall -> its loop
void runOn(StaticActor& actor, ShipFields& ship) {
    const uint8_t flags = ship.flags;
    ship.flags = uint8_t(flags | kShipOut);
    if (actor.step == 0) {
        ship.guns = kShipProbeMarksCell | kShipProbeSides | kShipSway;
        ship.flags = uint8_t(flags | kShipNoPushOut | kShipOut);
        actor.acceleration.z = -0x10;
        ship.pitchTarget = 0x30;
        ship.count = straightRuns[rngStep() & 0xf];
        actor.enterStep(1);
    }
    if (forwardToward(actor, 0x400) != 0) actor.velocity.z = int16_t(0xfc00);
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    if (ship.contact & kContactWall) return;
    nextLoop(actor);
}

// ---- its attitude, collision, hits and guns ------------------------------------------------------------------------
// exe 0x594bd0: the pitch toward the pitch target * 4, the roll toward the turn (8 a tick; not while rocking)
void attitude(StaticActor& actor, ShipFields& ship) {
    const int t = int(ship.pitchTarget) << 2;
    int d = angleDiff(uint32_t(int32_t(actor.rotation.x)), uint32_t(t));
    if (uint32_t(d + 8) < 0x10) actor.rotation.x = int16_t(t);
    else actor.rotation.x = int16_t(actor.rotation.x + (d <= 0 ? -8 : 8));
    if (ship.guns & kShipRocking) return;
    const int16_t turn =
        int16_t(angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(int32_t(actor.prevRotation.y))));
    const int16_t v = actor.velocity.z;
    if (v != 0) ship.rollTarget = int16_t((((v >> 6) ^ (v >> 15)) - (v >> 15) + 2) * turn);
    else ship.rollTarget = int16_t(turn * 2);
    const int16_t r = ship.rollTarget;
    d = angleDiff(uint32_t(int32_t(actor.rotation.z)), uint32_t(int32_t(r)));
    if (uint32_t(d + 8) < 0x10) actor.rotation.z = r;
    else actor.rotation.z = int16_t(actor.rotation.z + (d <= 0 ? -8 : 8));
}
// exe 0x595390(actor, d, out): out = the point 0x200 up and d (by the speed's sign) along its rotation; the move test
// from it back to the ship
uint32_t sideProbe(const PlayerEnv& env, const StaticActor& actor, int d, int32_t out[3]) {
    if (actor.velocity.z >= 1) d = -d;
    int32_t o[3];
    rotateVector(actor.rotation, 0, 0x200, -(d << 4), o); // exe 0x5e3970
    out[0] = int32_t(uint32_t(actor.pos.x) + uint32_t(o[0]));
    out[1] = int32_t(uint32_t(actor.pos.y) + uint32_t(o[1]));
    out[2] = int32_t(uint32_t(actor.pos.z) + uint32_t(o[2]));
    const int16_t p[3] = {int16_t(out[0] >> 16), int16_t(out[1] >> 16), int16_t(out[2] >> 16)};
    const int16_t q[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    return testMoveBlocked(env, p, q, CollisionBoxRef::record(stateAddress(TableId::St48GunshipProbeBox)), 0xff); // exe 0x40f5a0
}
// exe 0x595240: its contact of the tick and its ground
void collide(const PlayerEnv& env, StaticActor& actor, ShipFields& ship) {
    uint8_t contact = 0;
    const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const int16_t q[3] = {actor.prevPos.x.whole, actor.prevPos.y.whole, actor.prevPos.z.whole};
    const uint32_t r = testMoveBlocked(env, p, q, actor.collisionBox, 2);
    if (r & kMoveNoCell) ship.contact |= kContactWater; // overwritten below (exe)
    if (int32_t(r) >= 0) {
        const int rect = territoryViolated(actor.pos);                                         // exe 0x5e6ab0
        if (rect != 0 && (territoryPushBack(actor, rect) & kWallOnSides)) contact = kContactTerritory; // exe 0x5e6b40
    } else {
        contact = kContactWall;
        if (!(ship.flags & kShipNoPushOut) && (actorPushOutMask(env, actor, 2) & kWallOnSides)) // exe 0x410980
            contact = kContactWall | kContactTerritory;
    }
    ship.ground = int16_t(actorGround(env, actor)); // exe 0x40f190
    if (ship.guns & kShipProbeSides) {
        const int s = stoppingDistance(actor.velocity.z, -int(actor.acceleration.z));
        int32_t probe[3];
        bool blocked;
        if (int32_t(sideProbe(env, actor, (s >> 16) + 0x280, probe)) < 0) {
            if (actor.acceleration.y >= 1) contact |= kContactClimbing;
            const int16_t lp[3] = {int16_t(probe[0] >> 16), int16_t(probe[1] >> 16), int16_t(probe[2] >> 16)};
            const int g = probeGroundAt(env, lp, CollisionBoxRef::record(stateAddress(TableId::St48GunshipProbeBox)), 0, 0xff); // exe 0x40f1c0
            blocked = ship.ground - 400 > g;
        } else {
            blocked = territoryViolated(int16_t(probe[0] >> 16), int16_t(probe[2] >> 16)) != 0;
        }
        if (blocked) {
            contact |= kContactSideWall;
            if (ship.guns & kShipProbeMarksCell) {
                ship.cellX = int8_t(uint8_t(uint16_t(int16_t(int16_t(probe[0] >> 16) >> 9))));
                ship.cellZ = int8_t(uint8_t(uint16_t(int16_t(int16_t(probe[2] >> 16) >> 9))));
            }
        }
    }
    ship.contact = contact;
}
enum class ShipHit { None = 0, Strong = 1, Dead = 2 };
// exe 0x5951d0: the hits of its box: a strong one (a quarter of its hit points or more) rocks it; out of hit points:
// Dead
ShipHit takeHits(StaticActor& actor, ShipFields& ship) {
    const uint32_t hit = ship.hitBox.result;
    const int n = int(hit & kHitDamageMask);
    if (n == 0 || !(hit & kHitByPlayerOrStrong)) return ShipHit::None;
    actor.tint = kTintFlash;
    const bool weak = n < (actor.maxHitPoints >> 2);
    soundPlayAtPos(weak ? kHurtSound : kStrongHurtSound, actor.pos); // exe 0x5d5380
    if (hitPointsAdd(actor, -n)) return ShipHit::Dead;              // exe 0x5e52f0
    return weak ? ShipHit::None : ShipHit::Strong;
}
// exe 0x595030(actor, side, lim): one shot from the gun at side 0 / 1, aimed 0x40 above the player's feet (clamped to
// +-lim around its yaw / pitch)
void fire(const StaticActor& actor, int side, int lim) {
    int32_t o[3];
    rotateVector(actor.rotation, side != 0 ? 0x400 : -0x400, 0xe80, -0x1380, o); // exe 0x5e3970
    const uint32_t slot = allocActor40(kActorStageClass);                         // exe 0x5e1280
    if (!slot) return;
    ProjectileActor& shot = projectileActorAt(slot);
    EnemyShotFields fields(projectileActorAt(slot));
    shot.flags = kActorLive;
    shot.type = kEnemyShotType;
    shot.variant = shipKind(actor) >= 2 ? kStrongShotVariant : kShotVariant;
    shot.loopFlags |= kActorSweepGroup2;
    shot.pos.x = int32_t(uint32_t(actor.pos.x) + uint32_t(o[0]));
    shot.pos.y = int32_t(uint32_t(actor.pos.y) + uint32_t(o[1]));
    shot.pos.z = int32_t(uint32_t(actor.pos.z) + uint32_t(o[2]));
    const Player& p = player();
    const int16_t aim[3] = {p.pos.x.whole, int16_t(p.pos.y.whole - 0x40), p.pos.z.whole};
    const int16_t from[3] = {shot.pos.x.whole, shot.pos.y.whole, shot.pos.z.whole};
    int dy = yawTo(actor, angleOf(p.pos, shot.pos));
    if (lim < dy) dy = lim;
    if (dy < -lim) dy = -lim;
    int dp = angleDiff(uint32_t(int32_t(actor.rotation.x)), uint32_t(0x800 - pitchOf(aim, from)));
    if (lim < dp) dp = lim;
    if (dp < -lim) dp = -lim;
    shot.rotation.x = int16_t(dp + actor.rotation.x);
    shot.rotation.y = int16_t(actor.rotation.y + dy);
    shot.rotation.z = actor.rotation.z;
    fields.ticks = kShotTicks;
    fields.attack = shipStats(actor).power;
}

// ---- its states [0x8cdf68] -----------------------------------------------------------------------------------------
// exe 0x593310
void bind(StaticActor& actor, ShipFields& ship) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    const uint16_t hp = hasStaticStats(actor.type) ? uint16_t(shipStats(actor).hitPoints) : 0;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.onPlatform = 0;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.velocity.setXY(0, 0);
    actor.velocity.setZSpare(0, 0);
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.collisionBox.pointAt(TableId::St48GunshipShipBox);
    actor.airborne = 1;
    actor.clearClassBlock(); // exe 0x40dc40
    actor.enterState(uint8_t(ShipState::Alive));
    actor.subState = dataScalar<uint8_t>(uint32_t(int32_t(ship.startIndex)) + stateAddress(TableId::St48GunshipStartSubs) + uint32_t(shipKind(actor)) * 4);
    if (!(actor.variant & 0x80)) return;
    actor.pos.y = int32_t(uint32_t(actor.pos.y) - 0xe000000u);
}
void down(const PlayerEnv& env, StaticActor& actor, ShipFields& ship);
// exe 0x5933c0
void alive(const PlayerEnv& env, StaticActor& actor, ShipFields& ship) {
    const ShipHit hit = takeHits(actor, ship);
    if (hit == ShipHit::Strong) {
        if (!(ship.guns & kShipNoRock) && !(ship.flags & kShipRocked)) {
            ship.flags |= kShipRocked;
            rockInit(actor); // exe 0x5a06e0
        }
    } else if (hit == ShipHit::Dead) {
        actor.enterState(uint8_t(ShipState::Down));
        down(env, actor, ship);
        return;
    }
    switch (ShipSub(actor.subState)) { // [0x8cdf80]
    case ShipSub::Start: start(actor, ship); break;
    case ShipSub::Cruise: cruise(actor, ship); break;
    case ShipSub::Rocked: rocked(actor, ship); break;
    case ShipSub::StrafeStart: strafeStart(actor, ship); break;
    case ShipSub::Strafe: strafe(actor, ship); break;
    case ShipSub::SwingRound: swingRound(actor, ship); break;
    case ShipSub::ClimbCell: climbCell(env, actor, ship); break;
    case ShipSub::ClimbOver: climbOver(actor, ship); break;
    case ShipSub::WaitTurn: waitTurn(actor, ship); break;
    case ShipSub::WaitLoop: waitLoop(actor, ship); break;
    case ShipSub::Climb: climb(actor, ship); break;
    case ShipSub::RunIn: runIn(actor, ship); break;
    case ShipSub::RunFire: runFire(actor, ship); break;
    case ShipSub::ClimbAway: climbAway(actor, ship); break;
    case ShipSub::Veer: veer(actor, ship); break;
    case ShipSub::StraightRun: straightRun(actor, ship); break;
    case ShipSub::RunOn: runOn(actor, ship); break;
    default: notPortedAt(kStoryPort, "static 0x593190 sub-state past [0x8cdf80]", actor.subState);
    }
    if (ship.flags & kShipRocked) {
        ship.guns |= kShipRocking;
        if (rock(actor) != 0) { // exe 0x59e580
            ship.flags &= uint8_t(~kShipRocked);
            ship.guns &= uint8_t(~kShipRocking);
        }
    }
    if (ship.guns & kShipSway) sway(actor); // exe 0x5a0600
    attitude(actor, ship);
    collide(env, actor, ship);
}
// exe 0x594970: shot down -- spinning and falling along its yaw at the time; at a wall with a steep roll: a blast, a
// Servbot bailing out, the drops, freed; smoke every other tick
void down(const PlayerEnv& env, StaticActor& actor, ShipFields& ship) {
    const DownStep step = DownStep(actor.subState);
    if (step == DownStep::Crash) {
        if (const uint32_t slot = allocActor60(0)) { // exe 0x5e1300
            StaticActor& blast = staticActorAt(slot);
            BlastParams params(actorHeaderAt(slot));
            blast.flags = kActorLive;
            blast.type = typeByte(GlobalStaticType::Blast);
            blast.loopFlags |= kActorSweepGroup2;
            blast.variant = 0;
            blast.pos.x = actor.pos.x;
            blast.pos.y = actor.pos.y;
            params.size = 0xc0;
            blast.pos.z = actor.pos.z;
            params.attack = 4;
        }
        if (const uint32_t slot = allocActor20()) { // exe 0x5e1160
            AnimatedActor& servbot = animatedActorAt(slot);
            servbot.flags = kActorLive;
            servbot.type = kPirateServbotType;
            servbot.subtype = kServbotOnFootSubtype;
            servbot.variant = kBailOutVariant;
            servbot.loopFlags |= kActorSweepGroup2;
            servbot.pos.x = actor.pos.x;
            servbot.pos.z = actor.pos.z;
            servbot.pos.y.whole = actor.pos.y.whole;
        }
        spawnDrops(actor.pos, shipStats(actor).dropTable); // exe 0x5f1270
        freeActor(actor);
        return;
    }
    if (step == DownStep::Start) {
        countDefeat(); // exe 0x5e53e0
        ship.turnStep = int16_t(actor.spin.y >= 1 ? 0x80 : 0xff80);
        actor.spin.x = int16_t(actor.rotation.z >= 1 ? 0x40 : 0xffc0);
        actor.velocity.y = 0;
        ship.heading = actor.rotation.y;
        actor.acceleration.y = 4;
        ship.count = 0;
        actor.subState = uint8_t(DownStep::Fall);
        ship.flags &= uint8_t(~kShipNoPushOut);
    }
    if (step == DownStep::Start || step == DownStep::Fall) {
        actor.velocity.y = int16_t(actor.velocity.y + actor.acceleration.y);
        if (actor.velocity.y <= 0x1ff) actor.velocity.y = 0x200;
        actor.spin.y = int16_t(actor.spin.y + ship.turnStep);
        addToYaw(actor, actor.spin.y >> 4);
        if (actor.spin.y >= 0x801) actor.spin.y = 0x800;
        if (actor.spin.y <= -0x801) actor.spin.y = int16_t(0xf800);
        if (uint16_t(actor.rotation.z + 0x200) <= 0x3ff) actor.rotation.z = int16_t(actor.spin.x + actor.rotation.z);
        moveAlongYaw(actor.pos, ship.heading, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e36f0
        const uint32_t r = actorTestMove(env, actor);                                // exe 0x40f570
        if (int32_t(r) < 0) {
            queueBreakableWallHit(r, 0xff);                 // exe 0x58d4e0
            const uint32_t pushed = actorPushOut(env, actor); // exe 0x410960
            if (int32_t(pushed) < 0) actor.pos = actor.prevPos;
            if (pushed & kWallOnSides) actor.velocity.z = 0;
            if (pushed & kWallOnY) actor.velocity.y = 0;
            if (uint16_t(actor.rotation.z + 0x100) >= 0x201) actor.subState = uint8_t(DownStep::Crash);
        }
    }
    if (!(ship.count & 1)) gunshipSmoke(actor);
    ship.count += 1u;
}
void gunship(const PlayerEnv& env, StaticActor& actor) { // exe 0x593190
    ShipFields& ship = classState<ShipFields>(actor);
    const StaticActorStats stats = shipStats(actor);
    actor.tint = kTintNeutral;
    actor.runFlags |= kActorRunMarked;
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (ShipState(actor.state)) { // [0x8cdf68]
    case ShipState::Bind: bind(actor, ship); break;
    case ShipState::Alive: alive(env, actor, ship); break;
    case ShipState::Down: down(env, actor, ship); break;
    default: notPortedAt(kStoryPort, "static 0x593190 state past [0x8cdf68]", actor.state);
    }
    if (actor.flags & kActorLive) {
        const uint8_t guns = ship.guns;
        if (guns & kGunLeft) fire(actor, 0, 0x40);
        if (guns & kGunRight) fire(actor, 1, 0x40);
        const uint8_t rest = uint8_t(ship.guns & ~(kGunLeft | kGunRight));
        ship.guns = rest;
        const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
        ship.hitBox.result = 0;
        ship.hitBox.previousX = actor.prevPos.x.whole;
        ship.hitBox.previousZ = actor.prevPos.z.whole;
        const uint32_t shape = hitOwner(actor.slot) | ((rest & kShipNoLockOn) ? kHitNoLockOn : 0u) |
                               hitHeightLevel(stats.hitHeight) | hitGroup(kHitGroupBodies) | hitRadius(0xa0);
        hitListSubmit(p, ship.hitBox, shape, uint32_t(stats.damage) | kHitAttackSolid); // exe 0x5e4120
        if (pointVisibleOrNear(actor.pos, 0) != 0) { // exe 0x5e59a0: off screen -> counted up to 0x7f
            if (ship.offScreen < 0x7f) ship.offScreen += 1u;
        } else {
            ship.offScreen = 0;
        }
        // the shadow (exe 0x5f20f0(actor, 0xc0, 0x100, 0)) -- render output
    }
    if (!(ship.contact & kContactWater)) return;
    freeActor(actor);
}

} // namespace

void st48GunshipUpdate(const PlayerEnv& env, uint32_t actor) { gunship(env, staticActorAt(actor)); }

} // namespace od::port
