// Room collision: the queries over the room's collision grid and volumes (see collision.hpp).
#include "game/collision.hpp"

#include <cmath>
#include <cstddef>
#include <utility>

#include "engine/x87.hpp"
#include "game/angles.hpp"
#include "game/fixed_point.hpp"

namespace od {

namespace {

using port::wrap16;

// a special cell's packed origin: its first x / z (cell << kCellShift) in the low / high half (columns & 0x7f)
constexpr uint32_t kCellColumnMask = 0x7f;
constexpr uint32_t kCellOriginMask = 0xfe00; // the origin of a coordinate's cell (its low kCellShift bits cleared)
// the y push's step-up tolerance on the map (noStepUp clear) and on a diagonal's push
constexpr int kStepUp = 0x18;
constexpr int kDiagonalStepUp = 0x20;
// a diagonal push stays vertical below this y gap when the point did not move in x / z
constexpr int kDiagonalStandGap = 0x41;
// the shape tests' margin of the triangles (the mover's box doubled plus this)
constexpr int kTriangleMargin = 0xd;
// the wide y push's reach (exe 0x412de0): the larger planar move plus kWideYMargin, at least kWideYMinReach
constexpr int kWideYMargin = 0x10;
constexpr int kWideYMinReach = 0x30;
// the push-out's flags (exe 0x412360): which axes a push may take and how the y one is measured
constexpr uint32_t kPushSidesOnly = 0x00; // x / z only
constexpr uint32_t kPushMayY = 0x01;      // a y push is allowed
constexpr uint32_t kPushToTop = 0x02;     // the y push goes onto the top even when the point moved up
constexpr uint32_t kPushStrictX = 0x10;   // an x push needs only an x move (not one at least as long)
constexpr uint32_t kPushStrictZ = 0x20;   // ... z
constexpr uint32_t kPushWideY = 0x40;     // the y push's reach measured against the whole move (exe 0x412de0)
constexpr uint32_t kPushFromBelow = 0x80; // the y push goes to the bottom, without the step-up tolerance
constexpr uint32_t kPushOnto = kPushWideY | kPushToTop | kPushMayY;                 // the boxes' usual push
constexpr uint32_t kPushUnder = kPushFromBelow | kPushWideY | kPushMayY;            // pushed out from below
// the push-out gives up after this many passes
constexpr int kMaxPushPasses = 9;
// the wall query's triangles: the hypotenuse faces the travel direction when ((yaw - angle - this) >> 10) & 3 + 2
// is the shape's test
constexpr int kTriangleFacingBias = 0x600;
// a special cell is resolved as a box (shape 2: a y push allowed) spanning every height
constexpr int16_t kSpecialBoxReach = 0x4000;
constexpr uint32_t kSpecialBoxShape = 2;

int div512(int v) { return v / 512; } // (v + (v >> 31 & 0x1ff)) >> 9
int sdiv(int a, int b) { return b ? a / b : 0; } // x86 idiv truncates; the game would fault on 0
int iabs(int v) { return v < 0 ? -v : v; }
int16_t sabs(int16_t v) { return v < 0 ? wrap16(-v) : v; }

// the volume rect r's x / z from a volume and its segment's anchor
void volumeRect(const VolumeRecord& v, const int16_t anchor[3], int16_t* r) {
    r[kRectX0] = wrap16(v.x0 + anchor[0]);
    r[kRectX1] = wrap16(v.x1 + anchor[0]);
    r[kRectZ0] = wrap16(v.z0 + anchor[2]);
    r[kRectZ1] = wrap16(v.z1 + anchor[2]);
}
// ... and its y
void volumeHeights(const VolumeRecord& v, const int16_t anchor[3], int16_t* r) {
    r[kRectY0] = wrap16(anchor[1] + v.y0);
    r[kRectY1] = wrap16(v.y1 + anchor[1]);
}
// the packed origin of the special cell at column ix / row iz
uint32_t specialCellOrigin(int ix, int iz) {
    return ((uint32_t(ix + kCellBias) & kCellColumnMask) << kCellShift) | (uint32_t(iz + kCellBias) << (16 + kCellShift));
}
// a special cell's full-cell box rect from its packed origin
void specialRect(uint32_t origin, int16_t* r) {
    r[kRectX0] = int16_t(origin & kCellOriginMask);
    r[kRectX1] = wrap16(int(origin & kCellOriginMask) + kCellSize);
    r[kRectZ0] = int16_t((origin >> 16) & kCellOriginMask);
    r[kRectZ1] = wrap16(int((origin >> 16) & kCellOriginMask) + kCellSize);
}

// a segment's volumes: the first volume's offset in the block, their count, the segment's anchor
struct SegmentVolumes {
    size_t first;
    uint32_t count;
    int16_t anchor[3];
};

struct Query {
    const CollisionRoom& room;
    CollisionState& st;

    // the cell at byte `cell` of the block
    GridCellRecord cellRecord(size_t cell) const { return GridCellRecord(room.bytes() + cell); }
    // ... its word
    uint16_t word(size_t cell) const { return cellRecord(cell).word; }
    // the volume at byte `at` of the block
    VolumeRecord volume(size_t at) const { return VolumeRecord(room.bytes() + at); }
    // the byte offset of cell number n of the grid
    size_t cellOffset(int n) const { return room.cells + size_t(n) * kGridCellSize; }

    // exe 0x40f0b0 / 0x40f030: the cell's byte offset in the block, or 0 outside the grid
    size_t cellAt(int16_t x, int16_t z) const {
        const int n = gridCellNumber(room.bounds(), gridColumn(x), gridColumn(z));
        return n < 0 ? 0 : cellOffset(n);
    }

    // exe 0x413000: up to 4 cells (2x2 by the half of the cell the point is in), deduped by segment
    struct Gathered {
        size_t cell[4];
        uint32_t special[4]; // kCellSpecial cells: the packed cell origin (lo16 x, hi16 z)
        int n = 0;
    };
    Gathered gather() const {
        Gathered gt{};
        const int px = st.point[0], pz = st.point[2];
        // the neighbour column / row on the side of the cell's half the point is in (as offsets from kGridOrigin)
        const uint16_t ox =
            uint16_t((px & kCellMask) < kCellSize / 2 + 1 ? kGridOrigin - kCellSize : kGridOrigin + kCellSize);
        const uint16_t oz =
            uint16_t((pz & kCellMask) < kCellSize / 2 + 1 ? kGridOrigin - kCellSize : kGridOrigin + kCellSize);
        const uint16_t offs[4][2] = {{kGridOrigin, kGridOrigin}, {ox, kGridOrigin}, {kGridOrigin, oz}, {ox, oz}};
        // the exe's scratch: the offset table and, per gathered special cell, its packed origin (the cell pointers
        // are exe addresses: not mirrored). Game code reads it back: the snow reaverbot 0x4a2a50 copies the second
        // special origin into its snow puffs (exe 0x4a4b50)
        for (size_t i = 0; i < 4; ++i) {
            st.gatherOffsets[i][0] = offs[i][0];
            st.gatherOffsets[i][1] = offs[i][1];
        }
        for (const auto& o : offs) {
            const int ix = (int(o[0]) + px) >> kCellShift, iz = (int(o[1]) + pz) >> kCellShift;
            const int n = gridCellNumber(room.bounds(), ix, iz);
            if (n < 0) continue;
            const size_t cell = cellOffset(n);
            const uint16_t w = word(cell);
            if (int16_t(w) >= 1) continue; // not kCellCollides
            bool dup = false;
            for (int j = 0; j < gt.n && !dup; ++j) {
                const uint16_t wj = word(gt.cell[j]);
                dup = !(wj & kCellFloor) && !(w & kCellFloor) && !((wj ^ w) & kCellSegmentMask);
            }
            if (dup) continue;
            if (w & kCellSpecial) {
                gt.special[gt.n] = specialCellOrigin(ix, iz);
                st.specialCells[gt.n] = gt.special[gt.n];
            }
            gt.cell[gt.n++] = cell;
        }
        return gt;
    }

    // exe 0x40f450: the heightmap's height at the point (two triangles; kCellDiagonal picks the split). The corner
    // heights are bytes x 16, corner 0 is at the cell's origin
    int floorHeight(size_t cell) const {
        const int fz = kCellMask - (st.point[2] & kCellMask);
        const uint32_t c = cellRecord(cell).corners;
        const int h0 = int(c & 0xff);
        const int fx = st.point[0] & kCellMask;
        const int h1 = int((int32_t(c) >> 4) & 0xff0), h2 = int((int32_t(c) >> 12) & 0xff0),
                  h3 = int((int32_t(c) >> 20) & 0xff0);
        int y0 = h0 * 0x10, slopeZ;
        if (!(word(cell) & kCellDiagonal)) {
            if (kCellMask - fx < fz) return div512((h1 - h3) * fz) + div512((h2 - h3) * fx) - h2 - h1 + h3;
            slopeZ = y0 - h2;
        } else {
            if (fx < fz) return div512((h2 - h3) * fx) + div512((y0 - h2) * fz) - h0 * 0x10;
            slopeZ = h1 - h3;
        }
        const int alongX = (y0 - h1) * fx;
        return div512(slopeZ * fz) + div512(alongX) - h0 * 0x10;
    }

    // exe 0x600df0: the volumes of the segment of a cell word (the volume set the segment selects) and the segment's
    // anchor (its cell's corner, half a cell further by the pool entry's flags, at its base level minus its height)
    SegmentVolumes volumes(uint16_t w) const {
        const uint8_t* b = room.bytes();
        const SegmentRecord seg(b + room.segments + kListHeaderSize + (w & kCellSegmentMask) * kSegmentEntrySize);
        const SegmentPoolRecord pool(b + room.pool + kListHeaderSize + uint32_t(seg.poolIndex) * kPoolEntrySize);
        const size_t baseCell =
            room.cells + size_t(int32_t(gridCellNumberUnchecked(room.bounds(), seg.cellX, seg.cellZ))) * kGridCellSize;
        const int level = (kCellBias - int(cellRecord(baseCell).baseLevel)) * 0x10;
        const uint32_t poolFlags = pool.flags;
        SegmentVolumes v{};
        v.anchor[1] = wrap16(int16_t(level) - ((seg.heightWord >> 4) & 0x7f0));
        v.anchor[0] = wrap16(((poolFlags & kPoolHalfCellX) ? kCellSize / 2 : 0) + kCellSize / 2 +
                             (int(seg.cellX) - kCellBias) * kCellSize);
        v.anchor[2] = wrap16(((poolFlags & kPoolHalfCellZ) ? kCellSize / 2 : 0) + kCellSize / 2 +
                             (int(seg.cellZ) - kCellBias) * kCellSize);
        const size_t set = size_t(seg.flags & kSegmentVariantMask) * kVolumeSetDwords + pool.volumeSets;
        const VolumeSetRecord vs(b + room.pool + set * 4);
        v.count = vs.volumeCount;
        v.first = room.pool + size_t(vs.volumeList) * 4;
        return v;
    }

    // the hit rect the shape tests leave
    void setHit(int x0, int x1, int z0, int z1) {
        st.hitX0 = wrap16(x0);
        st.hitX1 = wrap16(x1);
        st.hitZ0 = wrap16(z0);
        st.hitZ1 = wrap16(z1);
    }

    // The shape tests of table 0x889cc8 on r = {x0, x1, y0, y1, z0, z1}: r's x / z shrunk by the mover's box, then
    // the point inside the shape (0: no). Tests 0 / 1 are a box (returning 1 + which halves of its diagonals the point
    // is in); tests 2..5 a right triangle with its corner at (x1, z1), (x1, z0), (x0, z0), (x0, z1): the edge of its
    // hypotenuse at the point (moved out by the doubled box and kTriangleMargin, except for the variants 5 / 6 of
    // each family) bounds it; 1 = the previous point was inside the corner's quadrant (the hit rect is the triangle's
    // corner part), else 2 (the whole rect)
    int shapeTest(int16_t* r, int shape) {
        const int test = (shape >> kShapeTestShift) & 0xf;
        if (test > 5) return 0; // table entries 6.. are not code
        if (test <= 1 && shape == kShapeScriptPassable && st.scriptPassable) return 0;
        r[kRectX0] = wrap16(r[kRectX0] - st.box[kBoxMaxX]);
        r[kRectX1] = wrap16(r[kRectX1] - st.box[kBoxMinX]);
        r[kRectZ0] = wrap16(r[kRectZ0] - st.box[kBoxMaxZ]);
        r[kRectZ1] = wrap16(r[kRectZ1] - st.box[kBoxMinZ]);
        const int x0 = r[kRectX0], x1 = r[kRectX1], z0 = r[kRectZ0], z1 = r[kRectZ1];
        const int px = st.point[0], pz = st.point[2], qx = st.previous[0], qz = st.previous[2];
        switch (test) {
        case 0:
        case 1: { // exe 0x411c00: a box
            if (!(x0 < px && px < x1 && z0 < pz && pz < z1)) return 0;
            setHit(x0, x1, z0, z1);
            const int width = x1 - x0;
            int halves = 0;
            if (px < sdiv((pz - z0) * width, z1 - z0) + x0) halves = 2;
            if (px < sdiv((z1 - pz) * width, z1 - z0) + x0) halves |= 1;
            return halves + 1;
        }
        case 2: { // exe 0x411d20: the corner at (x1, z1), the hypotenuse from (x0, z1) to (x1, z0)
            int margin, cornerX, cornerZ;
            if (shape < 0x25 || shape > 0x26) {
                cornerX = x0 - st.box[kBoxMaxX] * 2 - kTriangleMargin;
                margin = kTriangleMargin;
                cornerZ = z0 - st.box[kBoxMaxZ] * 2 - kTriangleMargin;
            } else {
                margin = 0;
                cornerX = x0;
                cornerZ = z0;
            }
            int edgeX = (x1 - sdiv((pz - cornerZ) * (x1 - cornerX), z1 - cornerZ)) - 1;
            if (edgeX - margin < cornerX) edgeX = cornerX;
            if (edgeX < x0) edgeX = x0;
            int edgeZ = (z1 - sdiv((px - cornerX) * (z1 - cornerZ), x1 - cornerX)) - 1;
            if (edgeZ - margin < cornerZ) edgeZ = cornerZ;
            if (edgeZ < z0) edgeZ = z0;
            if (!(edgeX < px && px < x1 && edgeZ < pz && pz < z1)) return 0;
            if (qx < x1 && qz < z1) {
                setHit(edgeX, x1, edgeZ, z1);
                return 1;
            }
            setHit(x0, x1, z0, z1);
            return 2;
        }
        case 3: { // exe 0x411eb0: the corner at (x1, z0)
            int margin, cornerX, cornerZ;
            if (shape < 0x35 || shape > 0x36) {
                cornerX = x0 - st.box[kBoxMaxX] * 2 - kTriangleMargin;
                margin = kTriangleMargin;
                cornerZ = z1 - st.box[kBoxMinZ] * 2 + kTriangleMargin;
            } else {
                margin = 0;
                cornerX = x0;
                cornerZ = z1;
            }
            int edgeX = sdiv((pz - z0) * (x1 - cornerX), cornerZ - z0) - 1 + cornerX;
            if (edgeX - margin < cornerX) edgeX = cornerX;
            if (edgeX < x0) edgeX = x0;
            int edgeZ = sdiv((px - cornerX) * (cornerZ - z0), x1 - cornerX) + 1 + z0;
            if (cornerZ < edgeZ + margin) edgeZ = cornerZ;
            if (z1 < edgeZ) edgeZ = z1;
            if (!(edgeX < px && px < x1 && z0 < pz && pz < edgeZ)) return 0;
            if (qx < x1 && z0 < qz) {
                setHit(edgeX, x1, z0, edgeZ);
                return 1;
            }
            setHit(x0, x1, z0, z1);
            return 2;
        }
        case 4: { // exe 0x412040: the corner at (x0, z0)
            int margin, cornerX, cornerZ;
            if (shape < 0x45 || shape > 0x46) {
                margin = kTriangleMargin;
                cornerX = x1 - st.box[kBoxMinX] * 2 + kTriangleMargin;
                cornerZ = z1 - st.box[kBoxMinZ] * 2 + kTriangleMargin;
            } else {
                margin = 0;
                cornerX = x1;
                cornerZ = z1;
            }
            int edgeX = (cornerX - sdiv((pz - z0) * (cornerX - x0), cornerZ - z0)) + 1;
            if (cornerX < edgeX + margin) edgeX = cornerX;
            if (x1 < edgeX) edgeX = x1;
            int edgeZ = (cornerZ - sdiv((px - x0) * (cornerZ - z0), cornerX - x0)) + 1;
            if (cornerZ < margin + edgeZ) edgeZ = cornerZ;
            if (z1 < edgeZ) edgeZ = z1;
            if (!(x0 < px && px < edgeX && z0 < pz && pz < edgeZ)) return 0;
            if (x0 < qx && z0 < qz) {
                setHit(x0, edgeX, z0, edgeZ);
                return 1;
            }
            setHit(x0, x1, z0, z1);
            return 2;
        }
        default: { // 5, exe 0x4121d0: the corner at (x0, z1)
            int margin, cornerX, cornerZ;
            if (shape < 0x55 || shape > 0x56) {
                cornerX = x1 - st.box[kBoxMinX] * 2 + kTriangleMargin;
                margin = kTriangleMargin;
                cornerZ = z0 - st.box[kBoxMaxZ] * 2 - kTriangleMargin;
            } else {
                margin = 0;
                cornerZ = z0;
                cornerX = x1;
            }
            int edgeX = sdiv((pz - cornerZ) * (cornerX - x0), z1 - cornerZ) + 1 + x0;
            if (cornerX < edgeX + margin) edgeX = cornerX;
            if (x1 < edgeX) edgeX = x1;
            int edgeZ = sdiv((px - x0) * (z1 - cornerZ), cornerX - x0) - 1 + cornerZ;
            if (edgeZ - margin < cornerZ) edgeZ = cornerZ;
            if (edgeZ < z0) edgeZ = z0;
            if (!(x0 < px && px < edgeX && edgeZ < pz && pz < z1)) return 0;
            if (x0 < qx && qz < z1) {
                setHit(x0, edgeX, edgeZ, z1);
                return 1;
            }
            setHit(x0, x1, z0, z1);
            return 2;
        }
        }
    }

    // exe 0x40f8d0: the volume's surface on its slope at the point (standOnBottom: the flat tops stand on y1; the
    // wall query passes it). The ramps rise from y0 to y1 across the rect (along x or z, or across a diagonal for the
    // stairs), the start moved back by the mover's box; the result is r's y0 / y1 minus the box's y extents
    void adjustSlope(int16_t* r, uint8_t shape, int standOnBottom = 0) const {
        const int x1 = r[kRectX1], z1 = r[kRectZ1], x0 = r[kRectX0], y0 = r[kRectY0], y1 = r[kRectY1];
        const int px = st.point[0], pz = st.point[2], boxMinX = st.box[kBoxMinX];
        const int boxWidth = st.box[kBoxMaxX] - st.box[kBoxMinX], boxDepth = st.box[kBoxMaxZ] - st.box[kBoxMinZ];
        const int16_t top = r[kRectY0];
        int16_t surface;
        int rampY = 0; // a plain ramp's height at the point (its top or bottom, by the shape)
        auto setSurface = [&](int16_t v) { // store + "below the base" check
            r[kRectY0] = v;
            if (v < y0) r[kRectY0] = top;
        };
        switch (shape) {
        case 1: case 0x21: case 0x31: case 0x41: case 0x51: // flat top variants: standOnBottom -> stand on y1
            if (standOnBottom) r[kRectY0] = int16_t(y1);
            break;
        case 4: case 0x10: case 0x25: case 0x56: { // rising along +z
            const int depth = z1 - r[kRectZ0];
            rampY = y0 - sdiv(boxDepth * (y1 - y0), depth);
            const int along = pz - r[kRectZ0];
            rampY = sdiv(along * (y1 - rampY), depth) + rampY;
            if (rampY < y0) rampY = y0;
            break;
        }
        case 5: case 0x11: case 0x36: case 0x45: { // rising along -z
            const int depth = z1 - r[kRectZ0];
            rampY = y0 - sdiv(boxDepth * (y1 - y0), depth);
            const int along = z1 - pz;
            rampY = sdiv(along * (y1 - rampY), depth) + rampY;
            if (rampY < y0) rampY = y0;
            break;
        }
        case 6: case 0x12: case 0x26: case 0x35: { // rising along +x
            const int width = x1 - x0;
            rampY = y0 - sdiv(boxWidth * (y1 - y0), width);
            const int along = px - x0;
            rampY = sdiv(along * (y1 - rampY), width) + rampY;
            if (rampY < y0) rampY = y0;
            break;
        }
        case 7: case 0x13: case 0x46: case 0x55: { // rising along -x
            const int width = x1 - x0;
            rampY = y0 - sdiv(boxWidth * (y1 - y0), width);
            const int along = x1 - px;
            rampY = sdiv(along * (y1 - rampY), width) + rampY;
            if (rampY < y0) rampY = y0;
            break;
        }
        case 0x14: case 0x44: { // the diagonal stairs
            const int base = y0 - sdiv(boxWidth * (y1 - y0), x1 - x0);
            surface = wrap16(sdiv((((px - r[kRectZ0]) + pz) - x0 + boxMinX) * (y1 - base), x1 - x0));
            setSurface(wrap16(int16_t(base) + surface));
            break;
        }
        case 0x15: case 0x54: {
            const int base = y0 - sdiv(boxWidth * (y1 - y0), x1 - x0);
            surface = wrap16(int16_t(base) + int16_t(sdiv((((px - pz) - x0) + boxMinX + z1) * (y1 - base), x1 - x0)));
            setSurface(surface);
            break;
        }
        case 0x16: case 0x24: {
            const int width = x1 - x0;
            const int base = y0 - sdiv(boxWidth * (y1 - y0), width);
            const int across = (boxMinX - px) - pz;
            surface = wrap16(sdiv((across + z1 + x1) * (y1 - base), width));
            setSurface(wrap16(int16_t(base) + surface));
            break;
        }
        case 0x17: case 0x34: {
            const int width = x1 - x0;
            const int base = y0 - sdiv(boxWidth * (y1 - y0), width);
            const int across = (pz - px) - r[kRectZ0];
            surface = wrap16(sdiv((across + boxMinX + x1) * (y1 - base), width));
            setSurface(wrap16(int16_t(base) + surface));
            break;
        }
        case 0x18: { // a ridge along x (rising toward the middle in z)
            const int depth = z1 - r[kRectZ0];
            rampY = (y0 * 2 - sdiv(boxDepth * (y1 - y0), depth)) - y1;
            if (depth / 2 + r[kRectZ0] < pz) surface = wrap16(sdiv((pz - r[kRectZ0]) * (y1 - rampY), depth) + int16_t(rampY));
            else surface = wrap16(int16_t(sdiv((z1 - pz) * (y1 - rampY), depth)) + int16_t(rampY));
            setSurface(surface);
            break;
        }
        case 0x19: { // a ridge along z (rising toward the middle in x)
            const int width = x1 - x0;
            rampY = (y0 * 2 - sdiv(boxWidth * (y1 - y0), width)) - y1;
            if (width / 2 + x0 < px) surface = wrap16(int16_t(sdiv((y1 - rampY) * (px - x0), width)) + int16_t(rampY));
            else surface = wrap16(int16_t(sdiv((x1 - px) * (y1 - rampY), width)) + int16_t(rampY));
            setSurface(surface);
            break;
        }
        case 0x1a: { // a pyramid: the face of the quarter the point is in (split by the rect's diagonals)
            const int width = x1 - x0, alongZ = pz - r[kRectZ0], depth = z1 - r[kRectZ0];
            rampY = y0 * 2 - y1;
            int quarter = 0;
            if (px < sdiv(alongZ * width, depth) + x0) quarter = 2;
            if (px < sdiv((z1 - pz) * width, depth) + x0) quarter |= 1;
            int16_t v = int16_t(rampY);
            switch (quarter) {
            case 0: v = wrap16(int16_t(sdiv((y1 - rampY) * (px - x0), width)) + v); break;
            case 1: v = wrap16(int16_t(sdiv((y1 - rampY) * (z1 - pz), depth)) + v); break;
            case 2: v = wrap16(int16_t(sdiv((y1 - rampY) * alongZ, depth)) + v); break;
            default: v = wrap16(int16_t(sdiv((x1 - px) * (y1 - rampY), width)) + v); break;
            }
            r[kRectY0] = v;
            break;
        }
        case 0x23: { // the triangles' stairs, down from y1
            const int width = x1 - x0;
            const int fall = ((sdiv(boxWidth * (y1 - y0), width) - y0) + y1) * (((z1 - px) - pz) + x1);
            setSurface(wrap16(int16_t(y1) - int16_t(sdiv(fall, width))));
            break;
        }
        case 0x33: {
            const int width = x1 - x0;
            const int fall = ((sdiv(boxWidth * (y1 - y0), width) - y0) + y1) * (((pz - px) - r[kRectZ0]) + x1);
            setSurface(wrap16(int16_t(y1) - int16_t(sdiv(fall, width))));
            break;
        }
        case 0x43: {
            const int width = x1 - x0;
            const int fall = ((sdiv(boxWidth * (y1 - y0), width) - y0) + y1) * (((px - r[kRectZ0]) + pz) - x0);
            setSurface(wrap16(int16_t(y1) - int16_t(sdiv(fall, width))));
            break;
        }
        case 0x53: {
            const int width = x1 - x0;
            const int fall = ((sdiv(boxWidth * (y1 - y0), width) - y0) + y1) * (((px - pz) - x0) + z1);
            setSurface(wrap16(int16_t(y1) - int16_t(sdiv(fall, width))));
            break;
        }
        default:
            break;
        }
        const int16_t v = int16_t(rampY);
        if (3 < shape && shape < 8) r[kRectY1] = v;
        if (0xf < shape && shape < 0x14) r[kRectY0] = v;
        if (0x1f < shape && 4 < (shape & 7) && (shape & 7) < 7) r[kRectY0] = v;
        r[kRectY0] = wrap16(r[kRectY0] - st.box[kBoxMaxY]);
        r[kRectY1] = wrap16(r[kRectY1] - st.box[kBoxMinY]);
    }

    // ---- push-out (exe 0x410eb0 and helpers); l = {x, y, z} correction --------------------------
    // exe 0x412f80: an x push: the applied x push dropped when the last push was along x too
    void axisX(const int16_t* l) {
        if (st.lastPushAxis & kPushedX) st.appliedPushX = 0;
        st.lastPushAxis = l[0] > 0 ? kPushedXPlus : kPushedXMinus;
    }
    // exe 0x412fb0: ... z
    void axisZ(const int16_t* l) {
        if (st.lastPushAxis & kPushedZ) st.appliedPushZ = 0;
        st.lastPushAxis = l[2] > 0 ? kPushedZPlus : kPushedZMinus;
    }
    // exe 0x412fe0: a y push
    void axisY(const int16_t* l) { st.lastPushAxis = l[1] > 0 ? kPushedYPlus : kPushedYMinus; }
    // exe 0x412cc0 / 0x412d20: may the push go along x (the move went along x, by at least the push unless strict,
    // and the last push was not the opposite way)
    uint32_t okX(const int16_t* l, bool strict) const {
        const int d = st.delta[0];
        const bool u = strict ? (d != 0 && l[0] != 0) : (d != 0 && iabs(l[0]) <= iabs(d));
        if (!u) return 0;
        return l[0] > 0 ? ((uint8_t(~st.lastPushAxis) & kPushedXMinus) >> 1) : (uint8_t(~st.lastPushAxis) & kPushedXPlus);
    }
    // exe 0x412ec0 / 0x412f30: ... z
    uint32_t okZ(const int16_t* l, bool strict) const {
        const int d = st.delta[2];
        const bool u = strict ? (d != 0 && l[2] != 0) : (d != 0 && iabs(l[2]) <= iabs(d));
        if (!u) return 0;
        return l[2] > 0 ? ((uint8_t(~st.lastPushAxis) & kPushedZMinus) >> 3)
                        : ((uint8_t(~st.lastPushAxis) & kPushedZPlus) >> 2);
    }
    // exe 0x412d70: ... y
    uint32_t okY(const int16_t* l) const {
        const int d = st.delta[1];
        const bool u = d != 0 && iabs(l[1]) <= iabs(d);
        if (!u) return 0;
        return l[1] > 0 ? ((uint8_t(~st.lastPushAxis) & kPushedYMinus) >> 5)
                        : ((uint8_t(~st.lastPushAxis) & kPushedYPlus) >> 4);
    }
    // exe 0x412de0: ... y, reaching as far as the whole move (kWideYMargin past the larger planar move, at least
    // kWideYMinReach, plus the y move)
    uint32_t okYWide(const int16_t* l) const {
        const int dx = st.delta[0], dy = st.delta[1], dz = st.delta[2];
        const int planar = iabs(dz) < iabs(dx) ? iabs(dx) : iabs(dz);
        const int reach = planar + kWideYMargin < kWideYMinReach ? kWideYMinReach : planar + kWideYMargin;
        const bool u = !(reach + iabs(dy) < iabs(l[1]));
        if (!u) return 0;
        return l[1] > 0 ? ((uint8_t(~st.lastPushAxis) & kPushedYMinus) >> 5)
                        : ((uint8_t(~st.lastPushAxis) & kPushedYPlus) >> 4);
    }

    // exe 0x412a30: no axis was "fresh": push along the shortest way out
    uint32_t pushShortest(uint8_t flags) {
        const int px = st.point[0], py = st.point[1], pz = st.point[2];
        int16_t l[3];
        {
            const int toX0 = st.hitX0 - px, toX1 = st.hitX1 - px;
            l[0] = wrap16(iabs(toX1) <= iabs(toX0) ? toX1 : toX0);
            const int toZ0 = st.hitZ0 - pz, toZ1 = st.hitZ1 - pz;
            l[2] = wrap16(iabs(toZ1) <= iabs(toZ0) ? toZ1 : toZ0);
        }
        int toTop = st.hitTop - py;
        const int toBottom = st.hitBottom - py;
        if (!(flags & (kPushFromBelow | kPushToTop))) {
            if (iabs(toBottom) <= iabs(toTop)) toTop = toBottom & 0xffff;
        } else if (!(flags & kPushToTop)) {
            toTop = toBottom & 0xffff;
        }
        l[1] = wrap16(toTop);
        if (flags & kPushMayY) {
            const int ay = iabs(l[1]);
            const int m = iabs(l[0]) < iabs(l[2]) ? iabs(l[0]) : iabs(l[2]);
            if (ay < m && !(st.lastPushAxis & kPushedY)) {
                st.point[1] = wrap16(py + l[1]);
                st.pendingPush[1] = wrap16(-l[1]);
                st.previous[1] = wrap16(st.previous[1] + l[1]);
                axisY(l);
                return 1;
            }
        }
        const int ax = iabs(l[0]), az = iabs(l[2]);
        const uint8_t f = st.lastPushAxis;
        bool xMove;
        if (ax <= az && !(f & kPushedX)) xMove = true;
        else if (az <= ax && !(f & kPushedZ)) xMove = false;
        else if (((l[2] > 0 && (f & kPushedZPlus)) || (l[2] < 0 && (f & kPushedZMinus))) && !(f & kPushedX)) xMove = true;
        else {
            int xReach = l[0];
            bool toZ = false;
            if (l[0] < 1 || !(f & kPushedXPlus)) {
                if (l[0] < 0) {
                    if ((f & kPushedXMinus) && !(f & kPushedZ)) toZ = true;
                    else xReach = -xReach;
                }
            } else {
                if (!(f & kPushedZ)) toZ = true;
            }
            xMove = !toZ && xReach < iabs(l[2]);
        }
        if (xMove) {
            st.point[0] = wrap16(px + l[0]);
            st.previous[0] = wrap16(st.previous[0] + l[0]);
            axisX(l);
        } else {
            st.point[2] = wrap16(pz + l[2]);
            st.previous[2] = wrap16(st.previous[2] + l[2]);
            axisZ(l);
        }
        return 0;
    }

    // exe 0x412360: push out of the hit rect along the axis the move came from (1: pushed along y)
    uint32_t pushOut(uint32_t flags) {
        const int px = st.point[0], py = st.point[1], pz = st.point[2];
        int16_t l[3];
        l[0] = wrap16((st.delta[0] < 0 ? st.hitX1 : st.hitX0) - px);
        l[2] = wrap16((st.delta[2] >= 0 ? st.hitZ0 : st.hitZ1) - pz);
        int ly = st.hitBottom;
        if ((st.delta[1] >= 0 || (flags & kPushToTop)) && !(flags & kPushFromBelow)) ly = st.hitTop;
        l[1] = wrap16(ly - py);
        const int16_t ax = sabs(l[0]), az = sabs(l[2]);
        int16_t ay = sabs(l[1]);
        if (st.noStepUp == 0 && !(flags & kPushFromBelow)) {
            ay = wrap16(ay - kStepUp);
            if (ay < 0) ay = 0;
        }
        const uint32_t bx = okX(l, flags & kPushStrictX);
        const uint32_t bz = okZ(l, flags & kPushStrictZ);
        const uint32_t by = (flags & kPushWideY) ? okYWide(l) : (okY(l) & flags);
        if (bz == 0 && bx == 0) {
            if (by == 0 || (!(flags & kPushWideY) && st.delta[1] == 0)) return pushShortest(uint8_t(flags));
        } else {
            if (bx != 0) {
                if (bz != 0) {
                    bool yFirst = false;
                    if (by != 0) {
                        const int16_t m = az <= ax ? az : ax;
                        yFirst = ay <= m;
                    }
                    if (!yFirst) {
                        if (ax < az) {
                            st.point[0] = wrap16(px + l[0]);
                            st.pendingPush[0] = wrap16(-l[0]);
                            axisX(l);
                            return 0;
                        }
                        st.point[2] = wrap16(pz + l[2]);
                        st.pendingPush[2] = wrap16(-l[2]);
                        axisZ(l);
                        return 0;
                    }
                } else if (by == 0 || ax < ay) {
                    st.point[0] = wrap16(px + l[0]);
                    axisX(l);
                    return 0;
                }
                st.point[1] = wrap16(py + l[1]);
                st.pendingPush[1] = wrap16(-l[1]);
                axisY(l);
                return 1;
            }
            if (by == 0 || az < ay) {
                st.point[2] = wrap16(pz + l[2]);
                axisZ(l);
                return 0;
            }
        }
        st.point[1] = wrap16(py + l[1]);
        st.pendingPush[1] = wrap16(-l[1]);
        axisY(l);
        return 1;
    }

    // exe 0x412610: push out across a triangle's hypotenuse (flags >> 8 = its corner 2..5), or up / down when that
    // is shorter (1: pushed along y)
    uint32_t pushDiagonal(uint32_t flags) {
        const int px = st.point[0], pz = st.point[2];
        int toEdgeX, toEdgeZ; // to the hypotenuse's ends along x / z (one past the edge)
        switch (int(flags) >> 8) {
        case 2: toEdgeX = st.hitX0 - px - 1; toEdgeZ = st.hitZ0 - pz - 1; break;
        case 3: toEdgeX = st.hitX0 - px - 1; toEdgeZ = st.hitZ1 - pz + 1; break;
        case 4: toEdgeX = st.hitX1 - px + 1; toEdgeZ = st.hitZ1 - pz + 1; break;
        case 5: toEdgeX = st.hitX1 - px + 1; toEdgeZ = st.hitZ0 - pz - 1; break;
        default: return 0;
        }
        const int lengthSq = toEdgeX * toEdgeX + toEdgeZ * toEdgeZ;
        int16_t l[3];
        l[0] = wrap16(sdiv(toEdgeZ * toEdgeZ * toEdgeX, lengthSq)); // the foot of the perpendicular to the hypotenuse
        l[2] = wrap16(sdiv(toEdgeZ * toEdgeX * toEdgeX, lengthSq));
        if (l[0] != 0) {
            if (l[0] < 1) {
                l[0] = wrap16(l[0] - 1);
                if (l[0] < toEdgeX) l[0] = wrap16(toEdgeX);
            } else {
                l[0] = wrap16(l[0] + 1);
                if (toEdgeX < l[0]) l[0] = wrap16(toEdgeX);
            }
        }
        if (l[2] != 0) {
            if (l[2] < 1) {
                l[2] = wrap16(l[2] - 1);
                if (l[2] < toEdgeZ) l[2] = wrap16(toEdgeZ);
            } else {
                l[2] = wrap16(l[2] + 1);
                if (toEdgeZ < l[2]) l[2] = wrap16(toEdgeZ);
            }
        }
        int ly = st.hitTop;
        if (st.delta[1] < 0 && !(flags & kPushToTop)) ly = st.hitBottom;
        l[1] = wrap16(ly - st.point[1]);
        const int16_t ax = sabs(l[0]), az = sabs(l[2]);
        int16_t ay = sabs(l[1]);
        if (st.airborne == 0) {
            ay = wrap16(ay - kDiagonalStepUp);
            if (ay < 0) ay = 0;
        }
        const uint32_t by = (flags & kPushWideY) ? okYWide(l) : (okY(l) & flags);
        if ((st.delta[0] == 0 && st.delta[2] == 0 && ay < kDiagonalStandGap) || (by != 0 && ay <= ax && ay <= az)) {
            st.point[1] = wrap16(st.point[1] + l[1]);
            st.pendingPush[1] = wrap16(-l[1]);
            axisY(l);
            return 1;
        }
        st.point[0] = wrap16(px + l[0]);
        st.point[2] = wrap16(pz + l[2]);
        st.appliedPushX = 0;
        st.appliedPushZ = 0;
        st.lastPushAxis = 0;
        return 0;
    }

    // exe 0x412860: snap to a side of the rect (shapes 8..0xb)
    void snapSide(int side) {
        int16_t l[3] = {};
        switch (side) {
        case 0: case 1: {
            const int pz = st.point[2];
            l[2] = wrap16((side == 0 ? st.hitZ1 : st.hitZ0) - pz);
            st.point[2] = wrap16(pz + l[2]);
            axisZ(l);
            break;
        }
        case 2: case 3: {
            const int px = st.point[0];
            l[0] = wrap16((side == 2 ? st.hitX1 : st.hitX0) - px);
            st.point[0] = wrap16(px + l[0]);
            axisX(l);
            break;
        }
        default: break;
        }
    }
    // exe 0x412910: snap to a corner of the rect (shapes 0xc..0xf), diagonally (by the shorter of the two distances)
    void snapCorner(int corner) {
        int cornerX, cornerZ;
        switch (corner) {
        case 0: cornerX = st.hitX1; cornerZ = st.hitZ1; break;
        case 1: cornerX = st.hitX1; cornerZ = st.hitZ0; break;
        case 2: cornerX = st.hitX0; cornerZ = st.hitZ0; break;
        case 3: cornerX = st.hitX0; cornerZ = st.hitZ1; break;
        default: return;
        }
        const int px = st.point[0], pz = st.point[2];
        const int dx = cornerX - px, dz = cornerZ - pz;
        const int adx = iabs(dx), adz = iabs(dz);
        int16_t l[3] = {};
        if (adz < adx) {
            l[2] = wrap16(dz);
            l[0] = wrap16(((0 < dx) * 2 - 1) * adz);
        } else {
            l[0] = wrap16(dx);
            l[2] = wrap16(((0 < dz) * 2 - 1) * adx);
        }
        st.point[0] = wrap16(px + l[0]);
        st.point[2] = wrap16(pz + l[2]);
        axisX(l);
        axisZ(l);
    }

    // a mover pushed up out of a volume while falling lands on it: airborne cleared, landed set (one word store)
    void landFix() {
        if (st.airborne != 0 && st.delta[1] >= 0) {
            st.airborne = 0;
            st.landed = 1;
        }
    }
    // the applied push cleared after a push that moved y
    void dropAppliedPush() {
        st.appliedPushX = 0;
        st.appliedPushY = 0;
        st.appliedPushZ = 0;
    }

    // exe 0x410eb0: resolve against one hit volume r (x0 x1 y0 y1 z0 z1, shrunk by the test): the push by the
    // volume's shape -- a box, a ramp (its surface the bottom or the top), a ridge, a pyramid, a triangle, the
    // stairs, a side / corner snap
    uint32_t resolveVolume(const int16_t* r, uint32_t shapeFull, int testResult) {
        const int z1 = r[kRectZ1], z0 = r[kRectZ0], x0 = r[kRectX0], x1 = r[kRectX1], y0 = r[kRectY0], y1 = r[kRectY1];
        const int boxMaxY = st.box[kBoxMaxY], boxMinY = st.box[kBoxMinY], boxMinX = st.box[kBoxMinX];
        const int boxWidth = st.box[kBoxMaxX] - st.box[kBoxMinX], boxDepth = st.box[kBoxMaxZ] - st.box[kBoxMinZ];
        const int top = y0 - boxMaxY, bottom = y1 - boxMinY; // the volume's y span for the mover's box
        const int16_t top16 = wrap16(top), bottom16 = wrap16(bottom);
        const int sh = int(shapeFull & 0xff);
        const int px = st.point[0], py = st.point[1], pz = st.point[2];
        uint32_t pushFlags = shapeFull;
        int surface = 0;

        auto prevInsideXZ = [&] { // shapes 0x1b / 0x27..: previous point inside the volume's xz rect
            const int qx = st.previous[0], qz = st.previous[2];
            return !(qx <= x0 || x1 <= qx || qz <= z0 || z1 <= qz);
        };
        // a box's push (the point within the y span)
        auto pushOutOfBox = [&](uint32_t flags) -> uint32_t {
            if (top < py && py < bottom) {
                st.hitTop = top16;
                st.hitBottom = bottom16;
                if (pushOut(flags)) {
                    landFix();
                    dropAppliedPush();
                }
                return 1;
            }
            return 0;
        };
        // exe 0x4116ad: a triangle's push: a hit of its corner part across the hypotenuse, else as a box
        auto pushOutOfTriangle = [&](uint32_t flags) -> uint32_t {
            if (py <= top) return 0;
            if (bottom <= py) return 0;
            st.hitTop = top16;
            if (testResult != 1) {
                st.hitTop = top16;
                st.hitBottom = bottom16;
                if (pushOut(flags)) {
                    landFix();
                    dropAppliedPush();
                }
                return 1;
            }
            st.hitBottom = bottom16;
            if (pushDiagonal(((pushFlags & 0xf0) << 4) | flags) == 0) return 1; // the shape's test is the corner
            landFix();
            dropAppliedPush();
            return 1;
        };
        // exe 0x4112b0: under a plain ramp (shapes 4..7): the ramp's surface is the bottom, pushed out from below
        auto pushUnderRamp = [&](int rampY) -> uint32_t {
            if (top < py && py < rampY - boxMinY) {
                st.hitBottom = wrap16(rampY - boxMinY);
                st.hitTop = top16;
                if (pushOut(pushFlags | kPushUnder)) {
                    st.appliedPushX = 0;
                    st.appliedPushY = 0;
                }
                st.appliedPushZ = 0;
                return 1;
            }
            return 0;
        };
        // exe 0x411a98: onto a ramp, a ridge, a pyramid or the stairs: the surface is the top
        auto pushOntoRamp = [&](int rampY) -> uint32_t {
            if (rampY - boxMaxY < py && py < bottom) {
                st.hitTop = wrap16(rampY - boxMaxY);
                st.hitBottom = bottom16;
                if (pushOut(pushFlags | kPushOnto)) {
                    landFix();
                    dropAppliedPush();
                }
                return 1;
            }
            return 0;
        };
        auto clampY0 = [&](int v) { return v < y0 ? y0 : v; };

        switch (sh) {
        case 0: return pushOutOfBox(kPushOnto);
        case 1: return pushOutOfBox(kPushSidesOnly);
        case 2: case 0x1c: return pushOutOfBox(kPushMayY);
        case 3: return pushOutOfBox(kPushUnder);
        case 0x1b: return pushOutOfBox(prevInsideXZ() ? kPushWideY | kPushMayY : kPushOnto);
        case 4: case 0x10: case 0x25: case 0x56: { // rising along +z (4: under it)
            const int depth = z1 - z0;
            surface = y0 - sdiv(boxDepth * (y1 - y0), depth);
            const int rise = y1 - surface;
            surface = clampY0(sdiv((pz - z0) * rise, depth) + surface);
            const int inset = sdiv((y1 - py) * depth, rise);
            if (0 < inset) {
                if (sh == 4) st.hitZ0 = wrap16(st.hitZ0 + inset);
                else st.hitZ1 = wrap16(st.hitZ1 - inset);
            }
            pushFlags = kPushStrictZ;
            return sh == 4 ? pushUnderRamp(surface) : pushOntoRamp(surface);
        }
        case 5: case 0x11: case 0x36: case 0x45: { // rising along -z
            const int depth = z1 - z0;
            surface = y0 - sdiv(boxDepth * (y1 - y0), depth);
            const int rise = y1 - surface;
            surface = clampY0(sdiv((z1 - pz) * rise, depth) + surface);
            const int inset = sdiv((y1 - py) * depth, rise);
            if (0 < inset) {
                if (sh == 5) st.hitZ1 = wrap16(st.hitZ1 - inset);
                else st.hitZ0 = wrap16(st.hitZ0 + inset);
            }
            pushFlags = kPushStrictZ;
            return sh == 5 ? pushUnderRamp(surface) : pushOntoRamp(surface);
        }
        case 6: case 0x12: case 0x26: case 0x35: { // rising along +x
            const int width = x1 - x0;
            surface = y0 - sdiv(boxWidth * (y1 - y0), width);
            const int rise = y1 - surface;
            surface = clampY0(sdiv((px - x0) * rise, width) + surface);
            const int inset = sdiv((y1 - py) * width, rise);
            if (0 < inset) {
                if (sh == 6) st.hitX0 = wrap16(st.hitX0 + inset);
                else st.hitX1 = wrap16(st.hitX1 - inset);
            }
            pushFlags = kPushStrictX;
            return sh == 6 ? pushUnderRamp(surface) : pushOntoRamp(surface);
        }
        case 7: case 0x13: case 0x46: case 0x55: { // rising along -x
            const int width = x1 - x0;
            surface = y0 - sdiv(boxWidth * (y1 - y0), width);
            const int rise = y1 - surface;
            surface = clampY0(sdiv((x1 - px) * rise, width) + surface);
            const int inset = sdiv((y1 - py) * width, rise);
            if (0 < inset) {
                if (sh == 7) st.hitX1 = wrap16(st.hitX1 - inset);
                else st.hitX0 = wrap16(st.hitX0 + inset);
            }
            pushFlags = kPushStrictX;
            return sh == 7 ? pushUnderRamp(surface) : pushOntoRamp(surface);
        }
        case 8: case 9: case 10: case 0xb:
            if (py <= top || bottom <= py) return 0;
            snapSide(sh - 8);
            return 1;
        case 0xc: case 0xd: case 0xe: case 0xf:
            if (py <= top || bottom <= py) return 0;
            snapCorner(sh - 0xc);
            return 1;
        case 0x18: { // a ridge along x
            const int depth = z1 - z0;
            const int baseY = (y0 * 2 - sdiv(boxDepth * (y1 - y0), depth)) - y1;
            if (pz < depth / 2 + z0) {
                surface = clampY0(sdiv((z1 - pz) * (y1 - baseY), depth) + baseY);
                const int inset = sdiv((y1 - py) * depth, y1 - baseY);
                if (0 < inset) st.hitZ0 = wrap16(st.hitZ0 + inset);
            } else {
                surface = clampY0(sdiv((pz - z0) * (y1 - baseY), depth) + baseY);
                const int inset = sdiv((y1 - py) * depth, y1 - baseY);
                if (0 < inset) st.hitZ1 = wrap16(st.hitZ1 - inset);
            }
            pushFlags = kPushStrictZ;
            return pushOntoRamp(surface);
        }
        case 0x19: { // a ridge along z
            const int width = x1 - x0;
            const int baseY = (y0 * 2 - sdiv(boxWidth * (y1 - y0), width)) - y1;
            if (width / 2 + x0 < px) {
                surface = clampY0(sdiv((px - x0) * (y1 - baseY), width) + baseY);
                const int inset = sdiv((y1 - py) * width, y1 - baseY);
                if (0 < inset) st.hitX1 = wrap16(st.hitX1 - inset);
            } else {
                surface = clampY0(sdiv((x1 - px) * (y1 - baseY), width) + baseY);
                const int inset = sdiv((y1 - py) * width, y1 - baseY);
                if (0 < inset) st.hitX0 = wrap16(st.hitX0 + inset);
            }
            pushFlags = kPushStrictX;
            return pushOntoRamp(surface);
        }
        case 0x1a: { // a pyramid: the face of the quarter the point is in
            const int baseY = y0 * 2 - y1, depth = z1 - z0, width = x1 - x0;
            int quarter = 0;
            if (px < sdiv(width * (pz - z0), depth) + x0) quarter = 2;
            if (px < sdiv(width * (z1 - pz), depth) + x0) quarter |= 1;
            switch (quarter) {
            case 0: {
                surface = sdiv((px - x0) * (y1 - baseY), width) + baseY;
                const int inset = sdiv((y1 - py) * width, y1 - baseY);
                if (0 < inset) st.hitX1 = wrap16(st.hitX1 - inset);
                break;
            }
            case 1: {
                surface = sdiv((z1 - pz) * (y1 - baseY), depth) + baseY;
                const int inset = sdiv((y1 - py) * depth, y1 - baseY);
                if (0 < inset) st.hitZ0 = wrap16(st.hitZ0 + inset);
                break;
            }
            case 2: {
                surface = sdiv((pz - z0) * (y1 - baseY), depth) + baseY;
                const int inset = sdiv((y1 - py) * depth, y1 - baseY);
                if (0 < inset) st.hitZ1 = wrap16(st.hitZ1 - inset);
                break;
            }
            default: {
                surface = sdiv((x1 - px) * (y1 - baseY), width) + baseY;
                const int inset = sdiv((y1 - py) * width, y1 - baseY);
                if (0 < inset) st.hitX0 = wrap16(st.hitX0 + inset);
                break;
            }
            }
            pushFlags = kPushSidesOnly;
            return pushOntoRamp(surface);
        }
        case 0x20: case 0x30: case 0x40: case 0x50: return pushOutOfTriangle(kPushOnto);
        case 0x21: case 0x31: case 0x41: case 0x51: return pushOutOfTriangle(kPushSidesOnly);
        case 0x22: case 0x32: case 0x42: case 0x52: return pushOutOfTriangle(kPushMayY);
        case 0x27: case 0x37: case 0x47: case 0x57:
            return pushOutOfTriangle(prevInsideXZ() ? kPushWideY | kPushMayY : kPushOnto);
        // diagonal stairs: 0x14..0x17 directly, 0x23/24 33/34 43/44 53/54 after a hit-rect shrink
        case 0x23: case 0x24: case 0x33: case 0x34: case 0x43: case 0x44: case 0x53: case 0x54:
        case 0x14: case 0x15: case 0x16: case 0x17: {
            if (sh >= 0x20) {
                const int insetX = sdiv((y1 - py) * (x1 - x0), y1 - y0);
                const int insetZ = sdiv((y1 - py) * (z1 - z0), y1 - y0);
                const bool xMin = sh == 0x23 || sh == 0x24 || sh == 0x33 || sh == 0x34;
                const bool zMin = sh == 0x23 || sh == 0x24 || sh == 0x53 || sh == 0x54;
                if (0 < insetX) {
                    if (xMin) st.hitX0 = wrap16(st.hitX0 + insetX);
                    else st.hitX1 = wrap16(st.hitX1 - insetX);
                }
                if (0 < insetZ) {
                    if (zMin) st.hitZ0 = wrap16(st.hitZ0 + insetZ);
                    else st.hitZ1 = wrap16(st.hitZ1 - insetZ);
                }
            }
            const int width = x1 - x0;
            const int base = y0 - sdiv(boxWidth * (y1 - y0), width);
            const int family = sh >= 0x20 ? (sh >> 4) : 0; // 2 = 0x16, 3 = 0x17, 4 = 0x14, 5 = 0x15
            const int kind = sh >= 0x20 ? (family == 2 ? 0x16 : family == 3 ? 0x17 : family == 4 ? 0x14 : 0x15) : sh;
            const bool down = (sh & 0xf) == 3; // 0x23 / 0x33 / 0x43 / 0x53: slope down from the max
            int rise;
            switch (kind) {
            case 0x16:
                if (!down) {
                    rise = sdiv((((boxMinX - px) - pz) + z1 + x1) * (y1 - base), width);
                    surface = clampY0(rise + base);
                } else {
                    rise = sdiv((((z1 - px) - pz) + x1) * (y1 - base), width);
                    surface = clampY0(y1 - rise);
                }
                break;
            case 0x17:
                if (!down) {
                    rise = sdiv(((((pz - px) - z0) + boxMinX) + x1) * (y1 - base), width);
                    surface = clampY0(rise + base);
                } else {
                    rise = sdiv((((pz - px) - z0) + x1) * (y1 - base), width);
                    surface = clampY0(y1 - rise);
                }
                break;
            case 0x14:
                if (!down) {
                    rise = sdiv(((((px - z0) + pz) - x0) + boxMinX) * (y1 - base), width);
                    surface = clampY0(rise + base);
                } else {
                    rise = sdiv((((px - z0) + pz) - x0) * (y1 - base), width);
                    surface = clampY0(y1 - rise);
                }
                break;
            default: // 0x15
                if (!down) {
                    surface = clampY0(sdiv((((px - pz) - x0) + boxMinX + z1) * (y1 - base), width) + base);
                } else {
                    surface = clampY0(y1 - sdiv((((px - pz) - x0) + z1) * (y1 - base), width));
                }
                break;
            }
            pushFlags = kPushStrictX | kPushStrictZ;
            return pushOntoRamp(surface);
        }
        default:
            return 0;
        }
    }
};

// the cell word holds a segment's volumes (not a floor cell)
bool segmentVolumesCell(uint32_t w) { return (w & (kCellFloor | kCellSegment)) == kCellSegment; }
// the cell is a special one and the mover's layers are stopped by it
bool specialFor(uint32_t w, uint32_t mask) { return (w & kCellSpecial) && (mask & kSpecialLayers); }


} // namespace

void CollisionState::applyPush() {
    point[0] = wrap16(point[0] + appliedPushX);
    point[1] = wrap16(point[1] + appliedPushY);
    point[2] = wrap16(point[2] + appliedPushZ);
    appliedPushX = pendingPush[0];
    appliedPushY = pendingPush[1];
    appliedPushZ = pendingPush[2];
    pendingPush[0] = 0;
    pendingPush[1] = 0;
    pendingPush[2] = 0;
}

CollisionRoom collisionRoom(FileDataBlock block, const RoomGrid& grid, size_t origin) {
    const RoomBlockHeaderT<BlockStorage> header(block.data() + origin);
    CollisionRoom r;
    r.block = std::move(block);
    r.origin = origin;
    r.pool = size_t(uint32_t(header.pool)) * 4;
    r.segments = size_t(uint32_t(header.segments)) * 4;
    r.cells = grid.cellsOffset;
    r.x0 = grid.x0;
    r.z0 = grid.z0;
    r.w = grid.width();
    r.h = grid.z1 - grid.z0 + 1;
    return r;
}

int32_t probeGround(const CollisionRoom& room, CollisionState& st, int16_t x, int16_t y, int16_t z,
                    const int16_t* box, int mode, uint32_t mask) {
    Query q{room, st};
    st.noStepUp = 0;
    st.lastPushAxis = 0;
    st.point[0] = x;
    st.point[1] = y;
    st.point[2] = z;
    const Query::Gathered gt = q.gather();
    st.setBox(box);
    int best = kNoSurface;
    for (;;) {
        const int prev = best;
        for (int i = gt.n - 1; i >= 0; --i) {
            const size_t cell = gt.cell[i];
            const uint16_t w = q.word(cell);
            bool vols = segmentVolumesCell(w);
            if (!vols) {
                if (!specialFor(w, mask)) {
                    if (q.cellAt(st.point[0], st.point[2]) == cell) {
                        const int fh = q.floorHeight(cell);
                        if (fh < best) best = fh;
                    }
                } else {
                    int16_t r[6] = {};
                    specialRect(gt.special[i], r);
                    if (q.shapeTest(r, 0)) return kInsideSpecial;
                }
                vols = !(w & kCellFloor);
            }
            if (!vols) continue;
            const SegmentVolumes sv = q.volumes(w);
            const uint32_t count = sv.count == 0 ? 1 : sv.count; // a do-while in the exe
            for (uint32_t k = 0; k < count; ++k) {
                const VolumeRecord vol = q.volume(sv.first + k * kVolumeSize);
                const uint32_t shape = vol.shape;
                if (shape >= kShapeTrigger || !(mask & vol.mask)) continue;
                int16_t r[6] = {};
                volumeRect(vol, sv.anchor, r);
                if (!q.shapeTest(r, int(shape))) continue;
                volumeHeights(vol, sv.anchor, r);
                q.adjustSlope(r, uint8_t(shape));
                const int py = st.point[1];
                if (mode < 0) {
                    if (r[kRectY0] < best && py < r[kRectY1]) best = st.box[kBoxMaxY] + r[kRectY0];
                } else if (r[kRectY0] < best) {
                    const int t = py < best ? py : best;
                    if (t < r[kRectY1]) best = st.box[kBoxMaxY] + r[kRectY0];
                }
            }
        }
        if (best == prev) return best;
    }
}

uint32_t testMove(const CollisionRoom& room, CollisionState& st, const std::array<int16_t, 3>& a,
                  const std::array<int16_t, 3>& b, const int16_t* box, uint32_t mask) {
    // a point this far from the centre whose move crosses an axis left the map
    constexpr int kMapEdge = 0x4000;
    Query q{room, st};
    st.noStepUp = 0;
    st.lastPushAxis = 0;
    st.point[0] = a[0];
    st.point[1] = a[1];
    st.point[2] = a[2];
    st.previous[0] = b[0];
    st.previous[1] = b[1];
    st.previous[2] = b[2];
    const int px = a[0], pz = a[2];
    if ((a[0] ^ b[0]) & 0x8000) {
        if (iabs(px) > kMapEdge) return kMoveOutsideMap;
    }
    if ((b[2] ^ a[2]) & 0x8000) {
        if (iabs(pz) > kMapEdge) return kMoveOutsideMap;
    }
    if (!q.cellAt(a[0], a[2])) return kMoveNoCell;
    const Query::Gathered gt = q.gather();
    st.setBox(box);
    for (int i = gt.n - 1; i >= 0; --i) {
        const size_t cell = gt.cell[i];
        const uint16_t w = q.word(cell);
        bool vols = segmentVolumesCell(w);
        if (!vols) {
            if (!specialFor(w, mask)) {
                if (q.cellAt(a[0], a[2]) == cell && q.floorHeight(cell) - st.box[kBoxMaxY] < a[1]) {
                    st.hitShape = kHitShapeFloor;
                    return w | kMoveBlocked;
                }
            } else {
                int16_t r[6] = {};
                specialRect(gt.special[i], r);
                if (q.shapeTest(r, 0)) return w | kMoveBlocked;
            }
            vols = !(w & kCellFloor);
        }
        if (!vols) continue;
        const SegmentVolumes sv = q.volumes(w);
        const uint32_t count = sv.count == 0 ? 1 : sv.count;
        for (uint32_t k = 0; k < count; ++k) {
            const VolumeRecord vol = q.volume(sv.first + k * kVolumeSize);
            const uint32_t shape = vol.shape;
            if (shape >= kShapeTrigger || !(mask & vol.mask)) continue;
            int16_t r[6] = {};
            volumeRect(vol, sv.anchor, r);
            if (!q.shapeTest(r, int(shape))) continue;
            volumeHeights(vol, sv.anchor, r);
            q.adjustSlope(r, uint8_t(shape));
            if (r[kRectY0] < a[1] && a[1] < r[kRectY1]) {
                st.hitShape = int16_t(uint16_t(vol.shape));
                return kMoveBlocked | ((uint32_t(vol.mask) & kVolumeLayerHigh) << 12) | w;
            }
        }
    }
    return 0;
}

uint32_t resolveMove(const CollisionRoom& room, CollisionState& st, const std::array<int16_t, 3>& pos,
                     const std::array<int16_t, 3>& prev, uint8_t airborne, uint32_t mask) {
    Query q{room, st};
    st.noStepUp = 0;
    st.lastPushAxis = 0;
    st.point[0] = pos[0];
    st.point[1] = pos[1];
    st.point[2] = pos[2];
    st.previous[0] = prev[0];
    st.previous[1] = prev[1];
    st.previous[2] = prev[2];
    st.airborne = airborne;
    st.clearPush();
    int passes = 0;
    const Query::Gathered gt = q.gather(); // the cells of the first point, reused by every pass
    // The exe also goes again when no cell was gathered but the last test hit: unreachable, as nothing is tested
    // without a cell (only the cell loop sets a hit), so that branch is not transcribed.
    for (;;) {
        bool moved = false;
        for (int i = gt.n - 1; i >= 0 && !moved; --i) {
            for (int axis = 0; axis < 3; ++axis) st.delta[axis] = wrap16(st.point[axis] - st.previous[axis]);
            const size_t cell = gt.cell[i];
            const uint32_t w = uint32_t(int32_t(int16_t(q.word(cell))));
            bool vols = segmentVolumesCell(w);
            if (!vols) {
                if (!specialFor(w, mask)) {
                    if (q.cellAt(st.point[0], st.point[2]) == cell) {
                        const int fh = q.floorHeight(cell);
                        if (fh - st.box[kBoxMaxY] < st.point[1]) { // stand on the floor
                            st.appliedPushX = 0;
                            st.appliedPushY = 0;
                            st.appliedPushZ = 0;
                            st.point[1] = wrap16(fh - st.box[kBoxMaxY]);
                            if (0 < st.delta[1]) st.airborne = 0;
                            moved = true;
                            break;
                        }
                    }
                } else {
                    int16_t r[6] = {};
                    specialRect(gt.special[i], r);
                    const int hit = q.shapeTest(r, 0);
                    if (hit) {
                        r[kRectY0] = int16_t(-kSpecialBoxReach); // the special box spans every height
                        r[kRectY1] = kSpecialBoxReach;
                        if (q.resolveVolume(r, kSpecialBoxShape, hit)) {
                            moved = true;
                            break;
                        }
                    }
                }
                vols = !(w & kCellFloor);
            }
            if (!vols) continue;
            const SegmentVolumes sv = q.volumes(uint16_t(w));
            const uint32_t count = sv.count == 0 ? 1 : sv.count;
            for (uint32_t k = 0; k < count; ++k) {
                const VolumeRecord vol = q.volume(sv.first + k * kVolumeSize);
                const uint32_t shape = vol.shape;
                if (shape >= kShapeTrigger || !(mask & vol.mask)) continue;
                int16_t r[6] = {};
                volumeRect(vol, sv.anchor, r);
                const int hit = q.shapeTest(r, int(shape));
                if (!hit) continue;
                volumeHeights(vol, sv.anchor, r);
                if (q.resolveVolume(r, shape, hit)) {
                    moved = true;
                    break;
                }
            }
        }
        if (!moved) break;
        // apply the accumulated push and go again
        st.applyPush();
        const bool giveUp = kMaxPushPasses - 2 < passes;
        ++passes;
        if (giveUp) return kResolveGaveUp;
    }
    // which way was the point pushed
    const int dx = pos[0] - st.point[0], dy = pos[1] - st.point[1], dz = pos[2] - st.point[2];
    if (dz == 0 && dy == 0 && dx == 0) return 0;
    uint32_t u;
    if (iabs(dz) < iabs(dx)) {
        if (iabs(dy) < iabs(dx)) u = dx < 1 ? kPushAxisXMinus : kPushAxisXPlus;
        else u = dy < 1 ? kPushAxisYMinus : kPushAxisYPlus;
    } else {
        if (iabs(dy) < iabs(dz)) u = dz < 1 ? kPushAxisZMinus : kPushAxisZPlus;
        else u = dy < 1 ? kPushAxisYMinus : kPushAxisYPlus;
    }
    if (dx != 0) u |= dx < 1 ? kWallOnXMinus : kWallOnXPlus;
    if (dy != 0) u |= dy < 1 ? kWallOnYMinus : kWallOnYPlus;
    if (dz == 0) return u;
    return u | (dz > 0 ? kWallOnZPlus : kWallOnZMinus);
}

uint32_t wallQuery(const CollisionRoom& room, CollisionState& st, const std::array<int16_t, 3>& pos,
                   const int16_t* box, int yaw, uint32_t mask) {
    Query q{room, st};
    st.noStepUp = 0;
    st.lastPushAxis = 0;
    st.point[0] = pos[0];
    st.point[1] = pos[1];
    st.point[2] = pos[2];
    int bestZ = kNoSurface, bestX = kNoSurface;
    const Query::Gathered gt = q.gather();
    st.setBox(box);
    auto atan = [](int a, int b) { // exe 0x40b960 (engine/x87)
        return port::x87::atanAngle(port::atanScale(), float(a), float(b)); // the arctangent scale (game/angles)
    };
    // the travel direction's quadrant: 0 = -z ... (the wall directions 0, 0x400, 0x800, 0xc00)
    const int dir = ((yaw + port::kQuarterTurn / 2) >> 10) & 3;
    for (int i = gt.n - 1; i >= 0; --i) {
        const uint16_t w = q.word(gt.cell[i]);
        if (w & kCellFloor) continue; // floor cells have no volumes
        const SegmentVolumes sv = q.volumes(w);
        const uint32_t count = sv.count == 0 ? 1 : sv.count;
        for (uint32_t k = 0; k < count; ++k) {
            const VolumeRecord vol = q.volume(sv.first + k * kVolumeSize);
            const uint32_t word = vol.word;
            if (vol.shape >= kShapeTrigger || !(vol.mask & mask)) continue;
            int16_t r[6] = {};
            volumeRect(vol, sv.anchor, r);
            const int test = int((word >> kShapeTestShift) & 0xf);
            const int px = st.point[0], pz = st.point[2];
            if (test < 2) { // boxes: walls facing the travel direction
                const bool inside = (dir & 1) == 0 ? (r[kRectX0] < px && px < r[kRectX1]) : (r[kRectZ0] < pz && pz < r[kRectZ1]);
                if (!inside) continue;
                // exe 0x410040 passes the whole volume word (mask << 16 | shape): the 0x1c gate never matches
                // and the triangle tests always take their margin variant (signed compare with 0x25 etc.)
                q.shapeTest(r, int(word)); // result ignored: only shrinks r by the box
                volumeHeights(vol, sv.anchor, r);
                q.adjustSlope(r, uint8_t(word), 1);
                const int py = st.point[1];
                if (!(r[kRectY0] < py && py < r[kRectY1])) continue;
                switch (dir) {
                case 0: {
                    const int d = r[kRectZ1] - pz;
                    if (iabs(d) < iabs(bestZ) && r[kRectZ0] < pz) { st.wallDirection = 0; bestZ = d; }
                    break;
                }
                case 1: {
                    const int d = r[kRectX1] - px;
                    if (iabs(d) < iabs(bestX) && r[kRectX0] < px) { st.wallDirection = port::kQuarterTurn; bestX = d; }
                    break;
                }
                case 2: {
                    const int d = r[kRectZ0] - pz;
                    if (iabs(d) < iabs(bestZ) && pz < r[kRectZ1]) { st.wallDirection = port::kHalfTurn; bestZ = d; }
                    break;
                }
                default: {
                    const int d = r[kRectX0] - px;
                    if (iabs(d) < iabs(bestX) && px < r[kRectX1]) {
                        st.wallDirection = port::kHalfTurn + port::kQuarterTurn;
                        bestX = d;
                    }
                    break;
                }
                }
                continue;
            }
            // triangles: the hypotenuse facing the travel direction
            if (!(r[kRectX0] < px && px < r[kRectX1] && r[kRectZ0] < pz && pz < r[kRectZ1])) continue;
            const bool odd = (test & 1) != 0;
            const int hypotenuse = odd ? atan(r[kRectX1] - r[kRectX0], r[kRectZ1] - r[kRectZ0])
                                       : atan(r[kRectX0] - r[kRectX1], r[kRectZ1] - r[kRectZ0]) + port::kQuarterTurn;
            if (test != (((yaw - hypotenuse - kTriangleFacingBias) >> 10) & 3) + 2) continue;
            q.shapeTest(r, int(word)); // the whole word, see above
            volumeHeights(vol, sv.anchor, r);
            q.adjustSlope(r, uint8_t(word), 1);
            const int py = st.point[1];
            if (!(r[kRectY0] < py && py < r[kRectY1])) continue;
            int hx, hz; // the nearest point on the hypotenuse along x / z
            if (odd) {
                hx = sdiv((pz - r[kRectZ0]) * (r[kRectX1] - r[kRectX0]), r[kRectZ1] - r[kRectZ0]) + r[kRectX0];
                hz = sdiv((px - r[kRectX0]) * (r[kRectZ1] - r[kRectZ0]), r[kRectX1] - r[kRectX0]) + r[kRectZ0];
            } else {
                hx = sdiv((pz - r[kRectZ1]) * (r[kRectX1] - r[kRectX0]), r[kRectZ0] - r[kRectZ1]) + r[kRectX0];
                hz = sdiv((px - r[kRectX1]) * (r[kRectZ1] - r[kRectZ0]), r[kRectX0] - r[kRectX1]) + r[kRectZ0];
            }
            const int ax = iabs(hx - px), az = iabs(hz - pz);
            int dx = sdiv((hx - px) * az, ax + az);
            int dz = sdiv((hz - pz) * ax, az + ax);
            const int minX = box ? box[kBoxMinX] : 0, maxX = box ? box[kBoxMaxX] : 0, minZ = box ? box[kBoxMinZ] : 0,
                      maxZ = box ? box[kBoxMaxZ] : 0;
            if (dx < 1) { dx -= minX; if (dx > 0) dx = 0; }
            else { dx -= maxX; if (dx < 0) dx = 0; }
            if (dz < 1) { dz -= minZ; if (dz > 0) dz = 0; }
            else { dz -= maxZ; if (dz < 0) dz = 0; }
            if (iabs(dx) < iabs(bestX)) {
                st.wallDirection = uint16_t(((test << 10) + hypotenuse) & port::kAngleMask);
                bestX = dx;
            }
            if (iabs(dz) < iabs(bestZ)) {
                st.wallDirection = uint16_t(((test << 10) + hypotenuse) & port::kAngleMask);
                bestZ = dz;
            }
        }
    }
    return (uint32_t(bestZ) << 16) | (uint32_t(bestX) & 0xffff);
}

uint32_t triggerScan(const CollisionRoom& room, CollisionState& st, const std::array<int16_t, 3>& pos, int16_t actorYaw,
                     int16_t playerYaw, TriggerHit& out) {
    // how far ahead of the actor the kTriggerAhead volumes are tested
    constexpr int kAheadDistance = 0x40;
    Query q{room, st};
    // the probe point ahead: Ry(yaw) * (0, 0, -kAheadDistance) (exe 0x40bea0 + 0x40bb30, shorts); the sine / cosine
    // from libm, not the exe table (an approximation)
    const int a = actorYaw & port::kAngleMask;
    const int sn = int(std::lround(4096.0 * std::sin(a * 6.283185307179586 / 4096.0)));
    const int cs = int(std::lround(4096.0 * std::cos(a * 6.283185307179586 / 4096.0)));
    const int16_t ahead[3] = {wrap16(pos[0] + wrap16((sn * -kAheadDistance) >> 12)), pos[1],
                              wrap16(pos[2] + wrap16((cs * -kAheadDistance) >> 12))};
    st.point[0] = pos[0];
    st.point[1] = pos[1];
    st.point[2] = pos[2];
    const Query::Gathered gt = q.gather();
    static const int16_t kProbeBox[6] = {-1, 1, -1, 1, -1, 1}; // the exe's 0x1ffff x 3
    st.setBox(kProbeBox);
    for (int i = gt.n - 1; i >= 0; --i) {
        const uint16_t w = q.word(gt.cell[i]);
        if (w & kCellFloor) continue;
        const SegmentVolumes sv = q.volumes(w);
        const uint32_t count = sv.count == 0 ? 1 : sv.count;
        for (uint32_t k = 0; k < count; ++k) {
            const VolumeRecord vol = q.volume(sv.first + k * kVolumeSize);
            const uint16_t shape = vol.shape, flags = vol.mask;
            if (shape < kShapeTrigger) continue;
            const int16_t* pt = (flags & kTriggerAhead) ? ahead : pos.data();
            st.point[0] = pt[0];
            st.point[1] = pt[1];
            st.point[2] = pt[2];
            int16_t r[6] = {};
            volumeRect(vol, sv.anchor, r);
            if (!q.shapeTest(r, shape)) continue;
            volumeHeights(vol, sv.anchor, r);
            q.adjustSlope(r, uint8_t(shape), 0);
            const int py = st.point[1];
            if (!(r[kRectY0] < py && py < r[kRectY1])) continue;
            if ((shape >> 8) == kTriggerFacingType && (flags & kTriggerFacing) &&
                (((int(playerYaw) + port::kQuarterTurn / 2) >> 10) & 3) != ((flags >> 8) & 3))
                continue;
            out.x = wrap16((vol.x1 + vol.x0) / 2 + sv.anchor[0]);
            out.y = wrap16((vol.y1 + vol.y0) / 2 + sv.anchor[1]);
            out.z = wrap16((vol.z1 + vol.z0) / 2 + sv.anchor[2]);
            out.flags = flags;
            out.type = uint16_t(shape >> 8);
            out.yaw = uint16_t(actorYaw & port::kAngleMask);
            return 1;
        }
    }
    out.type = 0;
    out.flags = 0;
    return 0;
}

bool roomHasTrigger(const CollisionRoom& room, uint16_t type) {
    CollisionState st;
    Query q{room, st};
    for (int n = 0; n < room.w * room.h; ++n) {
        const uint16_t w = q.word(q.cellOffset(n));
        if (int16_t(w) >= 1 || (w & kCellFloor)) continue; // the cells triggerScan reads: colliding, not floor
        const SegmentVolumes sv = q.volumes(w);
        const uint32_t count = sv.count == 0 ? 1 : sv.count;
        for (uint32_t k = 0; k < count; ++k) {
            const uint16_t shape = q.volume(sv.first + k * kVolumeSize).shape;
            if (shape >= kShapeTrigger && (shape >> 8) == type) return true;
        }
    }
    return false;
}

// the volume primitives never touch the grid: an empty room satisfies Query
int volumeShapeTest(CollisionState& st, int16_t r[6], int shape) {
    static const CollisionRoom none;
    Query q{none, st};
    return q.shapeTest(r, shape);
}
void volumeAdjustSlope(CollisionState& st, int16_t r[6], uint8_t shape) {
    static const CollisionRoom none;
    Query q{none, st};
    q.adjustSlope(r, shape);
}
uint32_t volumeResolve(CollisionState& st, const int16_t r[6], uint32_t shape, int testResult) {
    static const CollisionRoom none;
    Query q{none, st};
    return q.resolveVolume(r, shape, testResult);
}

} // namespace od
