// The active room's collision grid in the game state: cell lookups and the scripts' switches (see room_grid.hpp).
#include "game/room_grid.hpp"

#include "game/stage_state.hpp"
#include "game/vectors.hpp"

namespace od::port {

namespace {

// the stage's ground-kind table: one byte per floor attribute (a floor cell word's low bits)
constexpr size_t kGroundKinds = 0x20;
constexpr uint16_t kGroundKindMask = 0x1f;

// the cell of grid column ix / row iz, 0 outside the grid
uint32_t cellOfColumn(int ix, int iz) {
    const RoomGridState& g = roomGrid();
    const int n = gridCellNumber(g.bounds(), ix, iz);
    return n < 0 ? 0 : g.cells + uint32_t(n) * kGridCellSize;
}

} // namespace

SegmentEntry segmentEntry(uint32_t segment) {
    return SegmentEntry(roomGrid().segments + kListHeaderSize + (segment & kCellSegmentMask) * kSegmentEntrySize);
}

SegmentPoolEntry segmentPoolEntry(uint32_t poolIndex) {
    return SegmentPoolEntry(roomGrid().pool + kListHeaderSize + poolIndex * kPoolEntrySize);
}

uint32_t gridCellOf(int16_t x, int16_t z) { return cellOfColumn(gridColumn(x), gridColumn(z)); }

uint32_t gridCellUnder(const Fixed16Vec3& pos) { return gridCellOf(pos.x.whole, pos.z.whole); }

uint32_t gridCellAtPoint(int32_t x, int32_t z) { return cellOfColumn(gridColumn(x), gridColumn(z)); }

uint32_t gridCellAtIndex(uint32_t column, uint32_t row) {
    const RoomGridState& g = roomGrid();
    return g.cells + gridCellNumberUnchecked(g.bounds(), column, row) * kGridCellSize;
}

bool pointInCellBox(const Fixed16Vec3& pos, int column, int row, int columns, int rows) {
    return pointInCellBox(pos.x.whole, pos.z.whole, CellBoxValues{column, row, columns, rows});
}

bool pointInCellBox(const Fixed16Vec3& pos, const CellBox& box) {
    return pointInCellBox(pos, box.column, box.row, box.columns, box.rows);
}

bool pointInCellBox(int16_t x, int16_t z, const CellBox& box) {
    return pointInCellBox(x, z, CellBoxValues{box.column, box.row, box.columns, box.rows});
}

bool pointInCellBox(int16_t x, int16_t z, const CellBoxValues& box) {
    if (uint32_t(int(int16_t(x >> 9)) - box.column) >= uint32_t(box.columns)) return false;
    return uint32_t(int(int16_t(z >> 9)) - box.row) < uint32_t(box.rows);
}

int32_t groundKindAt(int16_t x, int16_t z) {
    const uint32_t table = stage().groundKinds;
    if (table == 0) return -1;
    const uint16_t word = GridCell(gridCellAtIndex(uint32_t(gridColumn(x)), uint32_t(gridColumn(z)))).word;
    if (!(word & kCellCollides) || !(word & kCellFloor)) return -1;
    return dataScalar<uint8_t>(table + (word & kGroundKindMask));
}

int32_t groundKindUnder(const Fixed16Vec3& pos) { return groundKindAt(pos.x.whole, pos.z.whole); }

void segmentSetAltVolumes(uint32_t segment, bool on) {
    auto&& e = segmentEntry(segment);
    const SegmentPoolEntry pool = segmentPoolEntry(e.poolIndex);
    if (!(pool.flags & kPoolHasAltVolumes)) return;
    if (on) e.flags |= kSegmentAltVolumes;
    else e.flags &= uint8_t(~kSegmentAltVolumes);
}

void segmentSetDoorsHidden(uint32_t segment, bool on) {
    auto&& e = segmentEntry(segment);
    if (on) e.flags |= kSegmentDoorsHidden;
    else e.flags &= uint8_t(~kSegmentDoorsHidden);
}

void cellSetSegment(uint32_t column, uint32_t row, int32_t number, int32_t variant) {
    auto&& cell = GridCell(gridCellAtIndex(column, row));
    const uint32_t bits = cell.head;
    if (!(bits & kCellCollides) || !(bits & kCellFloor)) return;
    if (number >= 0) cell.head = bits ^ ((bits ^ uint32_t(number)) & kCellSegmentNumberMask);
    if (variant >= 0) cell.head = ((uint32_t(variant) & 3) << kCellVariantShift) | (cell.head & ~kCellVariantMask);
}

} // namespace od::port
