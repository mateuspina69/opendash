// The player's motion helpers the sub-states share: the turns (the stick's analog curve, the yaw's turn speed), the
// aim pitch's turn and its return to level, the body lean and the head turn, the velocities' glide / stop on the floor
// kinds, the run's speed and its steering, the jumps' gravity, and the move in the facing frame.
#pragma once

#include <cstdint>


namespace od::port {

struct Player;

// exe 0x571bb0(player, step, limit, stick): the yaw's turn speed (spin.y) accelerates by step toward the stick's side
// up to limit (a reversal stops it first), yaw += speed; the lean follows the speed unless a weapon is busy. Returns the
// side: 1 positive, 2 negative
uint32_t turnTowardStick(Player& p, int16_t step, int limit, int stick);
// exe 0x571ae0: ... on the turn stick when it is out of the dead zone and nothing is locked on; else the turn stopped (0)
uint32_t turnByStick(Player& p, int16_t step, int limit);
// A turn's rate: the speed's step per tick and its limit (a digital stick's; the analog curve's limit)
struct TurnRate {
    int16_t step;
    int limit;
};
constexpr TurnRate kStandTurn{4, 0x60};     // standing / hanging
constexpr TurnRate kStandSlowTurn{2, 0x20}; // standing with the walk modifier
constexpr TurnRate kWalkTurn{2, 0x20};
constexpr TurnRate kWalkBackTurn{4, 0x40};
constexpr TurnRate kAirTurn{4, 0x20};
constexpr TurnRate kRunTurn{0x20, 0x20};
constexpr TurnRate kSkateCruiseTurn{0x18, 0x18};
constexpr TurnRate kSkateTopTurn{0x10, 0x10};
constexpr TurnRate kSkateBrakeTurn{0x20, 0x20};
// the analog curve's shift: kAnalogSharp ((|stick| - 0x38)^2 >> 6), kAnalogGentle (>> 7)
constexpr int8_t kAnalogSharp = 0, kAnalogGentle = 1;
// exe 0x571950(player, step, limit, shift): ... with the analog curve: rate ((|stick| - 0x38)^2 >> (6 + shift)) + 1 up to
// limit (a digital stick: the rate as given)
uint32_t turnByAnalogStick(Player& p, TurnRate rate, int8_t shift);
// exe 0x571c70(a, b, step): the angle b moved toward a by step (12-bit signed angles), held within +-0x200
int approachAngle(int a, int b, int step);
// exe 0x571cf0: the body lean target follows the move direction's offset (+-0x400; halved with the upper body mixed)
void leanTowardMove(Player& p);
// exe 0x571d90: the lean eased toward its target (0x100 a tick), the head turn toward the move direction (0x80 a tick)
void easeLeanAndHead(Player& p);
// exe 0x571f00(player, angle, flags): on a slippery floor the velocities turned by angle - the move direction's offset
// (flags & 1: the local velocity too) and their vertical parts stopped; else stopped (flags & 1: the local one too)
void turnOrStopVelocities(Player& p, int angle, uint8_t flags);
constexpr uint8_t kAlsoLocalVelocity = 1;
// exe 0x572050(velocity, d): a velocity component decays toward 0 by d
void decayToward0(int16_t& v, int16_t d);
// exe 0x572000(player, d): on a slippery floor the planar velocities decay by d
void glideDecay(Player& p, int16_t d);
// exe 0x572400: the aim pitch and the camera's look pitch return to level by 0x40 a tick (both level: the recentre ends)
void recentreAim(Player& p);
// exe 0x571a10(player, step, limit, dir): the aim pitch's turn speed (spin.x) accelerates by step toward dir's side up to
// limit, the pitch held within +-0x200; 1 = the pitch went past the camera's look pitch in the turn direction
int turnAimPitch(Player& p, int16_t step, int limit, int dir);
// exe 0x5719c0: ... on the look stick (its direction by the look option) when out of the dead zone
int turnAimByLookStick(Player& p, int16_t step, int limit);
// exe 0x571b30(player, step, limit, anim): the turn in place on the turn stick (anim: the shuffle / idle animation)
uint32_t turnInPlace(Player& p, int16_t step, int limit, bool anim);
// exe 0x571910: the local velocity integrated in the movement frame (yaw - the move direction's offset)
void moveInFacing(Player& p, int16_t x, int16_t y, int16_t z);
// exe 0x576160(player, mode): the run's speed (forward -0x200, a ramp on a slippery floor or in water) and its stick
// turn; mode != 0: the velocities stopped and the lean cleared instead
void runSpeed(Player& p, int mode);
constexpr int kRunStop = 1;
// exe 0x576020: the move direction turned toward the stick by 0x100 a tick; the arm put down when the body turns away
// from the head
void steerMoveDirection(Player& p);
// exe 0x577280(player, mode): the gravity of the jumps: kGravityRise 0x30, kGravityCut 0x240 (the jump released),
// kGravityFall 0x30 (3 floating down with a floating lift), halved in water, raised by the burden
void applyJumpGravity(Player& p, int mode);
constexpr int kGravityRise = 0, kGravityCut = 1, kGravityFall = 2;

// the velocities (local and external: x / y / z, their spare lanes kept) stopped (the exe's inline stop) ...
void stopMotion(Player& p);
// ... on a floor that is not slippery
void stopUnlessSlippery(Player& p);

// exe 0x5e3e50(out, x, y, z): (x, y, z) turned by Rx(the player's aim pitch) through the float transform (exe
// 0x40bbb0): the throws / launches along the aim
void playerAimVector(int x, int y, int z, int32_t out[3]);

} // namespace od::port
