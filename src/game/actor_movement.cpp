// The actors' movement through the room (see actor_movement.hpp).
#include "game/actor_movement.hpp"

#include <array>
#include <optional>

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/distances.hpp"
#include "game/fixed_point.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/vectors.hpp"

namespace od::port {

namespace {

// a collision box's extents {minX, maxX, minY, maxY, minZ, maxZ} (the order the room queries take), none for none
std::optional<std::array<int16_t, 6>> boxExtents(const CollisionBoxRef& ref) {
    if (!ref) return std::nullopt;
    const BoxAccess box = *ref;
    return std::array<int16_t, 6>{box.minX, box.maxX, box.minY, box.maxY, box.minZ, box.maxZ};
}
// ... as the room queries take them (nullptr: none)
const int16_t* extentsOrNone(const std::optional<std::array<int16_t, 6>>& box) { return box ? box->data() : nullptr; }

// the integer parts of a 16.16 point
std::array<int16_t, 3> wholeParts(const Fixed16Vec3& v) { return {v.x.whole, v.y.whole, v.z.whole}; }

} // namespace

uint32_t collisionLayerMask(uint32_t slot) {
    switch (actorSlotClass(slot & 0xff)) {
    case kPlayerClass: return kPlayerLayers;
    case kShotClass: return kShotLayers;
    case kAnimatedClass: return kAnimatedLayers;
    case kProjectileClass: return kProjectileLayers;
    case kStaticClass: return kStaticLayers;
    default: return kAllLayers;
    }
}

void rotateByYaw(int yaw, int x, int y, int z, int32_t out[3]) {
    const int s = fixedSin(yaw), c = fixedCos(yaw);
    // the exe's Ry(yaw) = [c 0 s; 0 4096 0; -s 0 c] (its product with the identity is exact)
    const int16_t m[9] = {int16_t(c), 0, int16_t(s), 0, kFixedOne, 0, int16_t(-s), 0, int16_t(c)};
    const int64_t v[3] = {x, y, z};
    for (int i = 0; i < 3; ++i) out[i] = int32_t(m[i * 3 + 2] * v[2] + m[i * 3 + 1] * v[1] + m[i * 3] * v[0]);
}

void integrateVelocity(Actor& actor, int x, int y, int z) {
    int32_t d[3];
    rotateByYaw(actor.rotation.y, x, y, z, d);
    actor.pos.x = int32_t(actor.pos.x + d[0]);
    actor.pos.y = int32_t(actor.pos.y + d[1]);
    actor.pos.z = int32_t(actor.pos.z + d[2]);
}

void moveAlongYaw(Fixed16Vec3& pos, int yaw, int x, int y, int z) {
    int32_t d[3];
    rotateByYaw(yaw, x, y, z, d);
    pos.x += uint32_t(d[0]);
    pos.y += uint32_t(d[1]);
    pos.z += uint32_t(d[2]);
}

void moveAlongYaw16(ShortVec3& pos, int yaw, int x, int y, int z) {
    int32_t d[3];
    rotateByYaw(yaw, x, y, z, d);
    pos.x = wrap16(pos.x + wholePart(d[0]));
    pos.y = wrap16(pos.y + wholePart(d[1]));
    pos.z = wrap16(pos.z + wholePart(d[2]));
}

void moveAlongYaw16(int16_t pos[3], int yaw, int x, int y, int z) {
    int32_t d[3];
    rotateByYaw(yaw, x, y, z, d);
    for (int k = 0; k < 3; ++k) pos[k] = wrap16(pos[k] + wholePart(d[k]));
}

void moveAlongRotation(Fixed16Vec3& pos, const int16_t rotation[3], int x, int y, int z) {
    int32_t d[3];
    rotateVector(rotation, x, y, z, d);
    pos.x += uint32_t(d[0]);
    pos.y += uint32_t(d[1]);
    pos.z += uint32_t(d[2]);
}

void moveAlongRotation(Fixed16Vec3& pos, const ShortVec4& rotation, int x, int y, int z) {
    int32_t d[3];
    rotateVector(rotation, x, y, z, d);
    pos.x += uint32_t(d[0]);
    pos.y += uint32_t(d[1]);
    pos.z += uint32_t(d[2]);
}

void moveAlongRotation16(int16_t pos[3], const int16_t rotation[3], int x, int y, int z) {
    int32_t d[3];
    rotateVector(rotation, x, y, z, d);
    for (int k = 0; k < 3; ++k) pos[k] = wrap16(pos[k] + wholePart(d[k]));
}

void moveAlongRotation16(ShortVec3& pos, const ShortVec4& rotation, int x, int y, int z) {
    int32_t d[3];
    rotateVector(rotation, x, y, z, d);
    pos.x = wrap16(pos.x + wholePart(d[0]));
    pos.y = wrap16(pos.y + wholePart(d[1]));
    pos.z = wrap16(pos.z + wholePart(d[2]));
}

int launchSpeed(const int32_t from[3], const int32_t to[3], int speed, int gravity, uint32_t maxDistance) {
    const int dx = wrap16(wholePart(to[0]) - wholePart(from[0])), dz = wrap16(wholePart(to[2]) - wholePart(from[2]));
    uint32_t d = uint32_t(isqrt(int32_t(uint32_t(dx * dx) + uint32_t(dz * dz)))); // exe 0x5e3b00
    if (d > maxDistance) d = maxDistance;
    const int32_t n = int32_t(d << 16);
    if (speed != 0 && n != 0) {
        const int32_t ticks = n / int32_t(uint32_t(speed) << 12);
        if (ticks != 0) {
            const int32_t dy = int32_t(uint32_t(from[1]) - uint32_t(to[1])) / ticks;
            const int32_t fall = int32_t((uint32_t(gravity) << 12) * uint32_t(ticks)) >> 1;
            return -(int32_t(uint32_t(dy) + uint32_t(fall)) >> 12);
        }
    }
    return -(int32_t(uint32_t(speed) << 12) >> 12);
}

int launchSpeed(const Fixed16Vec3& from, const Fixed16Vec3& to, int speed, int gravity, uint32_t maxDistance) {
    const int32_t f[3] = {from.x, from.y, from.z}, t[3] = {to.x, to.y, to.z};
    return launchSpeed(f, t, speed, gravity, maxDistance);
}

void lobVelocity(const int32_t from[3], const int32_t to[3], int pitch, int gravity, uint32_t maxDistance, int16_t& vy,
                 int16_t& vz) {
    vy = 0;
    vz = 0;
    if ((uint32_t(pitch) & 0x7ff) == 0) return;
    const int dx = wrap16(wholePart(to[0]) - wholePart(from[0])), dz = wrap16(wholePart(to[2]) - wholePart(from[2]));
    uint32_t d = uint32_t(isqrt(int32_t(uint32_t(dx * dx) + uint32_t(dz * dz)))); // exe 0x5e3b00
    if (d > maxDistance) d = maxDistance;
    const int angle = int(uint32_t(-pitch) & kAngleMask);
    const int32_t cosine = fixedCos(angle);
    if (cosine == 0) notPortedAt("actor movement", "exe 0x5e3f50: a division by a zero cosine, pitch", uint32_t(pitch));
    const int32_t slope = (int32_t(fixedSin(angle)) << 4) / cosine;
    const int32_t drop = int32_t(uint32_t(slope) * d) >> 4;
    const int32_t den = int32_t(uint32_t(int32_t(wholePart(from[1])) - drop - int32_t(wholePart(to[1]))) << 1);
    if (den == 0) return;
    int32_t q = int32_t(d * uint32_t(gravity)) / den;
    q = int32_t(uint32_t(q) * d);
    if (q < 0) q = int32_t(0u - uint32_t(q));
    const int32_t v = isqrt(int32_t(uint32_t(q) << 4)); // exe 0x40b980
    vy = wrap16(-(int32_t(uint32_t(v) * uint32_t(slope)) >> 4));
    vz = wrap16(v);
}

void lobVelocity(const Fixed16Vec3& from, const Fixed16Vec3& to, int pitch, int gravity, uint32_t maxDistance,
                 int16_t& vy, int16_t& vz) {
    const int32_t f[3] = {from.x, from.y, from.z}, t[3] = {to.x, to.y, to.z};
    lobVelocity(f, t, pitch, gravity, maxDistance, vy, vz);
}

int32_t stoppingDistance(int32_t speed, int32_t deceleration) {
    if (deceleration == 0) return 0x7fffffff;
    if (speed < deceleration) {
        speed = -speed;
        deceleration = -deceleration;
    }
    const int32_t n = int32_t(uint32_t(speed) << 12);
    const int32_t ticks = n / int32_t(uint32_t(deceleration) << 12);
    return int32_t(0u - uint32_t(ticks) * uint32_t(n >> 1));
}

namespace {

constexpr uint32_t kWallOnX = kWallOnXPlus | kWallOnXMinus;
constexpr uint32_t kWallOnZ = kWallOnZPlus | kWallOnZMinus;

// the yaw of exe 0x410740 / 0x4107d0 reflected off the walls of a push-out result (shared by both)
void reflectYawOffWalls(int16_t& yaw, uint32_t result) {
    int16_t mirror = 0;
    bool mirrored = false;
    if ((result & kWallOnX) && (result & kWallOnZ)) { // a corner
        if (result & kWallOnXPlus)
            mirror = (result & kWallOnZPlus) ? int16_t(-kQuarterTurn) : int16_t(kQuarterTurn);
        else
            mirror = (result & kWallOnZPlus) ? int16_t(kQuarterTurn) : int16_t(-kQuarterTurn);
        mirrored = true;
    } else {
        if (result & kWallOnX) yaw = int16_t(uint32_t(-int(uint16_t(yaw))) & kAngleMask);
        if (result & kWallOnZ) {
            mirror = -kHalfTurn;
            mirrored = true;
        }
    }
    if (mirrored) yaw = int16_t(uint32_t(mirror - int(uint16_t(yaw))) & kAngleMask);
}

} // namespace

int reflectRotationOffWalls(int16_t& pitch, int16_t& yaw, uint32_t result) {
    reflectYawOffWalls(yaw, result);
    if (!(result & kWallOnY)) return 0;
    pitch = int16_t(uint32_t(-int(uint16_t(pitch))) & kAngleMask);
    return (result & kWallOnYPlus) ? 1 : 0;
}

int reflectRotationOffWalls(ShortVec4& rotation, uint32_t result) {
    int16_t pitch = rotation.x, yaw = rotation.y;
    const int floor = reflectRotationOffWalls(pitch, yaw, result);
    rotation.x = pitch;
    rotation.y = yaw;
    return floor;
}

int bounceRotationOffWalls(ShortVec4& rotation, uint32_t result) {
    constexpr int kBend = 0x80; // the pitch bent away from a floor / ceiling it does not point into
    if ((result & kWallOnX) || (result & kWallOnZ)) {
        int16_t yaw = rotation.y;
        reflectYawOffWalls(yaw, result);
        rotation.y = yaw;
    }
    if (!(result & kWallOnY)) return 0;
    const int pitch = signedAngle(uint16_t(rotation.x));
    if (((result & kWallOnYPlus) && pitch > 0) || ((result & kWallOnYMinus) && pitch < 0))
        rotation.x = int16_t(uint32_t(-pitch) & kAngleMask);
    else
        rotation.x = wrap16((pitch > 0 ? -kBend : kBend) - pitch);
    return (result & kWallOnYPlus) ? 1 : 0;
}

uint32_t actorTestMove(const PlayerEnv& env, const Actor& actor) {
    const auto box = boxExtents(actor.collisionBox);
    const uint32_t mask = collisionLayerMask(actor.slot);
    return withCollisionState([&](CollisionState& st) {
        return testMove(env.room, st, wholeParts(actor.pos), wholeParts(actor.prevPos), extentsOrNone(box), mask);
    });
}

uint32_t actorPushOut(const PlayerEnv& env, Actor& actor) {
    return actorPushOutMask(env, actor, collisionLayerMask(actor.slot));
}

uint32_t actorPushOutMask(const PlayerEnv& env, Actor& actor, uint32_t mask) {
    const auto box = boxExtents(actor.collisionBox);
    std::array<int16_t, 3> resolved{};
    uint8_t airborne = 0;
    const uint32_t r = withCollisionState([&](CollisionState& st) {
        st.setBox(extentsOrNone(box)); // exe 0x413200
        const uint32_t result =
            resolveMove(env.room, st, wholeParts(actor.pos), wholeParts(actor.prevPos), actor.airborne, mask);
        for (int k = 0; k < 3; ++k) resolved[size_t(k)] = st.point[k];
        airborne = st.airborne;
        return result;
    });
    if (r == 0) return r;
    if (int32_t(r) < 1) { // gave up
        if (mask == kPlayerLayers) { // the player's layer (the exe reads the mover's +0x148: the player's skating byte)
            Player& p = asPlayer(actor);
            p.velocity.z = 0;
            p.pos.x = p.prevPos.x;
            p.pos.z = p.prevPos.z;
            p.velocity.x = 0;
            if (p.airborne != 0 && p.skating == 0) {
                p.airborne = 0;
                p.pos.y = p.prevPos.y;
                p.velocity.y = 0;
            }
        } else {
            actor.pos = actor.prevPos;
        }
    } else {
        if (r & (kWallOnXPlus | kWallOnXMinus)) actor.pos.x = int32_t(uint32_t(int32_t(resolved[0])) << 16);
        if (r & (kWallOnZPlus | kWallOnZMinus)) actor.pos.z = int32_t(uint32_t(int32_t(resolved[2])) << 16);
        if (r & (kWallOnYPlus | kWallOnYMinus)) actor.pos.y = int32_t(uint32_t(int32_t(resolved[1])) << 16);
        if (actor.airborne != kAirborneHanging && (r & kWallOnYPlus)) actor.airborne = airborne;
    }
    return r;
}

int actorGround(const PlayerEnv& env, const Actor& actor) {
    const uint32_t mask = collisionLayerMask(actor.slot);
    const auto box = boxExtents(actor.collisionBox);
    const int mode = int(actor.pos.y.whole) - int(actor.prevPos.y.whole);
    return withCollisionState([&](CollisionState& st) {
        return probeGround(env.room, st, actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole, extentsOrNone(box), mode,
                           mask);
    });
}

uint32_t actorWallQuery(const PlayerEnv& env, const Actor& actor, uint16_t* wallDirection) {
    const uint32_t mask = collisionLayerMask(actor.slot);
    const int offset = actorSlotClass(actor.slot) != kPlayerClass ? 0 : int(asPlayer(actor).directionOffset.y);
    const int yaw = int(actor.rotation.y) - offset;
    const auto box = boxExtents(actor.collisionBox);
    return withCollisionState([&](CollisionState& st) {
        const uint32_t r = wallQuery(env.room, st, wholeParts(actor.pos), extentsOrNone(box), yaw, mask);
        if (wallDirection) *wallDirection = st.wallDirection;
        return r;
    });
}

uint32_t testMoveBlocked(const PlayerEnv& env, const int16_t a[3], const int16_t b[3], const CollisionBoxRef& box,
                         uint32_t mask) {
    const auto extents = boxExtents(box);
    return withCollisionState([&](CollisionState& st) {
        return testMove(env.room, st, {a[0], a[1], a[2]}, {b[0], b[1], b[2]}, extentsOrNone(extents), mask);
    });
}

int probeGroundAt(const PlayerEnv& env, const int16_t p[3], const CollisionBoxRef& box, int mode, uint32_t mask) {
    const auto extents = boxExtents(box);
    return withCollisionState([&](CollisionState& st) {
        return probeGround(env.room, st, p[0], p[1], p[2], extentsOrNone(extents), mode, mask);
    });
}

namespace {

// the probes' offset ahead (the projection registers' single point; see actor_movement.hpp)
void offsetAhead(int angle, int distance, int32_t out[3]) {
    static constexpr int32_t kNoTranslation[3] = {0, 0, 0};
    loadProjectionMatrix(rotationY(int(uint32_t(angle) & kAngleMask)), 0); // exe 0x40bea0 on the identity, 0x40c3c0
    loadProjectionTranslation(kNoTranslation);                              // exe 0x40c3e0
    loadProjectionPoint(0, 0, int16_t(-distance), 0);                       // exe 0x40c6a0
    transformProjectionPoint();                                             // exe 0x40c800
    storeProjectionPoint(out);                                              // exe 0x40caa0
}

} // namespace

uint32_t probeMoveAhead(const PlayerEnv& env, const Actor& actor, const int16_t point[3], int angle, int distance) {
    const int16_t ahead[3] = {0, 0, wrap16(-distance)};
    int32_t d[3];
    transformVector(rotationY(int(uint32_t(angle) & kAngleMask)), ahead, d);
    const int16_t to[3] = {wrap16(d[0] + point[0]), wrap16(d[1] + point[1]), wrap16(d[2] + point[2])};
    return testMoveBlocked(env, to, point, actor.collisionBox, collisionLayerMask(actor.slot));
}

uint32_t probePushOutAhead(const PlayerEnv& env, const Actor& actor, int angle, int distance) {
    int32_t d[3];
    offsetAhead(angle, distance, d);
    const int16_t from[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const int16_t to[3] = {wrap16(d[0] + from[0]), wrap16(d[1] + from[1]), wrap16(d[2] + from[2])};
    return pointPushOut(env, to, from, actor.collisionBox, uint8_t(actor.airborne),
                        collisionLayerMask(actor.slot));
}

uint32_t pointPushOut(const PlayerEnv& env, const int16_t pos[3], const int16_t prev[3], const CollisionBoxRef& box,
                      uint8_t airborne,
                      uint32_t mask) {
    const auto extents = boxExtents(box);
    return withCollisionState([&](CollisionState& st) {
        st.setBox(extentsOrNone(extents)); // exe 0x413200
        return resolveMove(env.room, st, {pos[0], pos[1], pos[2]}, {prev[0], prev[1], prev[2]}, airborne, mask);
    });
}

} // namespace od::port
