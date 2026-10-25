// The enemies' motion helpers the classes share: the planar speed raised or lowered by the acceleration toward a limit
// (exe 0x5e5450 / 0x5e54a0 / 0x5e55a0), a speed or a turn rate stepped toward a limit or back toward 0 (exe 0x5e5510 /
// 0x5e60f0), the facing / distance test of one actor against another (exe 0x5e5a90) and the facing test of a point
// (exe 0x5036e0).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"

namespace od::port {

// exe 0x5e5450(actor, limit): below the limit the planar velocity (x / z) gains the acceleration: 0; at it or above: 1
int accelerateToward(Actor& actor, int limit);
// exe 0x5e54a0(actor, limit): above the limit the planar velocity loses the acceleration: 0 while neither component
// changed sign; at the limit or below, or a sign change: 1
int decelerateToward(Actor& actor, int limit);
// exe 0x5e55a0(actor, limit): the planar speed toward the limit -- at or below it accelerated (1 once at the limit,
// else 0), above it decelerated (-1 once at or below the limit or a component changed sign, else 0)
int speedToward(Actor& actor, int limit);
// exe 0x5e5510(value, step, limit): the value moved by step (its sign matched to the value's): |value| above the limit
// toward 0 (-1 once at or below it or past 0, else 0), else away from 0 (1 once at the limit, else 0)
int stepTowardLimit(int16_t& value, uint16_t step, int limit);
// exe 0x5e60f0(a, b, stepA, stepB): each value moved toward 0 by its step unless it is already within it
void settleTowardZero(int16_t& a, int16_t& b, int stepA, int stepB);
// exe 0x5e5a90(actor, other, tolerance, distance): kFacingOff when the actor's yaw is off the direction from the other
// to it by more than the tolerance, | kFacingFar when the other is farther than the distance (x / z)
constexpr int kFacingOff = 1, kFacingFar = 2;
int facingAndRange(const Actor& actor, const Actor& other, int tolerance, int distance);
// exe 0x5036e0(actor, point, tolerance): the 16.16 point (its x / z) within the tolerance of the actor's yaw
bool facingWithin(const Actor& actor, int32_t x, int32_t z, int tolerance);
bool facingWithin(const Actor& actor, const Fixed16Vec3& point, int tolerance);

} // namespace od::port
