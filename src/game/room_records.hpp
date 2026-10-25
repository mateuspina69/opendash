// The room block's collision records, one layout for both of their storages: the loaded room block (the stage
// loader's, followed through the grid pointers 0x6d0084..: the game's reads and the scripts' writes, game/room_grid)
// and a block's bytes (the host's offline room and the collision queries' reads, game/collision). Each record is
// declared once over a storage policy: `GameStorage` (fields of the loaded block, addresses) or `BlockStorage`
// (read-only little-endian fields over the block's bytes, pointers). The grid's geometry (the cell of a point) and
// its constants live here too.
#pragma once

#include <cstddef>
#include <cstdint>

#include "game/loaded_data.hpp"

namespace od {

// ---- the block's bytes -------------------------------------------------------------------------------------------

// the little-endian value of T's width at p
template <class T> T readLittleEndian(const uint8_t* p) {
    uint32_t v = 0;
    for (size_t k = 0; k < sizeof(T); ++k) v |= uint32_t(p[k]) << (8 * k);
    return T(v);
}

// A read-only field of a block's bytes (a record over BlockStorage)
template <class T> class BlockField {
public:
    explicit BlockField(const uint8_t* p) : p_(p) {}
    T get() const { return readLittleEndian<T>(p_); }
    operator T() const { return get(); }

private:
    const uint8_t* p_;
};

// the storages a record is declared over
struct GameStorage {
    using Address = uint32_t; // the record's address in the loaded room block
    template <class T> using Field = port::LoadedField<T>;
};
struct BlockStorage {
    using Address = const uint8_t*; // a byte of the block
    template <class T> using Field = BlockField<T>;
};

// ---- the grid ----------------------------------------------------------------------------------------------------

// the cell of a coordinate: (coordinate + kGridOrigin) >> kCellShift (cells of kCellSize units, the map centred on 0)
constexpr int kGridOrigin = 0x8000;
constexpr int kCellShift = 9;
constexpr int kCellSize = 0x200;
constexpr int kCellMask = kCellSize - 1;
// the grid columns / rows the records keep are + kCellBias (the segments' anchor cells, the grid's origin)
constexpr int kCellBias = 0x40;

// the record strides and the 4-byte headers of the segment pool and the segment list
constexpr uint32_t kGridCellSize = 12;
constexpr uint32_t kSegmentEntrySize = 8;
constexpr uint32_t kPoolEntrySize = 12;
constexpr uint32_t kListHeaderSize = 4;
constexpr uint32_t kVolumeSize = 16;
constexpr uint32_t kVolumeSetDwords = 3; // a pool entry's volume sets: 3 dwords each

// the grid column / row of a coordinate (exe 0x40f030 / 0x40f0b0 / 0x40f120)
constexpr int gridColumn(int coordinate) { return (coordinate + kGridOrigin) >> kCellShift; }
// The grid's placement: its first column / row and its size
struct GridBounds {
    int originX, originZ, width, depth;
};
// the cell number (row by row) of column ix / row iz, -1 outside the grid
constexpr int gridCellNumber(const GridBounds& g, int ix, int iz) {
    if (uint32_t(ix - g.originX) >= uint32_t(g.width) || uint32_t(iz - g.originZ) >= uint32_t(g.depth)) return -1;
    return (iz - g.originZ) * g.width + (ix - g.originX);
}
// ... unchecked (the exe's inlined index: a column / row the data names)
constexpr uint32_t gridCellNumberUnchecked(const GridBounds& g, uint32_t ix, uint32_t iz) {
    return (iz - uint32_t(g.originZ)) * uint32_t(g.width) - uint32_t(g.originX) + ix;
}

// ---- the records -------------------------------------------------------------------------------------------------

// A grid cell (12 bytes): its word (kCell* bits; the scenery segment index) and the heightmap corners
template <class S> struct GridCellT {
    template <class T> using F = typename S::template Field<T>;
    explicit GridCellT(typename S::Address a) : at(a) {}
    typename S::Address at;
    F<uint16_t> word{at + 0};
    F<uint32_t> head{at + 0};     // the word and the next 16 bits, as the exe's dword stores rewrite them
    F<uint32_t> corners{at + 4};  // the four corner heights (one byte each, x 16), corner 0 in the low byte
    F<uint8_t> corner0{at + 4};   // corner 0's height byte (a class's byte read of the cell's height)
    F<uint8_t> baseLevel{at + 6}; // corner 2's height byte: the level the cell's segment volumes stand on
};
// the cell word's bits
constexpr uint16_t kCellCollides = 0x8000;    // a collision cell (a cell word >= 1 as a short is none)
constexpr uint16_t kCellFloor = 0x4000;       // a heightmap floor cell
constexpr uint16_t kCellDiagonal = 0x2000;    // the floor's triangle split along the other diagonal
constexpr uint16_t kCellSegment = 0x0800;     // without kCellFloor: the cell holds a scenery segment's volumes
constexpr uint16_t kCellSpecial = 0x0400;     // a whole-cell box for the movers of the special layers (mask & 3)
constexpr uint16_t kCellSegmentMask = 0x07ff; // the scenery segment's index
// the bits exe 0x600f80 rewrites in a floor cell's word: its segment number (low 5 bits) and variant (bits 6..7)
constexpr uint32_t kCellSegmentNumberMask = 0x1f;
constexpr uint32_t kCellVariantShift = 6;
constexpr uint32_t kCellVariantMask = 0xc0;

// A scenery segment's entry (8 bytes after the list's header)
template <class S> struct SegmentEntryT {
    template <class T> using F = typename S::template Field<T>;
    explicit SegmentEntryT(typename S::Address a) : at(a) {}
    typename S::Address at;
    F<int16_t> hitPoints{at + 0};   // a breakable segment's hit points left (game/breakable_walls; < 0: broken)
    F<uint8_t> poolIndex{at + 2};   // its entry in the segment pool
    F<uint8_t> flags{at + 3};       // the volume set (& kSegmentVariantMask: kSegmentAltVolumes the alternate one),
                                    // kSegmentDoorsHidden
    F<uint16_t> heightWord{at + 4}; // (>> 4) & 0x7f0: the anchor's height
    F<uint8_t> cellX{at + 6};       // the anchor cell (grid units + kCellBias)
    F<uint8_t> cellZ{at + 7};
};
constexpr uint8_t kSegmentVariantMask = 0x03;
constexpr uint8_t kSegmentAltVolumes = 0x01;  // the pool entry's alternate volume set (an open door)
constexpr uint8_t kSegmentDoorsHidden = 0x04; // the segment's door list not drawn

// The segment list's header: its entries, and the breakable words that follow them (one per breakable segment, in
// segment order)
template <class S>
struct SegmentListHeaderT {
    template <class T>
    using F = typename S::template Field<T>;
    explicit SegmentListHeaderT(typename S::Address a) : at(a) {}
    typename S::Address at;
    F<uint16_t> segmentCount{at + 0};
    F<uint16_t> breakableCount{at + 2};
};
// A breakable segment's word (game/breakable_walls): the damage counter it adds into (kBreakSharedCounter: one of the
// counters several segments share, & kBreakCounterMask its index; else its own) and the damage pending this tick
template <class S>
struct BreakableWordT {
    template <class T>
    using F = typename S::template Field<T>;
    explicit BreakableWordT(typename S::Address a) : at(a) {}
    typename S::Address at;
    F<uint16_t> counter{at + 0};
    F<int16_t> pendingDamage{at + 2};

    // the pending damage cleared, the counter kept (the exe's dword read-modify-write)
    void clearPendingDamageWord() { word_ = uint32_t(word_) & 0xffffu; }

private:
    F<uint32_t> word_{at + 0};
};
constexpr uint16_t kBreakSharedCounter = 0x800;
constexpr uint16_t kBreakCounterMask = 0x7ff;
constexpr uint32_t kBreakableWordSize = 4;

// A segment pool entry (12 bytes after the pool's header)
template <class S> struct SegmentPoolEntryT {
    template <class T> using F = typename S::template Field<T>;
    explicit SegmentPoolEntryT(typename S::Address a) : at(a) {}
    typename S::Address at;
    F<int16_t> hitPoints{at + 0};    // a breakable segment's full hit points (> 0: breakable)
    F<uint8_t> reaction{at + 2};     // & 0xf the reaction to a break step, >> 4 the wall crumble's variant
    F<uint8_t> breakLevel{at + 3};   // & 3: the attack level (the queued attack's bits 16..17) that reaches it
    F<uint32_t> flags{at + 4};       // kPoolHalfCellX / Z, kPoolHasAltVolumes; bytes 0..2 the breakable anchor's
                                     // offsets, byte 3 the break flags (below)
    F<uint16_t> volumeSets{at + 8};  // its first volume set's index (dwords in the pool)
    F<uint8_t> breakWidth{at + 0xa}; // a breakable segment's width / height (<< its size shift)
    F<uint8_t> breakHeight{at + 0xb};

    // the breakable anchor's offsets x / y / z (s8, << the size shift) and the break flags: & kBreakStepsMask the
    // steps of its breaking (0: not breakable), >> kBreakSizeShift & 3 the size shift - 2, >> 4 & 3 the crumble's
    // blasts
    int8_t offsetX() const { return int8_t(uint8_t(uint32_t(flags))); }
    int8_t offsetY() const { return int8_t(uint8_t(uint32_t(flags) >> 8)); }
    int8_t offsetZ() const { return int8_t(uint8_t(uint32_t(flags) >> 16)); }
    uint8_t breakFlags() const { return uint8_t(uint32_t(flags) >> 24); }
};
constexpr uint8_t kSegmentReactionMask = 0x0f; // a pool entry's reaction (its low nibble)
constexpr uint8_t kBreakStepsMask = 0x03;
constexpr uint32_t kBreakSizeShift = 2;
constexpr uint32_t kBreakBlastsShift = 4;
constexpr uint32_t kPoolHasAltVolumes = 0x01000000; // the entry has an alternate volume set (the flags' top byte bit 0)
constexpr uint32_t kPoolHalfCellX = 0x10000000;     // the anchor half a cell further along x
constexpr uint32_t kPoolHalfCellZ = 0x20000000;     // ... along z

// A volume set at the pool's dword `index` (kVolumeSetDwords dwords): the texture list, the volume list and its count
template <class S> struct VolumeSetT {
    template <class T> using F = typename S::template Field<T>;
    explicit VolumeSetT(typename S::Address a) : at(a) {}
    typename S::Address at;
    F<uint16_t> textureList{at + 0}; // the segment's texture list's offset in the pool (dwords; the wall debris' faces)
    F<uint16_t> volumeList{at + 2}; // the list's offset in the pool (dwords)
    F<uint8_t> volumeCount{at + 6};
};

// A collision volume (16 bytes): its x / z / y spans from the segment's anchor, its shape and its layer mask
template <class S> struct VolumeT {
    template <class T> using F = typename S::template Field<T>;
    explicit VolumeT(typename S::Address a) : at(a) {}
    typename S::Address at;
    F<int16_t> x0{at + 0};
    F<int16_t> x1{at + 2};
    F<int16_t> z0{at + 4};
    F<int16_t> z1{at + 6};
    F<int16_t> y0{at + 8};   // the top (y down)
    F<int16_t> y1{at + 10};  // the bottom
    F<uint16_t> shape{at + 12}; // < kShapeTrigger: solid (the shape tests, slopes), else a trigger
    F<uint16_t> mask{at + 14};  // the layers it collides with (a trigger's flags)
    F<uint32_t> word{at + 12};  // mask << 16 | shape
};

// The room block's header: the segment pool's and the segment list's offsets (dwords)
template <class S> struct RoomBlockHeaderT {
    template <class T> using F = typename S::template Field<T>;
    explicit RoomBlockHeaderT(typename S::Address a) : at(a) {}
    typename S::Address at;
    F<uint32_t> pool{at + 0};
    F<uint32_t> segments{at + 4};
};

} // namespace od
