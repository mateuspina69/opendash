// The active room's collision grid as the game state holds it (the room block's copy and its pointers 0x6d0080..
// 0x6d0099): the cell lookups the actors and scripts make (exe 0x40f030 / 0x40f0b0 / 0x40f120) and the switches the
// scripts flip on it -- a segment's alternate volume set and door list (exe 0x600f10 / 0x600f50), a floor cell's
// segment number and variant (exe 0x600f80). The records and the grid's geometry are game/room_records (the collision
// queries read the same records from the block's bytes);
#pragma once

#include <cstdint>

#include "game/loaded_data.hpp"
#include "game/room_records.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port {

// the grid's globals
struct RoomGridState {
    uint32_t block = 0;     // the room block (0x6d0080: its header's dword offsets of the pool, the segments, the grids
                            // and the minimap's layers)
    uint32_t pool = 0;      // the segment pool (its header first; its address: the record's identity)
    uint32_t cells = 0;     // the cells (row by row)
    uint32_t segments = 0;  // the segment list (its header first)
    uint8_t originX = 0;    // the grid's first column / row (cell units + kCellBias)
    uint8_t originZ = 0;
    uint8_t lastX = 0;      // its last column / row (the grid header's; the town minimaps read them)
    uint8_t lastZ = 0;
    uint8_t width = 0;      // its columns / rows
    uint8_t depth = 0;
    uint8_t roomFlags = 0;  // the room record's +7: kRoomSwitchableSegments

    // the grid's placement
    GridBounds bounds() const { return {originX, originZ, width, depth}; }
    // the placement's six bytes (the origin, the last column / row, the size) cleared (the boot)
    void clearPlacementBytes() {
        originX = 0;
        originZ = 0;
        lastX = 0;
        lastZ = 0;
        width = 0;
        depth = 0;
    }
};
constexpr uint8_t kRoomSwitchableSegments = 0x80; // the room's segments switch (breakable walls, segment variants)
inline RoomGridState& roomGrid() {
    static RoomGridState s;
    return s;
}

// the records in the game state
using GridCell = GridCellT<GameStorage>;
using SegmentEntry = SegmentEntryT<GameStorage>;
using SegmentPoolEntry = SegmentPoolEntryT<GameStorage>;
using SegmentListHeader = SegmentListHeaderT<GameStorage>;
using BreakableWord = BreakableWordT<GameStorage>;
using VolumeSet = VolumeSetT<GameStorage>;

// A segment's texture list in the room pool (at the dword offset its volume set gives): its faces, the texture words
// the faces select (CLUT | tpage << 16) and, kListHeaderSize + firstWord * 4 bytes in, the segment's face lists
struct TextureList {
    explicit TextureList(uint32_t pointer) : at(pointer), data(loadedFile(pointer)) {}
    uint32_t at;
    FileDataReader data;
    uint8_t faces{data.read<uint8_t>(0)};
    uint8_t firstWord{data.read<uint8_t>(1)};
    uint32_t wordAddress(uint32_t i) const { return at + kListHeaderSize + i * 4; }
    uint32_t word(uint32_t i) const { return data.read<uint32_t>(kListHeaderSize + i * 4); }
};
// One face list of a segment: offsets of its vertices, faces, texels and shades.
struct FaceList {
    explicit FaceList(uint32_t pointer) : data(loadedFile(pointer)) {}
    FileDataReader data;
    uint16_t vertices{data.read<uint16_t>(0)};
    uint16_t faces{data.read<uint16_t>(2)};
    uint16_t texels{data.read<uint16_t>(4)};
    uint16_t shades{data.read<uint16_t>(6)};
};
constexpr uint32_t kFaceListSize = 8;

// the entry of segment `segment` (its index: & kCellSegmentMask) and the pool entry `poolIndex`
SegmentEntry segmentEntry(uint32_t segment);
SegmentPoolEntry segmentPoolEntry(uint32_t poolIndex);

// the cell's address in the game state (0: outside the grid) --
// exe 0x40f030 / 0x40f0b0: of a point's integer parts (the 16.16 position's high words / an s16 point)
uint32_t gridCellOf(int16_t x, int16_t z);
uint32_t gridCellUnder(const Fixed16Vec3& pos);
// exe 0x40f120: of an integer point
uint32_t gridCellAtPoint(int32_t x, int32_t z);
// the cell at column / row (cell units + kCellBias) of the grid, unchecked (the exe's inlined index)
uint32_t gridCellAtIndex(uint32_t column, uint32_t row);

// A box of grid cells (exe data, 4 s8): its first column / row and its columns / rows, in cells of 512 units (unbiased)
struct CellBox {
    // the box at a pointer of the data
    explicit CellBox(uint32_t pointer) {
        const DataReader r = dataAt(pointer, 4);
        column = r.s8(0);
        row = r.s8(1);
        columns = r.s8(2);
        rows = r.s8(3);
    }
    int8_t column = 0, row = 0, columns = 0, rows = 0;
};
// exe 0x5e5af0(pos, box): the point's 512-unit cell (its integer parts >> 9) inside the box (unsigned compares)
bool pointInCellBox(const Fixed16Vec3& pos, const CellBox& box);
// ... the box given by value
bool pointInCellBox(const Fixed16Vec3& pos, int column, int row, int columns, int rows);
// ... of a point's integer parts (the exe's stack point)
struct CellBoxValues {
    int column, row, columns, rows;
};
bool pointInCellBox(int16_t x, int16_t z, const CellBox& box);
bool pointInCellBox(int16_t x, int16_t z, const CellBoxValues& box);
// exe 0x5e6240(pos): the ground kind under the point (integer parts) -- its floor cell's attribute (& 0x1f) through the
// stage's ground-kind table (StageState::groundKinds); -1 without a table or off a floor cell (the cell unchecked)
int32_t groundKindAt(int16_t x, int16_t z);
int32_t groundKindUnder(const Fixed16Vec3& pos);

// exe 0x600f10(segment, on): the segment's alternate volume set on / off, when its pool entry has one
void segmentSetAltVolumes(uint32_t segment, bool on);
// exe 0x600f50(segment, on): the segment's door list hidden / shown
void segmentSetDoorsHidden(uint32_t segment, bool on);
// exe 0x600f80(column, row, number, variant): a floor cell's (kCellCollides and kCellFloor) segment number
// (number >= 0) and variant (variant >= 0) rewritten
void cellSetSegment(uint32_t column, uint32_t row, int32_t number, int32_t variant);

} // namespace od::port
