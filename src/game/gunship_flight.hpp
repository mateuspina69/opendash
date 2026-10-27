// The flight of the static gunship family (Nino's static type 0x1b ships, the ST15 / ST19 drones, the ST48 gunship): the
// bytes the family gives meanings of its own and the routines its classes share (exe 0x59e0e0 .. 0x5a3290),
// game/gunship_flight.cpp. The classes: content/nino/square_gunship (0x59f880), mission_gunships (0x59d310 / 0x5a07b0),
// st18_gunships (0x5a2cc0 / 0x59e630), st15_props (the drones 0x5a2620 / 0x5a2060), content/saul_kada/st48_gunship
// (0x593190: the turn damping, the rocking and the sway).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port {

// The family's bytes over a static slot: the motion lanes it gives meanings of its own and its class block
struct GunshipFields : ActorClassState {
    explicit GunshipFields(StaticActor& actor) : rollTarget(actor.rotation.spare), heading(actor.velocity.spare), pointX(actor.velocity.spare), rollSpeed(actor.acceleration.x), pointZ(actor.acceleration.spare), turnStep(actor.spin.spare), homeHeight(actor.wallHit), hitBox(actor.hitBox), tint(actor.tint), pitchTarget(actor.gunshipPitchTarget()), timer(actor.gunshipTimer()), turnMargin(actor.gunshipCooldown()), attackCooldown(actor.gunshipAttackCooldown()), cooldown(actor.gunshipCooldown(), actor.gunshipAttackCooldown()) {}
    int16_t& rollTarget; // the rotation's spare lane: the roll the turn asks for (the square gunship:
    int16_t& heading; // the velocity's spare lane: the yaw it moves along (a run, a fall) ...
    int16_t& pointX; // ... or the x of the point it steers to
    int16_t& rollSpeed; // the acceleration's x: the rocking's roll speed
    int16_t& pointZ; // the acceleration's spare lane: the z of its point (a dive: its height)
    int16_t& turnStep; // the spin's spare lane: the turn rate's step
    FieldAlias<int32_t, uint32_t> homeHeight; // the wall-hit word: the height it holds (whole units)
    HitBoxHead& hitBox;
    uint16_t& tint; // the slot's tint word
    FieldAlias<int8_t, uint8_t> pitchTarget; // the pitch the attitude turns to (/ 4)
    uint8_t& timer;
    FieldAlias<int8_t, uint8_t> turnMargin; // the mission gunships: the steering limit's offset (a veer: the walls seen)
    uint8_t& attackCooldown;
    uint8_t flight = 0; // kGun* / kFlight*
    uint8_t behaviour = 0; // kBehave*
    uint8_t contact = 0; // kContact*: the room collision of the tick
    uint8_t burst = 0; // the shots / bombs left of a burst
    PiecesField<int16_t> cooldown; // the square gunship / the scripted ones: a cooldown (over the margin and the
                                   // attack cooldown bytes)
    void resetTintBytes() { tint = kTintNeutral; }

    void clearBlock() override {
        pitchTarget = 0;
        turnMargin = 0;
        flight = 0;
        behaviour = 0;
        contact = 0;
        burst = 0;
        cooldown = 0;
    }
};
// flight (+0xc4)
constexpr uint8_t kGunLeft = 0x01;          // fire the left gun this tick
constexpr uint8_t kGunRight = 0x02;         // ... the right gun
constexpr uint8_t kGuns = kGunLeft | kGunRight;
constexpr uint8_t kFlightNoRock = 0x04;     // a strong hit does not rock it (a dive)
constexpr uint8_t kFlightRocking = 0x08;    // the rocking holds the roll (no roll toward the turn)
constexpr uint8_t kFlightSway = 0x10;       // x / z shifted with the yaw change
constexpr uint8_t kFlightProbeWalls = 0x20; // the walls 0x400 ahead probed (the square gunship: its dive's hit entry)
constexpr uint8_t kFlightDown = 0x40;       // shot down and falling: its hit entry is no lock-on target
constexpr uint8_t kFlightEntering = 0x80;   // the wave's entry: no attitude, collision or cooldown
// behaviour (+0xc5)
constexpr uint8_t kBehaveLevelA = 0x01;     // either of the two: the settle does not bob
constexpr uint8_t kBehaveLevelB = 0x02;
constexpr uint8_t kBehaveLevel = kBehaveLevelA | kBehaveLevelB;
constexpr uint8_t kBehaveSettling = 0x04;   // settling to a height
constexpr uint8_t kBehaveStraight = 0x08;   // a straight run: no steering
constexpr uint8_t kBehaveRocking = 0x10;    // rocked by a strong hit
constexpr uint8_t kBehaveNoPushOut = 0x20;  // the room collision does not push it out
// contact (+0xc6)
constexpr uint8_t kContactFloor = 0x01;     // pushed off a floor / ceiling
constexpr uint8_t kContactWallLeft = 0x02;  // a wall 0x400 ahead on the left
constexpr uint8_t kContactWallRight = 0x04; // ... on the right
constexpr uint8_t kContactWall = 0x08;      // the move hit the room
constexpr uint8_t kContactWater = 0x10;     // the move's water bit (0x20000000)
constexpr uint8_t kContactSide = 0x20;      // pushed off sideways

// the result of the hit box (gunshipHits / squareGunshipHits)
enum class GunshipHit { None = 0, Strong = 1, Dead = 2 };

// exe 0x5a3170(actor, angle, lim, acc): the turn rate (spin y) toward the angle (|rate| <= lim, step acc), the yaw +=
// rate >> 4; the angle difference (0 within the dead zone)
int steerRate(StaticActor& actor, uint32_t angle, int lim, int16_t acc);
// exe 0x5a3290(actor, acc): the turn rate back toward 0; 1 when it got there
int steerDamp(StaticActor& actor, int16_t acc);
// exe 0x5a06b0(actor, target, lim, acc): steerRate toward the bearing from the 16.16 point
int steerToward(StaticActor& actor, const Fixed16Vec3& target, int lim, int16_t acc);
// ... toward the 16.16 point (x, z)
int steerToward(StaticActor& actor, int32_t targetX, int32_t targetZ, int lim, int16_t acc);
// exe 0x59e2a0(actor, lim): the vertical speed moved by its step toward |lim|: 1 reached going out, -1 back in
int verticalApproach(StaticActor& actor, int lim);
// exe 0x5a0670: the vertical speed += its step, bouncing between -0x60 and 0x60
void bobVertical(StaticActor& actor);
// exe 0x5a06e0: the rocking started (the roll quartered, the amplitude 0x400, the roll speed toward the roll's side)
void rockInit(StaticActor& actor);
// exe 0x59e580: the roll swings with a damped amplitude; 1 once it settled
int rock(StaticActor& actor);
// exe 0x5a0600: x / z shifted by the yaw change (0x118 units)
void sway(StaticActor& actor);
// exe 0x5a25a0: the roll toward the turn (8 a tick), unless rocking
void rollToTurn(StaticActor& actor);
// exe 0x59e420: the pitch toward the pitch target (8 a tick), then the roll toward the turn
void attitude(StaticActor& actor);
// the smoke of a fall: a stage static type 3 at the actor
void gunshipSmoke(const StaticActor& actor);
// exe 0x59e390(actor, v): a bomb (stage static type 0x22) 0x90 behind along the yaw, 0x80 below, falling at v; sound
// 0x18a
void dropBomb(const StaticActor& actor, int16_t v);
// exe 0x59e0e0: the hits of the box -- a strong one (or one of a quarter of the hit points) rocks it; out of hit points:
// Dead
GunshipHit gunshipHits(StaticActor& actor);
// ... whether it takes its hit branch this tick (damage from the player or a strong push): the stack it leaves differs
// then (game/... classes read it as stale stack)
bool gunshipHitTaken(const StaticActor& actor);
// exe 0x5a28e0 / 0x5a2380 (with minFall 0x100) / 0x59f380 (with the drops): shot down -- spins and falls (0x5a ticks or
// a wall), then a blast and gone; smoke every other tick
void gunshipCrash(const PlayerEnv& env, StaticActor& actor, int16_t minFall, bool drops);
// The guns of the family: one shot (pool 0x40 type 9) from the gun at side 0 / 1, aimed at the player (clamped to +-lim
// around the ship's yaw / pitch); the shot's variant and damage are each class's
enum class GunshipGun {
    Square,  // exe 0x5a0370: the shot's variant 0x84 for variant 2, the damage of the variant-1 row's power
    Mission, // exe 0x59f5b0: variant 0x84 from variant 2 up, the variant-0 row's power
    Drone,   // exe 0x5a2b00: variant 0x84 from variant 2 up, the variant-1 row's power
};
void gunshipFire(const StaticActor& actor, int side, int lim, GunshipGun gun);
// the guns asked for this tick fired and the requests cleared
void gunshipFireAsked(StaticActor& actor, int lim, GunshipGun gun);

} // namespace od::port
