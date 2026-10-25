// .DAT container reader — every file in Dash2/dat uses the same TOC. Mirrors dash2.exe
// Maybe_ParseDatContainer (0x40eb90).
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace od {

struct DatSegment {
    uint32_t type = 0;       // 0..9, see file_inventory.md §3
    uint32_t size = 0;
    uint32_t arg = 0;        // type-dependent (PS1 load address, slot, CLUT destination, ...)
    uint32_t extra = 0;
    uint32_t fileOffset = 0; // payload offset inside the file
};

// Throws std::runtime_error on a malformed container.
std::vector<DatSegment> readContainer(std::span<const uint8_t> data);

std::vector<uint8_t> readFile(const std::filesystem::path& path);

inline uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

} // namespace od
