// The DAT file names of the load indices (exe data 0x8886d4: a pointer to a C string per load index; exe 0x40efe0 ->
// 0x40ef60 name the file [table + index * 4]), read through the data source (engine/data_source).
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "engine/data_source.hpp"

namespace od {

struct DatFileNames : DataRecord {
    static constexpr size_t kCount = 0x200;
    static constexpr size_t kMaxNameLength = 32;
    static constexpr uint32_t kBytes = uint32_t(4 * kCount);
    std::array<std::string, kCount> names; // "" for a null entry

    static DatFileNames load(const TableReader& r);
};

// the DAT file name of load index `index` ("" when the table has none)
std::string datFileName(uint16_t index);

} // namespace od
