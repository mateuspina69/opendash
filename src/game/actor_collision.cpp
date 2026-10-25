// Actor collision pass, transcribed from dash2.exe (see actor_collision.hpp). Decompiler output was checked against
// the disassembly where control flow matters (exe 0x4140e0's restart after a hit). Vectors are the game's 16.16 ints
// (int32[3]); the collision query's point is the s16 integer part (wholePart) of them.
#include "game/actor_collision.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

#include "game/actor_animation.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/collision.hpp"
#include "game/fixed_point.hpp"
#include "game/geometry_scratch.hpp"
#include "game/matrix.hpp"
#include "game/actor_movement.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/vectors.hpp"

namespace od::port {

namespace {

constexpr const char* kActorCollisionPort = "actor collision"; // the area of its port-gap errors

constexpr int kStandReach = 0x30;          // a solid's top within this above the feet is stood on (y snapped)
constexpr int16_t kStepOffDrop = 0x200;    // the y speed a mover gets when it steps off a platform it stood on
constexpr int kMaxPushPasses = 9;          // the push-out gives up after this many passes
constexpr int kMaxHitsPerMover = 8;        // a mover is tested again from the first solid after at most 8 hits
constexpr uint32_t kNoMeshTop = 0x7fff;    // no volume under the mover

// |v| in wrapping 32-bit arithmetic
int32_t absWrapped(int32_t v) { return v < 0 ? int32_t(0u - uint32_t(v)) : v; }

CollisionQueues& queues() { return actorCollisionQueues(); }

// the 16.16 vector of a field into ints
void read3(const Fixed16Vec3& v, int32_t* out) {
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
}
// {-x, -y, -z} of an angle triple
void negate(const ShortVec4& a, int16_t* ang) {
    ang[0] = wrap16(-a.x);
    ang[1] = wrap16(-a.y);
    ang[2] = wrap16(-a.z);
}

// exe 0x415200: p = c + M * (p - c) (rotate p about c)
void rotateAbout(const Matrix3& r, int32_t* p, const int32_t* c) {
    const int32_t d[3] = {int32_t(uint32_t(p[0]) - uint32_t(c[0])), int32_t(uint32_t(p[1]) - uint32_t(c[1])),
                          int32_t(uint32_t(p[2]) - uint32_t(c[2]))};
    int32_t o[3];
    transformVectorFloat(r, d, o); // exe 0x40bbb0
    for (int i = 0; i < 3; ++i) p[i] = int32_t(uint32_t(c[i]) + uint32_t(o[i]));
}

// One run of the pass over the queues, on the collision queries' state (the geometry scratch's)
struct Pass {
    CollisionState& st = queryCollisionState();

    // the bone position of the pass (the exe's bone-position helper): n < 0 or a class other than the player / the
    // animated pool -> the actor's position and previous position; the player -> its bone point n; the animated pool
    // -> point n & 3 of its n-th bone block. prev = bone + (actor prev - actor pos)
    void bonePosition(const Actor& actor, int32_t* pos, int32_t* prev, int32_t n) const {
        if (n < 0) {
            read3(actor.pos, pos);
            read3(actor.prevPos, prev);
            return;
        }
        const uint32_t c = actorSlotClass(actor.slot);
        if (c == kPlayerClass) {
            const ShortVec3& p = asPlayer(actor).bonePoints[size_t(n)];
            pos[0] = int32_t(uint32_t(int32_t(p.x)) << 16);
            pos[1] = int32_t(uint32_t(int32_t(p.y)) << 16);
            pos[2] = int32_t(uint32_t(int32_t(p.z)) << 16);
        } else if (c != kAnimatedClass) {
            read3(actor.pos, pos);
            read3(actor.prevPos, prev);
            return;
        } else {
            // the exe passes the bone index itself (not bone / 4) as the block number to exe 0x5e1780
            const BoneBlock* block = boneBlockOf(asAnimated(actor), n);
            if (!block) notPorted(kActorCollisionPort, "exe 0x5e1780 returned null (bone index past the mask)");
            const ShortVec4& p = block->points[uint32_t(n) & 3]; // n >= 0 here
            pos[0] = int32_t(uint32_t(int32_t(p.x)) << 16);
            pos[1] = int32_t(uint32_t(int32_t(p.y)) << 16);
            pos[2] = int32_t(uint32_t(int32_t(p.z)) << 16);
        }
        const int32_t cur[3] = {actor.pos.x, actor.pos.y, actor.pos.z};
        const int32_t was[3] = {actor.prevPos.x, actor.prevPos.y, actor.prevPos.z};
        for (int i = 0; i < 3; ++i) prev[i] = int32_t(uint32_t(was[i]) - uint32_t(cur[i]) + uint32_t(pos[i]));
    }

    // exe 0x413200: the mover's collision box (6 x s16 at *(mover +0x58)) into the query's actor box
    void setBox(const Actor& mover) {
        int16_t box[6] = {};
        if (mover.collisionBox != nullptr) {
            const BoxAccess b = *mover.collisionBox;
            box[kBoxMinX] = b.minX;
            box[kBoxMaxX] = b.maxX;
            box[kBoxMinY] = b.minY;
            box[kBoxMaxY] = b.maxY;
            box[kBoxMinZ] = b.minZ;
            box[kBoxMaxZ] = b.maxZ;
        }
        st.setBox(box);
    }

    // the solid's mesh (exe 0x414bd0 / 0x414790): the animated pool's or the statics', else none
    static uint32_t meshOf(const Actor& solid) {
        const uint32_t c = actorSlotClass(solid.slot);
        if (c == kAnimatedClass) return asAnimated(solid).collisionMesh.value();
        if (c == kStaticClass) return asStatic(solid).collisionMesh.value();
        return 0;
    }
    // the solid's mesh record: its count, then that many volumes (only those: a mesh can end where its data ends)
    static CollisionMesh meshRecord(uint32_t mesh) {
        const uint64_t bytes = 4 + uint64_t(dataScalar<uint32_t>(mesh)) * CollisionMesh::kVolumeBytes;
        return dataRecord<CollisionMesh>(mesh, uint32_t(std::min<uint64_t>(bytes, UINT32_MAX)));
    }
    // the volume's rect {x0, x1, ., ., z0, z1} relative to the solid position p
    static void rect(const MeshVolume& v, const int32_t* p, int16_t* r) {
        r[0] = wrap16(wholePart(p[0]) + v.x0);
        r[1] = wrap16(v.x1 + wholePart(p[0]));
        r[4] = wrap16(wholePart(p[2]) + v.z0);
        r[5] = wrap16(v.z1 + wholePart(p[2]));
    }
    // the volume's heights {., ., y0, y1} relative to the solid position p
    static void heights(const MeshVolume& v, const int32_t* p, int16_t* r) {
        r[2] = wrap16(wholePart(p[1]) + v.y0);
        r[3] = wrap16(v.y1 + wholePart(p[1]));
    }
    // the volume's push-out shape (the trigger shapes >= 0x100 are not the actor pass's)
    static uint32_t shapeOf(const MeshVolume& v) {
        const uint32_t shape = v.shape;
        if (shape >= 0x100) notPorted(kActorCollisionPort, "actor mesh volume with shape >= 0x100");
        return shape;
    }

    // exe 0x414bd0(mover, solid, solid pos, axisAligned, layer mask): the top of the solid's mesh under the mover (in
    // the solid's frame unless axisAligned). Returns (surface y << 16) | 1 when it is at most kStandReach above the
    // mover's feet (y - box[3]), else 0.
    uint32_t floorOn(const Actor& mover, const Actor& solid, const int32_t* pos, int axisAligned, uint32_t lmask) {
        st.noStepUp = 1;
        st.lastPushAxis = 0;
        int top = int(kNoMeshTop);
        int32_t loc[3];
        int16_t ang[3] = {};
        if (axisAligned == 0) {
            read3(mover.pos, loc);
            negate(solid.rotation, ang);
            rotateAbout(rotationZXY(ang[0], ang[1], ang[2]), loc, pos);
            for (int i = 0; i < 3; ++i) st.point[i] = wholePart(loc[i]);
        } else {
            st.point[0] = mover.pos.x.whole;
            st.point[1] = mover.pos.y.whole;
            st.point[2] = mover.pos.z.whole;
        }
        st.previous[0] = mover.prevPos.x.whole;
        st.previous[1] = mover.prevPos.y.whole;
        st.previous[2] = mover.prevPos.z.whole;
        const uint32_t mh = meshOf(solid);
        if (!mh) return 0;
        const CollisionMesh mesh = meshRecord(mh);
        uint32_t count = mesh.count;
        if (count == 0) notPorted(kActorCollisionPort, "empty actor collision mesh (the exe would loop 2^32 times)");
        for (size_t i = 0; count; --count, ++i) {
            const MeshVolume v = mesh.volume(i);
            if (!(lmask & v.layerMask)) continue;
            const uint32_t shape = shapeOf(v);
            int16_t r[6] = {};
            rect(v, pos, r);
            if (volumeShapeTest(st, r, int(shape)) == 0) continue;
            heights(v, pos, r);
            volumeAdjustSlope(st, r, uint8_t(shape));
            if (r[2] < top && st.point[1] < r[3]) top = st.box[kBoxMaxY] + r[2];
        }
        if (top == int(kNoMeshTop) || (top - mover.collisionBox->maxY) - kStandReach > st.point[1]) return 0;
        if (axisAligned != 0) return (uint32_t(top) << 16) | 1;
        for (int i = 0; i < 3; ++i) ang[i] = wrap16(-ang[i]);
        loc[1] = int32_t(uint32_t(top) << 16);
        rotateAbout(rotationYXZ(ang[0], ang[1], ang[2]), loc, pos);
        return (uint32_t(int32_t(wholePart(loc[1]))) << 16) | 1;
    }

    // exe 0x415070(solid, pos, prev, cur, mprev): the mover's previous position follows the solid's move (pos - prev)
    // and, when the solid turned since the last tick (+0x28.. vs +0x30..), its rotation about pos.
    void follow(const Actor& solid, const int32_t* pos, const int32_t* prev, const int32_t* cur, int32_t* mprev) {
        for (int i = 0; i < 3; ++i) mprev[i] = int32_t(uint32_t(mprev[i]) + (uint32_t(pos[i]) - uint32_t(prev[i])));
        const uint16_t r0 = uint16_t(solid.rotation.x), r1 = uint16_t(solid.rotation.y),
                       r2 = uint16_t(solid.rotation.z);
        if (((r0 | r1) & kAngleMask) == 0) return;
        const int16_t a[3] = {int16_t(r0 & kAngleMask), int16_t(r1 & kAngleMask), int16_t(r2 & kAngleMask)};
        const int16_t b[3] = {int16_t(uint16_t(-solid.prevRotation.x) & kAngleMask),
                              int16_t(uint16_t(-solid.prevRotation.y) & kAngleMask),
                              int16_t(uint16_t(-solid.prevRotation.z) & kAngleMask)};
        if (b[0] == int16_t(uint16_t(-(r0 & kAngleMask)) & kAngleMask) &&
            b[1] == int16_t(uint16_t(-(r1 & kAngleMask)) & kAngleMask) &&
            b[2] == int16_t(uint16_t(-(r2 & kAngleMask)) & kAngleMask))
            return;
        int32_t t[3] = {cur[0], cur[1], cur[2]};
        rotateAbout(rotationZXY(b[0], b[1], b[2]), t, pos);
        rotateAbout(rotationYXZ(a[0], a[1], a[2]), t, pos);
        for (int i = 0; i < 3; ++i) mprev[i] = int32_t(uint32_t(mprev[i]) + (uint32_t(t[i]) - uint32_t(cur[i])));
    }

    // exe 0x414790(solid, pos, cur, mprev, axisAligned, layer mask): push the mover's position (cur) out of the
    // solid's mesh volumes (exe 0x410eb0, <= kMaxPushPasses passes), in the solid's frame unless axisAligned. 0 =
    // untouched, kPushGaveUp, else the dominant push axis | the kPushed* axes.
    uint32_t pushOut(const Actor& solid, const int32_t* pos, int32_t* cur, int32_t* mprev, int axisAligned,
                     uint32_t lmask) {
        int hit = 0, iter = 0;
        st.noStepUp = 1;
        st.lastPushAxis = 0;
        int16_t ang[3] = {};
        if (axisAligned == 0) {
            negate(solid.rotation, ang);
            const Matrix3 r = rotationZXY(ang[0], ang[1], ang[2]);
            rotateAbout(r, mprev, pos);
            rotateAbout(r, cur, pos);
        }
        for (int i = 0; i < 3; ++i) st.point[i] = wholePart(cur[i]);
        for (int i = 0; i < 3; ++i) st.previous[i] = wholePart(mprev[i]);
        st.clearPush();
        const uint32_t mh = meshOf(solid);
        if (!mh) return 0;
        const CollisionMesh mesh = meshRecord(mh);
        for (;;) {
            st.delta[0] = wrap16(st.point[0] - st.previous[0]);
            uint32_t count = mesh.count;
            if (count == 0)
                notPorted(kActorCollisionPort, "empty actor collision mesh (the exe would loop 2^32 times)");
            st.delta[1] = wrap16(st.point[1] - st.previous[1]);
            st.delta[2] = wrap16(st.point[2] - st.previous[2]);
            bool moved = false;
            for (size_t i = 0; count; --count, ++i) {
                const MeshVolume v = mesh.volume(i);
                if (!(v.layerMask & lmask)) continue; // hit keeps its value (it can be the previous pass's)
                const uint32_t shape = shapeOf(v);
                int16_t r[6] = {};
                rect(v, pos, r);
                hit = volumeShapeTest(st, r, int(shape));
                if (hit == 0) continue;
                heights(v, pos, r);
                hit = int(volumeResolve(st, r, shape, hit));
                if (hit != 0) {
                    moved = true;
                    break;
                }
            }
            if (!moved && hit == 0) break;
            // the exe's 0x4149ae: apply the accumulated push and go again
            st.applyPush();
            const bool giveUp = kMaxPushPasses - 2 < iter;
            ++iter;
            if (giveUp) return kPushGaveUp;
        }
        const int px = st.point[0], pz = st.point[2];
        const int ox = wholePart(cur[0]), oz = wholePart(cur[2]);
        const int dy = wholePart(cur[1]) - st.point[1];
        if (oz == pz && dy == 0 && ox == px) return 0;
        if (ox != px) cur[0] = int32_t(uint32_t(px + (0 < ox - px ? -1 : 1)) << 16);
        if (oz != pz) cur[2] = int32_t(uint32_t(pz + (0 < oz - pz ? -1 : 1)) << 16);
        if (dy != 0) cur[1] = int32_t(uint32_t(st.point[1] + (0 < dy ? -1 : 1)) << 16);
        if (axisAligned == 0) {
            for (int i = 0; i < 3; ++i) ang[i] = wrap16(-ang[i]);
            rotateAbout(rotationYXZ(ang[0], ang[1], ang[2]), cur, pos);
        }
        const int dx = wholePart(cur[0]) - px, dz = wholePart(cur[2]) - pz;
        uint32_t u;
        if (absWrapped(dz) < absWrapped(dx)) {
            if (absWrapped(dy) < absWrapped(dx)) u = dx < 1 ? kMainSideXMinus : kMainSideXPlus;
            else u = dy < 1 ? kMainSideYMinus : kMainSideYPlus;
        } else {
            if (absWrapped(dy) < absWrapped(dz)) u = dz < 1 ? kMainSideZMinus : kMainSideZPlus;
            else u = dy < 1 ? kMainSideYMinus : kMainSideYPlus;
        }
        if (dx != 0) u |= dx < 1 ? kSolidOnXMinus : kSolidOnXPlus;
        if (dz != 0) u |= dz < 1 ? kSolidOnZMinus : kSolidOnZPlus;
        if (dy != 0) u |= 0 < dy ? kSolidOnYPlus : kSolidOnYMinus;
        return u;
    }

    // exe 0x414690(solid, mover, bone, axisAligned): the push-out solids (types 0 / 2)
    uint32_t solidPush(const Actor& solid, Actor& mover, int32_t bone, int axisAligned) {
        int32_t pos[3], prev[3];
        bonePosition(solid, pos, prev, bone);
        setBox(mover);
        const uint32_t lmask = collisionLayerMask(mover.slot);
        if (floorOn(mover, solid, pos, axisAligned, lmask) == 0) return 0;
        int32_t cur[3], mprev[3];
        read3(mover.pos, cur);
        read3(mover.prevPos, mprev);
        st.airborne = mover.airborne;
        st.landed = mover.onPlatform;
        follow(solid, pos, prev, cur, mprev);
        const uint32_t r = pushOut(solid, pos, cur, mprev, axisAligned, lmask);
        if (r == 0) return 0;
        mover.pos.x = cur[0];
        mover.pos.z = cur[2];
        mover.pos.y = cur[1];
        return r;
    }

    // |b - a| > |a| + 0x80000 for a mover delta a that is 0 or opposite to the solid's delta b (exe 0x4141e0)
    static bool outruns(uint32_t a, uint32_t b) {
        int32_t d = int32_t(b - a);
        if (d < 0) d = int32_t(a - b);
        uint32_t aa = a;
        if (int32_t(aa) < 0) aa = 0u - aa;
        return int32_t(aa + 0x80000) < d;
    }

    // exe 0x4141e0(solid, mover, platform, bone, param, axisAligned): the stand-on solids (types 1 / 3) -- the
    // push-out, standing on the mesh (mover +0x52 = 1, +0x54 = platform, y snapped when the top is within
    // +-kStandReach), stepping off (the velocity inherits the platform's move), and the "hit by a moving solid"
    // record of the player (its hit box = param and the solid's position).
    uint32_t solidStand(const Actor& solid, Actor& mover, Actor& platform, int32_t bone, uint32_t param,
                        int axisAligned) {
        const uint8_t air = mover.airborne;
        int32_t pos[3], prev[3];
        bonePosition(solid, pos, prev, bone);
        setBox(mover);
        const uint32_t lmask = collisionLayerMask(mover.slot);
        int32_t d[3] = {int32_t(uint32_t(pos[0]) - uint32_t(prev[0])), int32_t(uint32_t(pos[1]) - uint32_t(prev[1])),
                        int32_t(uint32_t(pos[2]) - uint32_t(prev[2]))};
        if (floorOn(mover, solid, pos, axisAligned, lmask) == 0) {
            if (mover.onPlatform == 0 || mover.platform != &platform) return 0;
            const bool isPlayer = mover.slot == kPlayerClass;
            bool velocity = false;
            Matrix3 r{};
            int16_t ang[3];
            if (axisAligned == 0) {
                negate(mover.rotation, ang);
                if (isPlayer) {
                    const ShortVec3& offset = asPlayer(mover).directionOffset;
                    ang[0] = wrap16(ang[0] + offset.x);
                    ang[1] = wrap16(ang[1] + offset.y);
                    ang[2] = wrap16(ang[2] + offset.z);
                }
                r = rotationYXZ(ang[0], ang[1], ang[2]);
                velocity = isPlayer;
            } else if (isPlayer) {
                const ShortVec3& offset = asPlayer(mover).directionOffset;
                r = rotationYXZ(offset.x, offset.y, offset.z);
                velocity = true;
            }
            if (axisAligned == 0 || isPlayer) {
                const int32_t zero[3] = {0, 0, 0};
                rotateAbout(r, d, zero);
            }
            if (velocity) { // the player: the step-off velocity +0xd8..
                mover.platform = nullptr;
                auto&& push = asPlayer(mover).pushVelocity;
                push.x = int16_t(d[0] >> 12);
                push.z = int16_t(d[2] >> 12);
                push.y = int16_t(d[1] >> 12);
                mover.onPlatform = 0;
                return kSteppedOff;
            }
            // the exe's 0x4143b9
            mover.velocity.z = wrap16(mover.velocity.z + wrap16(d[2] >> 12));
            mover.velocity.x = wrap16(mover.velocity.x + wrap16(d[0] >> 12));
            const int16_t sy = wrap16(d[1] >> 12);
            mover.platform = nullptr;
            if (mover.airborne == 0) {
                mover.onPlatform = 0;
                mover.velocity.y = wrap16(mover.velocity.y + sy - kStepOffDrop);
                return kSteppedOff;
            }
            mover.velocity.y = wrap16(mover.velocity.y + sy);
            mover.onPlatform = 0;
            return kSteppedOff;
        }
        int32_t cur[3], mprev[3];
        read3(mover.pos, cur);
        read3(mover.prevPos, mprev);
        const uint32_t mdx = uint32_t(cur[0]) - uint32_t(mprev[0]);
        const uint32_t mdy = uint32_t(cur[1]) - uint32_t(mprev[1]);
        const uint32_t mdz = uint32_t(cur[2]) - uint32_t(mprev[2]);
        st.airborne = mover.airborne;
        st.landed = mover.onPlatform;
        follow(solid, pos, prev, cur, mprev);
        uint32_t r = pushOut(solid, pos, cur, mprev, axisAligned, lmask);
        if (int32_t(r) < 1) {
            r = 0;
        } else {
            // the exe's 0x414545: on some axis the mover stands still or moves against the solid, and the solid's move
            // exceeds the mover's by more than 8 units (x first; y and z only if x does not decide)
            auto against = [](uint32_t a, uint32_t b) { return a == 0 || int32_t(a ^ b) < 0; };
            const bool pushed = (against(mdx, uint32_t(d[0])) && outruns(mdx, uint32_t(d[0]))) ||
                                (against(mdy, uint32_t(d[1])) && outruns(mdy, uint32_t(d[1]))) ||
                                (against(mdz, uint32_t(d[2])) && outruns(mdz, uint32_t(d[2])));
            if (pushed && mover.slot == kPlayerClass && st.landed == 0) {
                auto&& hit = asPlayer(mover).solidHit;
                hit.result = param;
                hit.contact.x = solid.pos.x.whole;
                hit.contact.y = solid.pos.y.whole;
                hit.contact.z = solid.pos.z.whole;
                hit.contactKind = 0;
            }
            if (st.airborne == 0 && st.landed != 0) { // exe 0x410eb0 landed the mover on the mesh
                mover.airborne = 0;
                mover.platform = &platform;
                mover.onPlatform = st.landed;
            }
            mover.pos.x = cur[0];
            mover.pos.z = cur[2];
        }
        // the exe's 0x4145dd
        const uint32_t f = floorOn(mover, solid, pos, axisAligned, lmask);
        if ((f & 0xffff) == 0) {
            mover.pos.y = cur[1];
            if (mover.onPlatform != 0) {
                mover.onPlatform = 0;
                mover.airborne = air;
                mover.platform = nullptr;
            }
            return r;
        }
        const int32_t y = int32_t((f & 0xffff0000u) - uint32_t(int32_t(mover.collisionBox->maxY)) * 0x10000u);
        const int32_t my = mover.pos.y;
        const uint32_t reach = uint32_t(kStandReach) << 16;
        if (int32_t(uint32_t(y) - reach) <= my && my <= int32_t(uint32_t(y) + reach) && mover.airborne == 0) {
            mover.pos.y = y;
            mover.onPlatform = 1;
            mover.platform = &platform;
            return r;
        }
        mover.pos.y = cur[1];
        return r;
    }

    // exe 0x4140e0's loop: every mover against every solid of the queues, then both queues emptied
    void run() {
        CollisionQueues& q = queues();
        uint32_t moverAt = 0;
        if (q.moverCount == 0) {
            q.moverCount = 0;
            q.solidCount = 0;
            return;
        }
        int hits = 0;
        for (;;) {
            bool restart = false;
            for (uint32_t k = 0; k != q.solidCount; ++k) {
                const SolidEntry& entry = q.solids[k];
                const ActorRef<ActorHeader>& moverEntry = q.movers[moverAt];
                if (entry.actor.get() == moverEntry.get()) continue;
                // the Actor layout of whatever the queues hold (any slot that submitted itself)
                auto&& solid = actorLayoutOf(*entry.actor);
                auto&& mover = actorLayoutOf(*moverEntry);
                const int32_t bone = entry.bone;
                uint32_t r;
                switch (SolidType(entry.type)) {
                case SolidType::PushOut: r = solidPush(solid, mover, bone, 0); break;
                case SolidType::StandOn: r = solidStand(solid, mover, solid, bone, entry.param, 0); break;
                case SolidType::PushOutAligned: r = solidPush(solid, mover, bone, 1); break;
                case SolidType::StandOnAligned: r = solidStand(solid, mover, solid, bone, entry.param, 1); break;
                default: continue;
                }
                if (r == 0) continue;
                mover.actorHit = r;
                solid.lastMover = &mover;
                if (++hits <= kMaxHitsPerMover) restart = true; // the same mover again from the first solid
                break;
            }
            if (!restart) {
                hits = 0;
                ++moverAt;
            }
            if (moverAt == q.moverCount) break;
        }
        q.solidCount = 0;
        q.moverCount = 0;
    }
};

} // namespace

CollisionQueues& actorCollisionQueues() {
    static CollisionQueues q;
    return q;
}

void actorCollisionReset() {
    queues().solidCount = 0;
    queues().moverCount = 0;
}

void actorCollisionSubmitSolid(ActorHeader& submitter, SolidType type, int16_t bone, uint32_t param) {
    auto&& actor = actorLayoutOf(submitter); // the exe writes +0x6c whatever the pool
    if (actor.flags & kActorQueued) {
        CollisionQueues& q = queues();
        if (q.solidCount == CollisionQueues::kSolids)
            throw ActorLayoutError("actor collision: a solid past the queue's 64 (the exe writes over the movers')");
        q.solids[q.solidCount++] = {&submitter, int16_t(type), bone, param};
    }
    actor.lastMover = nullptr;
}

void actorCollisionSubmitMover(ActorHeader& submitter) {
    auto&& actor = actorLayoutOf(submitter); // the exe writes +0x68 whatever the pool
    if (actor.flags & kActorQueued) {
        CollisionQueues& q = queues();
        if (q.moverCount == CollisionQueues::kMovers) throw ActorLayoutError("actor collision: the mover queue full");
        q.movers[q.moverCount++] = &submitter;
    }
    actor.actorHit = 0;
}

void actorCollision() {
    Pass p;
    p.run();
}

void actorCarry(Actor& actor, const Actor& platform) {
    if (platform.slot >= kEffectClass) return; // the effect / controller pools carry nothing
    int32_t t[3], pp[3];
    read3(actor.pos, t);
    read3(platform.prevPos, pp);
    const int32_t nx = platform.pos.x, ny = platform.pos.y, nz = platform.pos.z;
    if ((uint16_t(platform.rotation.x) & kAngleMask) != 0 || (uint16_t(platform.rotation.y) & kAngleMask) != 0) {
        int16_t ang[3];
        negate(platform.prevRotation, ang);
        rotateAbout(rotationZXY(ang[0], ang[1], ang[2]), t, pp);
        rotateAbout(rotationYXZ(platform.rotation.x, platform.rotation.y, platform.rotation.z), t, pp);
    }
    actor.pos.x = int32_t(uint32_t(nx) - uint32_t(pp[0]) + uint32_t(t[0]));
    actor.pos.y = int32_t(uint32_t(ny) - uint32_t(pp[1]) + uint32_t(t[1]));
    actor.pos.z = int32_t(uint32_t(nz) - uint32_t(pp[2]) + uint32_t(t[2]));
    actor.rotation.y = wrap16(actor.rotation.y + (platform.rotation.y - platform.prevRotation.y));
}

int reflectOffActorHit(int16_t& pitch, int16_t& yaw, uint32_t r) {
    const uint32_t side = r & kMainSideMask;
    const uint32_t y = uint16_t(yaw) & kAngleMask;
    if (r & kSolidAboveOrBelow) {
        pitch = int16_t((0u - uint16_t(pitch)) & kAngleMask);
        return (r & kSolidOnYPlus) ? 1 : 0;
    }
    if (side == kMainSideXPlus || side == kMainSideXMinus) {
        if (side == kMainSideXPlus ? y >= uint32_t(kHalfTurn) : y <= uint32_t(kHalfTurn)) return 0;
        yaw = int16_t((0u - uint16_t(yaw)) & kAngleMask);
        return 0;
    }
    if (side == kMainSideZPlus) {
        if (y >= uint32_t(kQuarterTurn) && y <= uint32_t(3 * kQuarterTurn)) return 0;
    } else if (side == kMainSideZMinus) {
        if (y <= uint32_t(kQuarterTurn) || y >= uint32_t(3 * kQuarterTurn)) return 0;
    } else {
        return 0;
    }
    yaw = int16_t((uint32_t(kHalfTurn) - uint16_t(yaw)) & kAngleMask); // the exe: (0xf800 - yaw) & 0xfff
    return 0;
}

} // namespace od::port
