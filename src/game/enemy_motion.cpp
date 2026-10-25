// The enemies' motion helpers (see enemy_motion.hpp).
#include "game/enemy_motion.hpp"

#include "game/angles.hpp"
#include "game/distances.hpp"
#include "game/fixed_point.hpp"
#include "game/vectors.hpp"

namespace od::port {

namespace {

// the planar speed of the velocity (x / z)
int planarSpeed(const Actor& actor) {
    const int vx = actor.velocity.x, vz = actor.velocity.z;
    return isqrt(int32_t(uint32_t(vz * vz) + uint32_t(vx * vx))); // exe 0x40b980
}
// the velocity's x / z gain / lose the acceleration's (16-bit)
void accelerate(Actor& actor) {
    actor.velocity.x = wrap16(actor.velocity.x + actor.acceleration.x);
    actor.velocity.z = wrap16(actor.velocity.z + actor.acceleration.z);
}
void decelerate(Actor& actor) {
    actor.velocity.x = wrap16(actor.velocity.x - actor.acceleration.x);
    actor.velocity.z = wrap16(actor.velocity.z - actor.acceleration.z);
}
// the two values have different signs
bool signChanged(int before, int after) { return (before ^ after) < 0; }
int magnitude(int v) { return v < 0 ? -v : v; }

} // namespace

int accelerateToward(Actor& actor, int limit) {
    if (planarSpeed(actor) >= limit) return 1;
    accelerate(actor);
    return 0;
}

int decelerateToward(Actor& actor, int limit) {
    const int16_t vx = actor.velocity.x, vz = actor.velocity.z;
    if (planarSpeed(actor) > limit) {
        decelerate(actor);
        if (!signChanged(actor.velocity.x, vx) && !signChanged(actor.velocity.z, vz)) return 0;
    }
    return 1;
}

int speedToward(Actor& actor, int limit) {
    const int vx = actor.velocity.x, vz = actor.velocity.z;
    if (limit >= planarSpeed(actor)) {
        accelerate(actor);
        return limit > planarSpeed(actor) ? 0 : 1;
    }
    decelerate(actor);
    if (limit >= planarSpeed(actor)) return -1;
    if (signChanged(vx, actor.velocity.x) || signChanged(vz, actor.velocity.z)) return -1;
    return 0;
}

int stepTowardLimit(int16_t& value, uint16_t step, int limit) {
    const int16_t v = value;
    if (v != 0 && (uint16_t(v) & 0x8000) != (step & 0x8000)) step = uint16_t(-step);
    if (limit < magnitude(v)) {
        const int16_t next = int16_t(uint16_t(uint16_t(v) - step));
        value = next;
        return (magnitude(next) <= limit || signChanged(next, v)) ? -1 : 0;
    }
    const int16_t next = int16_t(uint16_t(uint16_t(v) + step));
    value = next;
    return limit <= magnitude(next) ? 1 : 0;
}

void settleTowardZero(int16_t& a, int16_t& b, int stepA, int stepB) {
    auto settle = [](int16_t& f, int step) {
        int16_t v = f;
        if (uint32_t(step * 2) <= uint32_t(int(v) + step)) v = int16_t(v + (v >= 0 ? -step : step));
        f = v;
    };
    settle(a, stepA);
    settle(b, stepB);
}

int facingAndRange(const Actor& actor, const Actor& other, int tolerance, int distance) {
    int r = 0;
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleOf(other.pos, actor.pos)));
    if (uint32_t(d + tolerance) > uint32_t(tolerance * 2)) r = kFacingOff;
    if (planarDistance(actor.pos, other.pos) > distance) r |= kFacingFar;
    return r;
}

bool facingWithin(const Actor& actor, int32_t x, int32_t z, int tolerance) {
    const uint16_t angle = uint16_t(uint16_t(angleOf(actor.pos.x, actor.pos.z, x, z)) - uint16_t(actor.rotation.y));
    return int((uint32_t(angle) + uint32_t(tolerance) - 0x800) & 0xfff) <= int(int16_t(tolerance)) * 2;
}

bool facingWithin(const Actor& actor, const Fixed16Vec3& point, int tolerance) {
    return facingWithin(actor, point.x, point.z, tolerance);
}

} // namespace od::port
