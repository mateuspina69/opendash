#include "ps1_container.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>

#include "engine/dat.hpp"

namespace od {

namespace {

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error("PS1 container: " + what); }

std::string hex(uint32_t v) {
    char s[11];
    std::snprintf(s, sizeof s, "0x%x", v);
    return s;
}

std::span<const uint8_t> blockBytes(std::span<const uint8_t> file, const Ps1Block& b) {
    return file.subspan(b.offset, size_t(b.sectors) * kPs1Sector);
}

} // namespace

std::vector<Ps1Block> ps1Blocks(std::span<const uint8_t> file) {
    std::vector<Ps1Block> out;
    for (size_t o = 0; o + kPs1BlockHeader <= file.size();) {
        Ps1Block b;
        for (size_t i = 0; i < b.header.size(); ++i) b.header[i] = rd32(file.data() + o + i * 4);
        b.type = b.header[0];
        b.size = b.header[1];
        b.sectors = b.header[2];
        b.offset = uint32_t(o);
        if (b.sectors == 0) break;
        if (o + size_t(b.sectors) * kPs1Sector > file.size())
            fail("block " + std::to_string(out.size()) + " runs past the file");
        out.push_back(b);
        o += size_t(b.sectors) * kPs1Sector;
    }
    return out;
}

std::vector<uint8_t> ps1Decompress(std::span<const uint8_t> block, uint32_t bitfieldBytes, uint32_t outSize) {
    std::vector<uint8_t> out;
    out.reserve(outSize);
    size_t bp = kPs1BlockHeader, dp = kPs1BlockHeader + size_t(bitfieldBytes), base = 0;
    while (out.size() < outSize) {
        if (bp + 4 > block.size()) fail("LZ bitfield runs out");
        const uint32_t word = rd32(block.data() + bp);
        bp += 4;
        for (int bit = 31; bit >= 0 && out.size() < outSize; --bit) {
            if (dp + 2 > block.size()) fail("LZ tokens run out");
            const uint16_t t = rd16(block.data() + dp);
            dp += 2;
            if (!((word >> bit) & 1)) {
                out.push_back(uint8_t(t));
                out.push_back(uint8_t(t >> 8));
            } else if (t == 0xffff) {
                base = out.size();
            } else {
                const size_t from = base + (t >> 3), n = ((t & 7) + 2) * 2;
                if (from >= out.size()) fail("LZ copy from past the output");
                for (size_t i = 0; i < n; ++i) out.push_back(out[from + i]); // may overlap its own output
            }
        }
    }
    out.resize(outSize);
    return out;
}

std::vector<uint8_t> ps1Payload(std::span<const uint8_t> file, const Ps1Block& b) {
    const auto bytes = blockBytes(file, b);
    switch (b.type) {
    case kPs1Code:
    case kPs1Models:
    case kPs1Messages:
        if (kPs1BlockHeader + size_t(b.size) > bytes.size()) fail("raw payload runs past its block");
        return {bytes.begin() + kPs1BlockHeader, bytes.begin() + kPs1BlockHeader + b.size};
    case kPs1PackedModels:
    case kPs1PackedGeometry: return ps1Decompress(bytes, b.header[4], b.size);
    case kPs1Image:
    case kPs1PackedImage: fail("an image block's payload is its VRAM uploads (ps1Image)");
    default: fail("block type " + hex(b.type) + " is not read");
    }
}

std::vector<Ps1Upload> ps1Image(std::span<const uint8_t> file, const Ps1Block& b) {
    if (b.type != kPs1Image && b.type != kPs1PackedImage) fail("not an image block");
    const auto bytes = blockBytes(file, b);
    std::vector<uint8_t> data;
    if (b.type == kPs1PackedImage) data = ps1Decompress(bytes, b.header[9] & 0xffff, b.size);
    else {
        if (kPs1BlockHeader + size_t(b.size) > bytes.size()) fail("raw image runs past its block");
        data.assign(bytes.begin() + kPs1BlockHeader, bytes.begin() + kPs1BlockHeader + b.size);
    }
    struct Rect {
        uint16_t x, y, w, h;
        size_t bytes() const { return size_t(w) * h * 2; }
    };
    const Rect rects[2] = {
        {uint16_t(b.header[3]), uint16_t(b.header[3] >> 16), uint16_t(b.header[4]), uint16_t(b.header[4] >> 16)},
        {uint16_t(b.header[5]), uint16_t(b.header[5] >> 16), uint16_t(b.header[6]), uint16_t(b.header[6] >> 16)}};
    // the pixels back to back; a raw block whose page does not fit in its first sector holds the CLUT at +0x30 and
    // the page from the next sector (+0x800: data offset 0x7d0)
    constexpr size_t kNextSector = kPs1Sector - kPs1BlockHeader;
    size_t pageAt = rects[0].bytes();
    if (b.type == kPs1Image && rects[1].bytes() != 0 && data.size() == kNextSector + rects[1].bytes() &&
        rects[0].bytes() <= kNextSector)
        pageAt = kNextSector;
    else if (rects[0].bytes() + rects[1].bytes() != data.size())
        fail("image block at " + hex(b.offset) + ": its rects do not cover its payload");
    std::vector<Ps1Upload> out;
    const size_t at[2] = {0, pageAt};
    for (int i = 0; i < 2; ++i) {
        if (rects[i].bytes() == 0) continue;
        Ps1Upload u{rects[i].x, rects[i].y, rects[i].w, rects[i].h, {}};
        u.pixels.assign(data.begin() + at[i], data.begin() + at[i] + rects[i].bytes());
        out.push_back(std::move(u));
    }
    return out;
}

} // namespace od
