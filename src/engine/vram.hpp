// Offline emulated PS1 VRAM (1024x512, 16-bit) fed from a game file's VRAM parts (engine/game_file),
// with standard PS1 texture-page/CLUT decoding.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "engine/game_file.hpp"

namespace od {

class Vram {
public:
    static constexpr int kWidth = 1024;
    static constexpr int kHeight = 512;

    struct UploadCounts {
        int clut = 0;
        int image = 0;
        int rejected = 0;
    };

    Vram() : mem_(size_t(kWidth) * kHeight, 0) {}

    uint16_t pixel(int x, int y) const { return mem_[size_t(y & 0x1FF) * kWidth + (x & 0x3FF)]; }

    // Rect upload; rejects out-of-bounds rects like Maybe_LoadImageToVram (0x401630).
    bool loadImage(int x, int y, int w, int h, std::span<const uint8_t> src);

    // Rect read (FUN_004012c0: rows of w 16-bit pixels, little endian)
    void readImage(int x, int y, int w, int h, uint8_t* out) const;

    // Uploads every VRAM part of a game file in file order.
    UploadCounts apply(const GameFile& file);

    // 256x256 RGBA8 of a texture window. tpage/clut as in room tpage-table entries.
    // Colour 0x0000 is transparent (alpha 0). stp: a colour with the semi-transparency bit 0x8000 gets alpha
    // kStpAlpha (the 3D pages: on a semi-transparent polygon only those texels blend, the others are opaque)
    static constexpr uint8_t kStpAlpha = 0x80;
    std::vector<uint8_t> decode(uint16_t tpage, uint16_t clut, bool stp = false) const;

private:
    std::vector<uint16_t> mem_;
};

} // namespace od
