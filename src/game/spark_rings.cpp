// The spark rings class 0x433750 (see spark_rings.hpp). State 0 sets the size and the peak (a zero peak or life: freed
// at once), then every tick the draw (while drawn), the life, the spin, the size toward the target and the drift.
//
// The draw (exe: drawn, the view depth of the position >> 2 below the stage's far depth): the plane's matrix = the
// angles << 4 (Rz Rx Ry by exe 0x40c020, or Ry Rx Rz by 0x40bfd0), the fade colour = the colour * t / (life >> 1) (t
// the ticks left, mirrored past half the life), then the variant [0x88cb4c]: a ring of segments -- outer point (cos,
// h, sin) * size with h = height * size / peak, inner point = outer * ratio >> 5 at height 0, both through the matrix
// (exe 0x40c800, >> 12) and added to the position (no stage shift), projected (OTZ = AVSZ4, drawn when 1 <= OTZ <= the
// far depth) as DR_MODE (abr 1) + POLY_G4 pairs {outer black, mid colour, ...}, mid = (outer * (16 - k) + inner * k)
// >> 4. Port: the corners as camera-input points (dc::otQuadG4); the mids the same lerp of the camera-input points (the
// exe lerps the projected xy: equal up to perspective).
//   0 / 6 / 7 (0x433970): 32 segments (64-step table), one colour
//   1 (0x433e10): as 0, the colours cycled per segment over (c, c / 2, c / 4, c / 2)
//   2 (0x434360): as 1, both quads linked twice
//   3 (0x434940): 128 segments (4096-step table), the ratio ((RNG & 3) + ratio) & 0x1f with one RNG step per segment,
//      one LINE_G2 (mid -> inner black) per segment (dc::otLineG2)
//   4 (0x434d80): as 0 with the ratio 16 and k 14
//   5 (0x4351f0): as 1 with the ratio 16 and k 4
//   8 (0x435700) / 9 (0x435760) / 10 (0x4358c0): another effect sharing the class: not ported (no ported class spawns
//      them)
#include "game/actor_state.hpp"
#include "game/spark_rings.hpp"

#include <algorithm>

#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/draw_commands.hpp"
#include "game/fixed_point.hpp"
#include "game/matrix.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/stage_state.hpp"

namespace od::port {

namespace {

enum class SparkRingsState : uint8_t { Start = 0, Run = 1 };
constexpr uint8_t kRingsAbr = 1; // the DR_MODE tpage 0x3e: semi-transparency mode 1
constexpr uint32_t kRingQuadOpcode = 0x3a000000, kRingLineOpcode = 0x52000000;

// pos + (r * local) >> 12 (exe 0x40c800's 32-bit sums, translation 0; exe 0x40caa0 then the s16 add)
void sparkPoint(const EffectActor& e, const Matrix3& r, int lx, int ly, int lz, int16_t out[3]) {
    const int16_t v[3] = {wrap16(lx), wrap16(ly), wrap16(lz)};
    const int16_t pos[3] = {e.pos.x, e.pos.y, e.pos.z};
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t sum = uint32_t(int32_t(r.e[i * 3])) * uint32_t(int32_t(v[0])) +
                             uint32_t(int32_t(r.e[i * 3 + 1])) * uint32_t(int32_t(v[1])) +
                             uint32_t(int32_t(r.e[i * 3 + 2])) * uint32_t(int32_t(v[2]));
        out[i] = wrap16(int(uint16_t(uint16_t(pos[i]) + uint16_t(uint32_t(int32_t(sum) >> 12)))));
    }
}
// one ring of variants 0 / 1 / 2 / 4 / 5: ratio = the inner radius / 32, k = the gradient's split / 16, cycle = the
// 4-colour cycle (1 / 2 / 5), twice = each pair linked twice (2)
void sparkRing(const EffectActor& e, const SparkRingParams& f, const Matrix3& r, uint32_t col, int ratio, int k,
               bool cycle, bool twice) {
    const int size = f.size;
    const int peak4 = int(f.peak) << 2;
    if (peak4 == 0) return; // the exe divides by it (never 0: freed at the start)
    const int h = wrap16((int(f.height) * 4 * size) / peak4); // idiv
    uint32_t cols[4] = {col, col, col, col};
    if (cycle) {
        auto sh = [](uint32_t c, int s) {
            return ((c & 0xff) >> s) | ((((c >> 8) & 0xff) >> s) << 8) | ((((c >> 16) & 0xff) >> s) << 16) |
                   (c & 0xff000000u);
        };
        cols[1] = sh(col, 1);
        cols[2] = sh(col, 2);
        cols[3] = sh(col, 1);
    }
    const uint16_t farZ = stage().depthRange;
    const float off[4][2] = {};
    for (uint32_t i = 0; i < 0x20; ++i) {
        int16_t p[4][3]; // outer0, inner0, outer1, inner1 (the exe's 0x6d00c0 / c8 / d0 / d8)
        for (uint32_t n = 0; n < 2; ++n) {
            const uint32_t j = (i * 2 + n * 2) & 0x3f; // a 64-step angle of the exe table
            const int ox = (int(fixedCos64(j)) * size) >> 12;
            const int oz = (int(fixedSin64(j)) * size) >> 12;
            sparkPoint(e, r, ox, h, oz, p[n * 2]);
            sparkPoint(e, r, (ox * ratio) >> 5, 0, (oz * ratio) >> 5, p[n * 2 + 1]);
        }
        int16_t sxy[4][2];
        const int32_t otz = projectQuad(p, sxy);
        if (otz < 1 || otz > int32_t(farZ)) continue;
        int16_t m0[3], m1[3];
        for (int c = 0; c < 3; ++c) {
            m0[c] = wrap16((int(p[0][c]) * (16 - k) + int(p[1][c]) * k) >> 4);
            m1[c] = wrap16((int(p[2][c]) * (16 - k) + int(p[3][c]) * k) >> 4);
        }
        const uint32_t ca = cols[i & 3], cb = cols[(i + 1) & 3];
        const int16_t va[4][3] = {{p[0][0], p[0][1], p[0][2]}, {m0[0], m0[1], m0[2]},
                                  {p[2][0], p[2][1], p[2][2]}, {m1[0], m1[1], m1[2]}};
        const int16_t vb[4][3] = {{m0[0], m0[1], m0[2]}, {p[1][0], p[1][1], p[1][2]},
                                  {m1[0], m1[1], m1[2]}, {p[3][0], p[3][1], p[3][2]}};
        const uint32_t c_a[4] = {kRingQuadOpcode, ca, 0, cb};
        const uint32_t c_b[4] = {ca | kRingQuadOpcode, 0, cb, 0};
        for (int n = 0; n < (twice ? 2 : 1); ++n) {
            dc::otQuadG4(e.at, c_a, kRingsAbr, va, off);
            dc::otQuadG4(e.at, c_b, kRingsAbr, vb, off);
        }
    }
}
// exe 0x434940 (variant 3): 128 segments (the 4096-step table, outer i at index i * 32), one RNG step a segment: the
// ratio ((RNG & 3) + inner ratio) & 0x1f for the segment's new inner point; one LINE_G2 {colour mid -> black inner i}
// a segment
void sparkLines(const EffectActor& e, const SparkRingParams& f, const Matrix3& r, uint32_t col) {
    const int size = f.size;
    const int peak4 = int(f.peak) << 2;
    if (peak4 == 0) return; // the exe divides by it (never 0: freed at the start)
    const int h = wrap16((int(f.height) * 4 * size) / peak4); // idiv
    const int k = f.gradient;
    const uint16_t farZ = stage().depthRange;
    static constexpr float kNoOff2[2][2] = {};
    int16_t p[4][3]{}; // outer i, inner i, outer i+1, inner i+1
    for (uint32_t i = 0; i < 0x80; ++i) {
        const int ratio = int(((rngStep() & 3) + f.innerRatio) & 0x1f);
        auto ring = [&](uint32_t j, int16_t outer[3], int16_t inner[3]) {
            const int ox = (int(fixedCos(int(j & kAngleMask))) * size) >> 12;
            const int oz = (int(fixedSin(int(j & kAngleMask))) * size) >> 12;
            sparkPoint(e, r, ox, h, oz, outer);
            sparkPoint(e, r, (ox * ratio) >> 5, 0, (oz * ratio) >> 5, inner);
        };
        if (i == 0) {
            ring(0, p[0], p[1]);
        } else {
            std::copy(p[2], p[2] + 3, p[0]);
            std::copy(p[3], p[3] + 3, p[1]);
        }
        ring((i + 1) << 5, p[2], p[3]);
        int16_t sxy[4][2];
        const int32_t otz = projectQuad(p, sxy);
        if (otz < 1 || otz > int32_t(farZ)) continue;
        int16_t v[2][3];
        for (int c = 0; c < 3; ++c) {
            v[0][c] = wrap16((int(p[0][c]) * (16 - k) + int(p[1][c]) * k) >> 4);
            v[1][c] = p[1][c];
        }
        const uint32_t cl[2] = {col | kRingLineOpcode, 0};
        dc::otLineG2(e.at, cl, kRingsAbr, v, kNoOff2);
    }
}
void sparkDraw(const EffectActor& e, const SparkRingParams& f) {
    if (!(e.flags & kActorDrawn)) return;
    const int16_t pos[3] = {e.pos.x, e.pos.y, e.pos.z};
    const CameraPoint cp = cameraPoint(pos); // exe 0x40c3c0 / 0x40c3e0 / 0x40c6a0 / 0x40c800 / 0x40caa0
    if ((cp.view[2] >> 2) >= int(stage().depthRange)) return;
    const int ax = int(f.angles[0]) << 4, ay = int(f.angles[1]) << 4, az = int(f.angles[2]) << 4;
    const Matrix3 r = f.rotationOrder == 0 ? rotationZXY(ax, ay, az)  // exe 0x40c020
                                           : rotationYXZ(ax, ay, az); // exe 0x40bfd0
    const uint32_t life = f.life, half = life >> 1;
    uint32_t t = f.ticksLeft;
    if (t > half) t = life - t;
    if (half == 0) return; // the exe divides by it (a life of 1 would fault)
    uint32_t col = 0;      // the fade colour (the exe's 0x6d01f0, byte 3 = 0)
    for (uint32_t c = 0; c < 3; ++c) col |= (((uint32_t(f.colour[c]) * t) / half) & 0xff) << (c * 8);
    switch (e.variant) {
    case 0: case 6: case 7: sparkRing(e, f, r, col, f.innerRatio, f.gradient, false, false); return; // 0x433970
    case 1: sparkRing(e, f, r, col, f.innerRatio, f.gradient, true, false); return;                 // 0x433e10
    case 2: sparkRing(e, f, r, col, f.innerRatio, f.gradient, true, true); return;                  // 0x434360
    case 3: sparkLines(e, f, r, col); return;                                                        // 0x434940
    case 4: sparkRing(e, f, r, col, 16, 14, false, false); return;                                   // 0x434d80
    case 5: sparkRing(e, f, r, col, 16, 4, true, false); return;                                     // 0x4351f0
    default: return; // 8 / 9 / 10: not ported (see above)
    }
}
void sparkRings(EffectActor& e) { // exe 0x433750
    SparkRingParams& f = classState<SparkRingParams>(e);
    if (SparkRingsState(e.state) == SparkRingsState::Start) {
        const uint8_t g = f.grow;
        f.ticksLeft = f.life;
        f.size = int16_t(uint32_t(g) << 2);
        if (f.targetSize < g) {
            f.peak = g;
            f.grow = 1;
        } else {
            f.peak = f.targetSize;
            f.grow = 0;
        }
        if (f.peak == 0 || f.life == 0) {
            freeActor(e);
            return;
        }
        e.state = uint8_t(SparkRingsState::Run);
    }
    sparkDraw(e, f); // drawn: the ring of the variant (variant 3 steps the RNG)
    if (f.ticksLeft == 0) {
        freeActor(e);
        return;
    }
    --f.ticksLeft;
    const int16_t s = f.size;
    f.angles[1] = uint8_t(f.angles[1] + f.spin);
    if (f.grow == 0) {
        if (int(s) < int(f.targetSize) * 4) f.size = int16_t(uint32_t(f.sizeStep) + uint16_t(s));
    } else if (int(f.targetSize) * 4 < int(s)) {
        f.size = int16_t(uint16_t(s) - uint32_t(f.sizeStep));
        if (int16_t(uint16_t(s) - f.sizeStep) < 0) f.size = 0;
    }
    e.pos.x = int16_t(e.pos.x + f.velocityX);
    e.pos.y = int16_t(e.pos.y + f.velocityY);
    e.pos.z = int16_t(e.pos.z + f.velocityZ);
}

} // namespace

void sparkRingsUpdate(const PlayerEnv&, uint32_t actor) { sparkRings(effectActorAt(actor)); }

} // namespace od::port
