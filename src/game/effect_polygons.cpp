// The ordering-table polygons of the projected effects (see effect_polygons.hpp).
#include "game/effect_polygons.hpp"

#include "game/angles.hpp"
#include "game/fixed_point.hpp"
#include "game/stage_state.hpp"

namespace od::port {

namespace {

// the corners' camera-input points (the anchor's point) and their offsets: the screen corners minus the anchor's screen
// point, brought back to its depth
void screenOffsets(const ScreenAnchor& an, const uint32_t xy[4], int16_t v[4][3], float off[4][2]) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 3; ++j) v[i][j] = an.pt[j];
        off[i][0] = float(wrap16(int16_t(uint16_t(xy[i])) - an.c.sx)) * an.unitsPerPixel;
        off[i][1] = float(wrap16(int16_t(uint16_t(xy[i] >> 16)) - an.c.sy)) * an.unitsPerPixel;
    }
}

constexpr float kNoOffsets[4][2] = {};

} // namespace

uint32_t screenAround(uint32_t c, uint32_t i, int r) {
    const uint32_t x = uint16_t(wrap16(int16_t(uint16_t(c)) + (fixedCos64(i) * r >> 12)));
    const uint32_t y = uint16_t(wrap16(int16_t(uint16_t(c >> 16)) + (fixedSin64(i) * r >> 12)));
    return y << 16 | x;
}

ScreenAnchor screenAnchorAt(const int16_t pt[3]) {
    ScreenAnchor an{};
    for (int i = 0; i < 3; ++i) an.pt[i] = pt[i];
    an.c = cameraPoint(an.pt);
    an.unitsPerPixel = pixelUnitsAt(an.c);
    return an;
}

ScreenAnchor shiftedScreenAnchor(const int16_t p[3]) {
    const int sh = stage().coordinateShift & 0x1f;
    const int16_t q[3] = {wrap16(int(p[0]) << sh), wrap16(int(p[1]) << sh), wrap16(int(p[2]) << sh)};
    return screenAnchorAt(q);
}

void anchoredGT4(uint32_t owner, const ScreenAnchor& an, const dc::GouraudTexturedQuad& q, const uint32_t xy[4]) {
    int16_t v[4][3];
    float off[4][2];
    screenOffsets(an, xy, v, off);
    dc::otQuadGT4(owner, q, v, off);
}

void anchoredFT4(uint32_t owner, const ScreenAnchor& an, const dc::TexturedQuad& q, const uint32_t xy[4]) {
    anchoredGT4(owner, an, dc::gouraudForm(q), xy);
}

void anchoredG4(uint32_t owner, const ScreenAnchor& an, const uint32_t c[4], const uint32_t xy[4]) {
    int16_t v[4][3];
    float off[4][2];
    screenOffsets(an, xy, v, off);
    dc::otQuadG4(owner, c, dc::kAdditiveBlend, v, off);
}

void worldGT4(uint32_t owner, const dc::GouraudTexturedQuad& q, const int16_t v[4][3]) {
    dc::otQuadGT4(owner, q, v, kNoOffsets);
}

void worldG4(uint32_t owner, const uint32_t c[4], const int16_t v[4][3]) {
    dc::otQuadG4(owner, c, dc::kAdditiveBlend, v, kNoOffsets);
}

void LocalFrame::point(int x, int y, int z, int16_t out[3]) const {
    const int16_t v[3] = {wrap16(x), wrap16(y), wrap16(z)};
    int16_t r[3];
    transformShortVector(rotation, v, r); // exe 0x40bb30
    for (int k = 0; k < 3; ++k) out[k] = wrap16(origin[k] + r[k]);
}

LocalFrame localFrame(int16_t x, int16_t y, int16_t z, const int16_t origin[3]) {
    LocalFrame f{};
    f.rotation = rotationZYX(x, y, z); // exe 0x40c070
    for (int k = 0; k < 3; ++k) f.origin[k] = origin[k];
    return f;
}

} // namespace od::port
