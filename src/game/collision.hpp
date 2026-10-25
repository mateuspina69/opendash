// Room collision: the queries a point or a mover makes against the room's collision data -- the ground probe, the
// move test, the push-out of a moved point, the wall query, the trigger scan -- bit-exact ports of dash2.exe (golden:
//   probeGround  exe 0x40f1c0: the lowest surface under a point
//   testMove     exe 0x40f5a0: is point A inside the floor / a volume
//   resolveMove  exe 0x410a60: push a moved point out of the floor / volumes (at most 9 passes)
//   wallQuery    exe 0x410040: the nearest wall face around a point in a direction
//   triggerScan  exe 0x413c80: the trigger volume a point is in
// over the cells gathered around the point (exe 0x413000), the floor heightmap (exe 0x40f450), the volumes of the
// cells' scenery segments (exe 0x600df0), the shape tests of the exe table 0x889cc8, the slopes (exe 0x40f8d0) and the
// push-out helpers (exe 0x410eb0, 0x412360..0x412fe0).
// Coordinates: game units, y down, the integer parts of the 16.16 positions (what the game reads).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "engine/file_data.hpp"
#include "engine/room.hpp"
#include "game/room_records.hpp"

namespace od {

// ---- the room block's collision records (game/room_records), as the queries read them: from the block's bytes ------

using GridCellRecord = GridCellT<BlockStorage>;
using SegmentRecord = SegmentEntryT<BlockStorage>;
using SegmentPoolRecord = SegmentPoolEntryT<BlockStorage>;
using VolumeSetRecord = VolumeSetT<BlockStorage>;
using VolumeRecord = VolumeT<BlockStorage>;

constexpr uint32_t kShapeTrigger = 0x100;   // shapes from here on are triggers (shape >> 8 = the trigger type)
constexpr int kShapeTestShift = 4;          // (shape >> 4) & 0xf: the point-in-shape test of the table 0x889cc8
constexpr uint8_t kShapeScriptPassable = 0x1c; // a box the movers pass through while a script runs (see the gate)

// The active grid (exe 0x6d0084 pool, 0x6d0088 cells, 0x6d008c segment list, 0x6d0094 / 95 origin, 0x6d0098 / 99
// size): offsets are bytes inside the room block
// block: the bytes holding the room block from `origin` on (a block shared with the loaded files -- FileDataBlock::share
// -- follows the writes of the transition scripts there, the segment switches)
struct CollisionRoom {
    FileDataBlock block;
    size_t origin = 0;
    const uint8_t* bytes() const { return block.data() + origin; }
    size_t pool = 0, cells = 0, segments = 0;
    int x0 = 0, z0 = 0, w = 0, h = 0;
    // the grid's placement (game/room_records)
    GridBounds bounds() const { return {x0, z0, w, h}; }
};
CollisionRoom collisionRoom(FileDataBlock block, const RoomGrid& grid, size_t origin = 0);

// ---- the query state -----------------------------------------------------------------------------------------------

// The exe keeps the query state in globals (0x6d00c0..0x6d013f) between calls; CollisionState is their image byte for
// byte (the layout the static_asserts in collision.cpp pin), so the 8 / 16 / 32-bit stores of the exe land on the same
// bytes. Axis lanes: [0] x, [1] y, [2] z. The game copies the globals in before a query and back after it
// (loadCollisionState / storeCollisionState); the host's offline queries start from a cleared state (every byte 0).
// The exe uses 0x6d00c0.. as a scratch area of the transforms too (game/projection_state ProjectionScratch,
// game/camera_state CameraTransform viewScratch): a query starts from whatever the last transform or query left there.
struct CollisionState {
    int16_t point[3]{};        // +0x00: the point being resolved / probed
    uint8_t airborne{};        // +0x06: the mover's airborne byte, in and out
    uint8_t landed{};          // +0x07: the mover's standing-on-something byte
    int16_t previous[3]{};     // +0x08: the previous point / the move's start
    int16_t reserved0E{};      // +0x0e: not used by the queries
    int16_t delta[3]{};        // +0x10: point - previous
    uint16_t wallDirection{};  // +0x16: the wall direction wallQuery leaves
    int16_t hitX0{};           // +0x18: the hit rect the shape tests leave (shrunk by the mover's box)
    int16_t hitTop{};          // +0x1a
    int16_t hitZ0{};           // +0x1c
    uint8_t noStepUp{};        // +0x1e: 1 = no step-up tolerance on the y push (a solid's mesh), 0 = kStepUp
    uint8_t lastPushAxis{};    // +0x1f: kPushed* of the last push
    int16_t hitX1{};           // +0x20
    int16_t hitBottom{};       // +0x22
    int16_t hitZ1{};           // +0x24
    int16_t hitShape{};        // +0x26: the shape word of the volume testMove hit (kHitShapeFloor: the floor)
    int16_t pendingPush[3]{};  // +0x28: the push of this pass (applied at its end)
    int16_t appliedPushZ{};    // +0x2e: the push the last pass applied
    int16_t appliedPushX{};    // +0x30
    int16_t appliedPushY{};    // +0x32
    uint32_t gatheredCells[4]{}; // +0x34: the exe's gathered cell pointers (exe addresses: not mirrored by the port)
    int16_t box[6]{};          // +0x44: the mover's box {minX, maxX, minY, maxY, minZ, maxZ} (exe 0x413200)
    uint16_t gatherOffsets[4][2]{}; // +0x50: the 2x2 cell offsets the gather took (x, z)
    uint32_t specialCells[4]{}; // +0x60: per gathered kCellSpecial cell, its packed origin (lo16 x, hi16 z)
    int16_t userLanes[8]{};     // +0x70: not used by the queries (the other users' lanes: the lock-on's points)
    // not part of the image: the shape-0x1c gate of the running game (a script runs and flag port::kFlagScriptSolids (game/story_flags) is
    // clear: kShapeScriptPassable volumes are ignored)
    bool scriptPassable = false;

    // the mover's box (nullptr: none, a zero box)
    void setBox(const int16_t* b) {
        for (int i = 0; i < 6; ++i) box[i] = b ? b[i] : int16_t(0);
    }
    // the push accumulators cleared (pending and applied)
    void clearPush() {
        pendingPush[0] = 0;
        pendingPush[1] = 0;
        pendingPush[2] = 0;
        appliedPushX = 0;
        appliedPushY = 0;
        appliedPushZ = 0;
    }
    // one pass's end: the point moves by the applied push, the pending push becomes the applied one
    void applyPush();
};
// the indices of the box's lanes
constexpr int kBoxMinX = 0, kBoxMaxX = 1, kBoxMinY = 2, kBoxMaxY = 3, kBoxMinZ = 4, kBoxMaxZ = 5;
// lastPushAxis: the side the last push moved the point to
constexpr uint8_t kPushedXPlus = 0x01, kPushedXMinus = 0x02, kPushedZPlus = 0x04, kPushedZMinus = 0x08;
constexpr uint8_t kPushedYPlus = 0x10, kPushedYMinus = 0x20;
constexpr uint8_t kPushedX = kPushedXPlus | kPushedXMinus, kPushedZ = kPushedZPlus | kPushedZMinus,
                  kPushedY = kPushedYPlus | kPushedYMinus;
// hitShape: testMove stopped by the floor
constexpr int16_t kHitShapeFloor = 0xff;


// ---- the queries ---------------------------------------------------------------------------------------------------

// probeGround's answers
constexpr int32_t kNoSurface = 0x7fff;       // no surface under the point
constexpr int32_t kInsideSpecial = -0x4000;  // the point is inside a kCellSpecial cell
// mode < 0: only surfaces above the point; mask: the volume layer mask (& kSpecialLayers enables the kCellSpecial
// cells). Returns the surface y, kNoSurface or kInsideSpecial.
int32_t probeGround(const CollisionRoom& room, CollisionState& st, int16_t x, int16_t y, int16_t z,
                    const int16_t* box, int mode, uint32_t mask);
constexpr uint32_t kSpecialLayers = 3; // the layers the kCellSpecial cells stop
// testMove's answers: 0 = free, kMoveOutsideMap, kMoveNoCell, kMoveBlocked | the cell word (inside the floor / a
// special cell), kMoveBlocked | (layer & kVolumeLayerHigh) << 12 | the cell word (inside a volume)
constexpr uint32_t kMoveBlocked = 0x80000000u;
constexpr uint32_t kMoveOutsideMap = 0x40000000u;
constexpr uint32_t kMoveNoCell = 0x20000000u;
constexpr uint32_t kVolumeLayerHigh = 0x10; // the volume's mask bit testMove reports (<< 12)
uint32_t testMove(const CollisionRoom& room, CollisionState& st, const std::array<int16_t, 3>& a,
                  const std::array<int16_t, 3>& b, const int16_t* box, uint32_t mask);
// exe 0x410a60(pos, prev, airborne, mask) with the box already in st (exe 0x410980 sets it): pushes st's point (from
// `pos`) out of the floor and the volumes. 0 = untouched, kResolveGaveUp = gave up after 9 passes, else the
// dominant push axis (kPushAxis*) | the kWallOn* sides; the resolved point is in st.point and the airborne byte in
// st.airborne.
uint32_t resolveMove(const CollisionRoom& room, CollisionState& st, const std::array<int16_t, 3>& pos,
                     const std::array<int16_t, 3>& prev, uint8_t airborne, uint32_t mask);
// resolveMove's push bits: the side of the point the wall was on (the point was pushed away from it)
constexpr uint32_t kWallOnXPlus = 0x100, kWallOnXMinus = 0x200, kWallOnZPlus = 0x400, kWallOnZMinus = 0x800;
constexpr uint32_t kWallOnYPlus = 0x1000, kWallOnYMinus = 0x2000;
constexpr uint32_t kWallOnSides = kWallOnXPlus | kWallOnXMinus | kWallOnZPlus | kWallOnZMinus;
constexpr uint32_t kWallOnY = kWallOnYPlus | kWallOnYMinus; // a floor or a ceiling
// resolveMove's dominant axis (the low byte): the largest of the push's components and its sign
constexpr uint32_t kPushAxisXPlus = 1, kPushAxisXMinus = 2, kPushAxisZPlus = 3, kPushAxisZMinus = 4,
                   kPushAxisYPlus = 5, kPushAxisYMinus = 6;
// resolveMove gave up (the point stays where the passes left it)
constexpr uint32_t kResolveGaveUp = 0x80001000u;

// exe 0x410040 (via exe 0x40fff0, the ledge-grab test of the player): the nearest wall face around `pos` in the
// direction `yaw` (4096 = 360 deg, the facing minus the stick offset), within the volumes whose height span contains
// pos.y. Returns (dz << 16) | (dx & 0xffff): signed distances to the nearest z / x wall face (kNoSurface = none); the
// wall direction (0, 0x400, 0x800, 0xc00, or the triangle's hypotenuse angle) is left in st.wallDirection.
uint32_t wallQuery(const CollisionRoom& room, CollisionState& st, const std::array<int16_t, 3>& pos,
                   const int16_t* box, int yaw, uint32_t mask);

// exe 0x413c80: the trigger volume (shape >= kShapeTrigger: exits, doors, events, status floors) the actor stands in.
// Volumes with kTriggerAhead are tested 0x40 ahead of the actor (along `actorYaw`); type kTriggerFacingType with
// kTriggerFacing only fires when the player faces direction (flags >> 8) & 3 (`playerYaw`).
struct TriggerHit {
    uint16_t type = 0;  // shape >> 8
    uint16_t flags = 0; // the volume's mask word
    int16_t x = 0, y = 0, z = 0; // the volume's centre
    uint16_t yaw = 0;   // the actor's yaw & 0xfff
};
constexpr uint16_t kTriggerAhead = 0x8000;
constexpr uint16_t kTriggerFacing = 0x400;
constexpr uint16_t kTriggerFacingType = 0x10;
uint32_t triggerScan(const CollisionRoom& room, CollisionState& st, const std::array<int16_t, 3>& pos, int16_t actorYaw,
                     int16_t playerYaw, TriggerHit& out);
// the grid holds a trigger volume of `type` (shape >> 8) in a cell triggerScan reads (host-side, no exe counterpart)
bool roomHasTrigger(const CollisionRoom& room, uint16_t type);
// the trigger type of an in-stage warp (a relative move: the action wrapped back by its exit's offset)
constexpr uint16_t kTriggerWarpType = 0x1;

// Volume primitives, also used on the collision meshes of solid actors (exe 0x414bd0 / 0x414790, see
// game/actor_collision). r = {x0, x1, y0, y1, z0, z1}; the point, previous point, delta, push accumulators and the
// mover's box are st's.
//   volumeShapeTest    the shape tests of table 0x889cc8 (shrinks r's x / z by the box; 0 = miss)
//   volumeAdjustSlope  exe 0x40f8d0(r, shape, 0)
//   volumeResolve      exe 0x410eb0(r, shape, test result): push the point out of the volume (0 = untouched)
int volumeShapeTest(CollisionState& st, int16_t r[6], int shape);
void volumeAdjustSlope(CollisionState& st, int16_t r[6], uint8_t shape);
uint32_t volumeResolve(CollisionState& st, const int16_t r[6], uint32_t shape, int testResult);
// the lanes of a volume rect r
constexpr int kRectX0 = 0, kRectX1 = 1, kRectY0 = 2, kRectY1 = 3, kRectZ0 = 4, kRectZ1 = 5;

} // namespace od
