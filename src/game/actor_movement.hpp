// The actors' movement through the room: the velocity integration in an actor's facing frame (exe 0x5e3690 /
// 0x5e38d0) and the room collision queries an actor or a point makes -- the test move, the push-out, the ground probe,
// the wall query -- over the room's collision grid (game/collision). Every class that moves calls these, the player
// too. The query state lives in the game state (0x6d00c0..0x6d013f, game/collision CollisionState): each query copies
// it in, runs, and copies it back.
#pragma once

#include <cstdint>
#include <utility>

#include "game/actor_state.hpp"
#include "game/collision.hpp"
#include "game/geometry_scratch.hpp"
#include "game/vectors.hpp"

namespace od::port {

struct Actor;

// The environment of the frame's actor updates: the room's collision grid (0x6d0084..0x6d0099 at runtime)
struct PlayerEnv {
    CollisionRoom room;
};

// A room query run on the game's collision state (the geometry scratch's, game/geometry_scratch)
template <class F> auto withCollisionState(F&& query) { return std::forward<F>(query)(queryCollisionState()); }

// The layer masks of the slot classes (exe 0x413240): the volumes of the room a mover of that class collides with
constexpr uint32_t kPlayerLayers = 0x1;
constexpr uint32_t kAnimatedLayers = 0x2;
constexpr uint32_t kShotLayers = 0x4;
constexpr uint32_t kProjectileLayers = 0x8;
constexpr uint32_t kStaticLayers = 0xc;
constexpr uint32_t kAllLayers = 0xff;
// exe 0x413240: the collision layer mask of an actor's slot id (by its slot class)
uint32_t collisionLayerMask(uint32_t slot);

// exe 0x5e38d0(out, yaw, x, y, z): the vector (x, y, z) << 12 turned by Ry(yaw), / 4096 (16.16 when v is integer); the
// exe does it on the FPU with exact integer products (|v| * 4096 stays far below 2^24), truncated by __ftol
void rotateByYaw(int yaw, int x, int y, int z, int32_t out[3]);
// exe 0x5e3690: the 16.16 position += the velocity (x, y, z) turned by the actor's yaw
void integrateVelocity(Actor& actor, int x, int y, int z);
// exe 0x5e36f0(pos, yaw, x, y, z): the 16.16 point += (x, y, z) turned by Ry(yaw) (exe 0x5e38d0)
void moveAlongYaw(Fixed16Vec3& pos, int yaw, int x, int y, int z);
// exe 0x5e3750(pos, yaw, x, y, z): the s16 point += the integer part of (x, y, z) turned by Ry(yaw)
void moveAlongYaw16(ShortVec3& pos, int yaw, int x, int y, int z);
// ... on an s16 point of values (the exe's stack point)
void moveAlongYaw16(int16_t pos[3], int yaw, int x, int y, int z);
// exe 0x5e3810(pos, rot, x, y, z): the 16.16 point += Ry Rx Rz(rotation) * (x, y, z) << 12 (exe 0x5e3970)
void moveAlongRotation(Fixed16Vec3& pos, const ShortVec4& rotation, int x, int y, int z);
// ... with the angles given as values (the exe's stack angles)
void moveAlongRotation(Fixed16Vec3& pos, const int16_t rotation[3], int x, int y, int z);
// exe 0x5e3870(pos, rot, x, y, z): the s16 point += the integer part of Ry Rx Rz(rotation) * (x, y, z) << 12
void moveAlongRotation16(int16_t pos[3], const int16_t rotation[3], int x, int y, int z);
// ... on an s16 point and an angle triple of the game state
void moveAlongRotation16(ShortVec3& pos, const ShortVec4& rotation, int x, int y, int z);
// exe 0x5e3ee0(from, to, speed, gravity, max): the vertical speed (per tick) that lands a hop from `from` at `to`
// (16.16 points) at the planar speed `speed` under `gravity`, the planar distance capped at maxDistance
int launchSpeed(const int32_t from[3], const int32_t to[3], int speed, int gravity, uint32_t maxDistance);
// ... between two 16.16 points of the game state
int launchSpeed(const Fixed16Vec3& from, const Fixed16Vec3& to, int speed, int gravity, uint32_t maxDistance);

// exe 0x5e3f50(from, to, pitch, gravity, maxDistance): the vertical / forward speeds (vy, vz) that lob a body from
// `from` to `to` (16.16 points) at the pitch under the gravity, the planar distance capped at maxDistance (unsigned);
// (0, 0) for a level pitch or a level lob (a zero cosine, which the exe divides by, is the port gap)
void lobVelocity(const int32_t from[3], const int32_t to[3], int pitch, int gravity, uint32_t maxDistance, int16_t& vy,
                 int16_t& vz);
// ... between two 16.16 points of the game state
void lobVelocity(const Fixed16Vec3& from, const Fixed16Vec3& to, int pitch, int gravity, uint32_t maxDistance,
                 int16_t& vy, int16_t& vz);
// exe 0x5e4040(speed, deceleration): the distance a speed covers while the deceleration stops it (the operands << 12,
// the product of the ticks and half the speed); 0x7fffffff without a deceleration
int32_t stoppingDistance(int32_t speed, int32_t deceleration);

// exe 0x410740(rot, result): the yaw reflected off the walls of a push-out result (a corner: +-a quarter turn - yaw),
// the pitch negated off a floor / ceiling; 1 = a floor
int reflectRotationOffWalls(int16_t& pitch, int16_t& yaw, uint32_t result);
// ... on an actor's rotation (pitch x, yaw y)
int reflectRotationOffWalls(ShortVec4& rotation, uint32_t result);
// exe 0x4107d0(rot, result): the yaw as exe 0x410740; the pitch negated when it points into the floor / ceiling,
// else bent away by 0x80; 1 = a floor
int bounceRotationOffWalls(ShortVec4& rotation, uint32_t result);

// exe 0x40f570: is the actor's position (integer parts) inside the floor / a volume, coming from
// its previous one, with its collision box and its layers (game/collision testMove's answer)
uint32_t actorTestMove(const PlayerEnv& env, const Actor& actor);
// exe 0x410960: the actor pushed out of the floor / the volumes of its layers (exe 0x410980 with its slot's mask)
uint32_t actorPushOut(const PlayerEnv& env, Actor& actor);
// exe 0x410980(actor, mask): the box push-out of the position from the previous one with an explicit layer mask; the
// resolved axes written back (gave up: back to the previous position), the airborne byte from the resolve
uint32_t actorPushOutMask(const PlayerEnv& env, Actor& actor, uint32_t mask);
// exe 0x40f190: the ground under the actor (its box, its layers; y - previous y < 0: only the surfaces above)
int actorGround(const PlayerEnv& env, const Actor& actor);
// exe 0x40fff0 -> 0x410040 (game/collision wallQuery): the nearest wall around the actor in its facing direction (the
// player's: the yaw minus its move direction's offset); the wall direction is left in the collision state (and handed
// out in wallDirection when given)
uint32_t actorWallQuery(const PlayerEnv& env, const Actor& actor, uint16_t* wallDirection = nullptr);

// The point queries take a collision box of the exe's data by its address (0: none).
// exe 0x40f5a0 on two points (integer parts)
uint32_t testMoveBlocked(const PlayerEnv& env, const int16_t a[3], const int16_t b[3], const CollisionBoxRef& box,
                         uint32_t mask);
// exe 0x413200(box) + 0x410a60(pos, prev, airborne, mask) on two points (integer parts): the push-out of a probe point
// (game/collision resolveMove; the resolved point in the collision state)
uint32_t pointPushOut(const PlayerEnv& env, const int16_t pos[3], const int16_t prev[3], const CollisionBoxRef& box,
                      uint8_t airborne,
                      uint32_t mask);
// exe 0x40f1c0(point, box, mode, mask) on a point (integer parts)
int probeGroundAt(const PlayerEnv& env, const int16_t p[3], const CollisionBoxRef& box, int mode, uint32_t mask);

// The probes ahead of an actor: the offset (0, 0, -distance) turned by Ry(angle & 0xfff) through the projection
// registers' single-point path (the stack matrix and a zero translation loaded, the point loaded with its spare lane 0 --
// the exe's uninitialised stack short --, transformed, stored), then a collision query with the actor's box and layers.
// exe 0x5e5670(actor, point, angle, distance): the box test-moved from the s16 point to the point ahead of it (< 0:
// blocked). As content/first_ruin/ruin_boss's transcription has it, the offset is turned without the projection
// registers (the same numbers; the exe leaves its single point in them)
uint32_t probeMoveAhead(const PlayerEnv& env, const Actor& actor, const int16_t point[3], int angle, int distance);
// exe 0x5e5760(actor, angle, distance): the box pushed out at the point ahead of the actor's position, from the
// position; the push-out's result
uint32_t probePushOutAhead(const PlayerEnv& env, const Actor& actor, int angle, int distance);


// ---- the same on a point of the geometry scratch's lanes (game/geometry_scratch)
inline void moveAlongYaw16(LaneVec3& pos, int yaw, int x, int y, int z) {
    ShortVec3 p = pos.vec3();
    moveAlongYaw16(p, yaw, x, y, z);
    pos = p;
}
inline void moveAlongYaw16(LaneVec4& pos, int yaw, int x, int y, int z) {
    ShortVec3 p = pos.vec3();
    moveAlongYaw16(p, yaw, x, y, z);
    pos = p;
}
inline int reflectRotationOffWalls(LaneVec4& rotation, uint32_t result) {
    ShortVec4 r = rotation.vec4();
    const int v = reflectRotationOffWalls(r, result);
    rotation.x = r.x;
    rotation.y = r.y;
    rotation.z = r.z;
    return v;
}

} // namespace od::port
