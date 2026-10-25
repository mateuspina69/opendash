// The PS1 disc's game files (COMMON/*.BIN, DAT/*.BIN; docs/ps1_disc.md): a chain of sector-aligned blocks, each
// with a 0x30-byte header {type, size, sectors, ...} (sectors 0 ends the chain) and its payload from +0x30, raw or
// LZ-compressed. The PC's .DAT (engine/dat) carries the same payloads under its own segment types.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace od {

inline constexpr uint32_t kPs1Sector = 0x800;
inline constexpr uint32_t kPs1BlockHeader = 0x30;

// block types whose layout is known (the PC segment type each one becomes)
inline constexpr uint32_t kPs1Code = 0x01;           // PC 0: data at a PS1 address (+0x0c), raw
inline constexpr uint32_t kPs1Image = 0x02;          // PC 3 / 4 / 5: VRAM rects, raw
inline constexpr uint32_t kPs1PackedImage = 0x03;    // the same, LZ (bitfield bytes: the low half of +0x24)
inline constexpr uint32_t kPs1Models = 0x0a;         // PC 1: room actor models, raw
inline constexpr uint32_t kPs1PackedModels = 0x0c;   // PC 1, LZ (bitfield bytes at +0x10)
inline constexpr uint32_t kPs1PackedGeometry = 0x0d; // PC 2: room geometry, LZ (bitfield bytes at +0x10)
inline constexpr uint32_t kPs1Messages = 0x12;       // PC 6's layout, raw; the text in the PS1 font's encoding

struct Ps1Block {
    uint32_t type = 0;
    uint32_t size = 0;    // payload bytes (decompressed)
    uint32_t sectors = 0; // the block's length in the file
    uint32_t offset = 0;  // the header's file offset
    std::array<uint32_t, 12> header{};
};

// Throws std::runtime_error when a block runs past the file.
std::vector<Ps1Block> ps1Blocks(std::span<const uint8_t> file);

// The payload of a code / models / geometry / messages block, decompressed; throws std::runtime_error for an image
// block (ps1Image), a type whose layout is not known, or a corrupt LZ stream.
std::vector<uint8_t> ps1Payload(std::span<const uint8_t> file, const Ps1Block& block);

// The VRAM uploads of an image block: up to two rects (the CLUT, then the page), in upload order, each with its
// pixels (rows of w 16-bit words).
struct Ps1Upload {
    uint16_t x = 0, y = 0, w = 0, h = 0;
    std::vector<uint8_t> pixels;
};
std::vector<Ps1Upload> ps1Image(std::span<const uint8_t> file, const Ps1Block& block);

// The PS1 files' LZ: a u32 bitfield (MSB first, `bitfieldBytes` long) at +0x30, 16-bit tokens after it. Bit 0: a
// literal u16. Bit 1: token 0xffff opens a new 8 KiB window at the output position, any other copies
// ((t & 7) + 2) u16 from the window + (t >> 3) bytes. Throws std::runtime_error on a stream that runs out.
std::vector<uint8_t> ps1Decompress(std::span<const uint8_t> block, uint32_t bitfieldBytes, uint32_t outSize);

} // namespace od
