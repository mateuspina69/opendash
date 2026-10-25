#include "vram.hpp"

#include "dat.hpp"

namespace od {

namespace {

void bgr555ToRgba(uint16_t c, bool stp, uint8_t* out) {
    out[0] = uint8_t((c & 0x1F) << 3);
    out[1] = uint8_t(((c >> 5) & 0x1F) << 3);
    out[2] = uint8_t(((c >> 10) & 0x1F) << 3);
    out[3] = c == 0 ? 0 : stp && (c & 0x8000) ? Vram::kStpAlpha : 255;
}

} // namespace

bool Vram::loadImage(int x, int y, int w, int h, std::span<const uint8_t> src) {
    if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > kWidth || y + h > kHeight) return false;
    if (src.size() < size_t(w) * h * 2) return false;
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c)
            mem_[size_t(y + r) * kWidth + (x + c)] = rd16(src.data() + (size_t(r) * w + c) * 2);
    return true;
}

void Vram::readImage(int x, int y, int w, int h, uint8_t* out) const {
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c) {
            const uint16_t v = pixel(x + c, y + r);
            out[(size_t(r) * w + c) * 2] = uint8_t(v);
            out[(size_t(r) * w + c) * 2 + 1] = uint8_t(v >> 8);
        }
}

Vram::UploadCounts Vram::apply(const GameFile& file) {
    UploadCounts counts;
    for (const FilePart& p : file.parts) {
        if (p.kind != FilePart::Kind::Vram) continue;
        for (const VramRect& r : p.rects) {
            const bool ok = loadImage(r.x, r.y, r.w, r.h, r.pixels);
            ok ? ++(r.clut ? counts.clut : counts.image) : ++counts.rejected;
        }
    }
    return counts;
}

std::vector<uint8_t> Vram::decode(uint16_t tpage, uint16_t clut, bool stp) const {
    const int mode = (tpage >> 7) & 3;
    const int px = (tpage & 0xF) * 64;
    const int py = ((tpage >> 4) & 1) * 256;
    const int cx = (clut & 0x3F) * 16;
    const int cy = (clut >> 6) & 0x1FF;
    std::vector<uint8_t> out(256 * 256 * 4);
    uint8_t* o = out.data();
    for (int v = 0; v < 256; ++v) {
        for (int u = 0; u < 256; ++u, o += 4) {
            uint16_t colour;
            if (mode == 0) {
                const int idx = (pixel(px + (u >> 2), py + v) >> ((u & 3) * 4)) & 0xF;
                colour = pixel(cx + idx, cy);
            } else if (mode == 1) {
                const int idx = (pixel(px + (u >> 1), py + v) >> ((u & 1) * 8)) & 0xFF;
                colour = pixel(cx + idx, cy);
            } else {
                colour = pixel(px + u, py + v);
            }
            bgr555ToRgba(colour, stp, o);
        }
    }
    return out;
}

} // namespace od
