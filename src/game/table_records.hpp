// Record shapes many of the game's tables share (engine/data_source RecordTable records): the s16 points of the
// tables of offsets, spots and corners.
#pragma once

#include <cstdint>

#include "engine/data_source.hpp"

namespace od::port {

// an s16 point (x, y, z: 6 bytes; a table's stride may pad it)
struct ShortPoint : DataRecord {
    static constexpr uint32_t kBytes = 6; // (a table of one point)
    int16_t x = 0, y = 0, z = 0;
    static ShortPoint load(const TableReader& r) { return {{}, r.s16(0), r.s16(2), r.s16(4)}; }
};
// an s16 point and its 4th lane (8 bytes)
struct ShortPoint4 : DataRecord {
    static constexpr uint32_t kBytes = 8; // (a table of one point)
    int16_t x = 0, y = 0, z = 0, w = 0;
    static ShortPoint4 load(const TableReader& r) { return {{}, r.s16(0), r.s16(2), r.s16(4), r.s16(6)}; }
};

} // namespace od::port
