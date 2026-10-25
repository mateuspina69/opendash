// The town's cosmetic effect, class 0x43bff0 (pool 0xa0, stage table type 0x1f: ST0A 0x8d8a04, ST0B 0x8d90d0, ST0C
// 0x8d9784; also ST47). The class runs its variant's routine (the exe's table 0x88d870 by the variant):
//   0 0x43c010  a 0x2c billboard (white 0x80, CLUT 0x7cd2, tpage 0x2f, u frame stepping 0x20) at the position << the
//               stage shift, its size from the parameters
//   1 0x52ef00  nothing (ret)
//   2 0x43c130  the same billboard, grey 0x60, at the raw position, its size from the yaw's slot
//   3 0x43c200  moves along its yaw by the row 0x88d898 / 0x88d8b8 [its parameter]; two rows (8 + 7) of 0x2c
//               billboards (dark 0x20, CLUT 0x7c91, tpage 0x2e, u frame 0x88d8d8) shrinking along the yaw
//   4 0x43c3e0  a 0x2c billboard cycling 4 frames (u / v 0x88d8e8 / 0x88d8ec), a random first frame (one RNG step)
//   5 0x43c4f0  a screen-space glow (fade in / hold until released / fade out, freed): DR_MODE + 64 POLY_G3
//               (dc::otTriG3)
//   6 0x43c730  a burst: 0xf0 rings (draw commands) + 8 additive POLY_G4 star rays in the OT (dc::otQuadG4) + 16 more
//               0xf0 ring segments; grows to 0x40, then fades out and is freed
//   7 0x43cdb0  a full-screen semi-transparent TILE (320 x 240, its colour stepping 0x400 a tick) in the OT slot
//               [0xa67840] + 0x74: rebuilt by stageEffectTintDraw (od::hud::Prim) for blend mode 0 only
//   8 0x43ce80  a screen-space glow (flickering with the frame parity unless steady): DR_MODE + 64 POLY_G3
// The POLY_G3 fans of variants 5 / 8 go through dc::otTriG3 at the point's depth (the exe links them in the screen OT
// slot [0xa67840] + 0x78). Not drawn: the TILE of variant 7 when its blend mode is not 0. The OT depth / link order of
// the G4 rays of variant 6 (OT [0x6d0048] at (view z - max(R, r / 2) / 2) / 4, at least 1) and the GPU packet cursor
// 0x6d004c are presentation only (not written).
#include "content/yosyonke/st0a_fx.hpp"
#include "content/yosyonke/st0a_fx_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actors.hpp"
#include "game/angles.hpp"
#include "game/draw_commands.hpp"
#include "game/game_state.hpp"
#include "game/glow_fan.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st0a_fx_state;

namespace {

int16_t t16(int v) { return int16_t(uint16_t(uint32_t(v))); }
int32_t sar(int32_t v, uint32_t n) { return v >> (n & 0x1f); }

uint32_t xyOf(int16_t x, int16_t y) { return uint32_t(int32_t(y)) << 16 | uint16_t(x); }

enum class StageEffectVariant : uint8_t {
    Billboard = 0,      // exe 0x43c010
    Nothing = 1,        // exe 0x52ef00
    RawBillboard = 2,   // exe 0x43c130
    Trail = 3,          // exe 0x43c200
    CyclingBillboard = 4, // exe 0x43c3e0
    Glow = 5,           // exe 0x43c4f0
    Burst = 6,          // exe 0x43c730
    Tint = 7,           // exe 0x43cdb0
    FlickerGlow = 8,    // exe 0x43ce80
};
constexpr uint8_t kStarted = 1; // +8 of the billboards / the trail / the burst: the set-up done

// the trail's rows (exe data, 8 bytes): its step a tick, the billboards' first size and their shrink
struct TrailRow : DataRecord {
    int16_t step = 0;
    int16_t size = 0;
    int16_t shrink = 0;
    static TrailRow load(const TableReader& r) {
        TrailRow rec;
        rec.step = r.s16(0x0);
        rec.size = r.s16(0x4);
        rec.shrink = r.s16(0x6);
        return rec;
    }
};
const DataTable<RecordTable<TrailRow, 4, 8>> trailRowsAlongZ{TableId::St0aFxTrailRowsAlongZ};
const DataTable<RecordTable<TrailRow, 4, 8>> trailRowsAlongY{TableId::St0aFxTrailRowsAlongY};
const DataTable<ScalarTable<uint32_t, 4>> trailFrames{TableId::St0aFxTrailFrames};
const DataTable<ScalarTable<uint8_t, 4>> cycleFrameU{TableId::St0aFxCycleFrameU};
const DataTable<ScalarTable<uint8_t, 4>> cycleFrameV{TableId::St0aFxCycleFrameV};
const DataTable<ScalarTable<uint32_t, 8>> burstRayAngles{TableId::St0aFxBurstRayAngles};
const DataTable<ScalarTable<uint32_t, 8>> burstSegmentAngles{TableId::St0aFxBurstSegmentAngles};
const DataTable<ScalarTable<int32_t, 8>> burstRayLengths{TableId::St0aFxBurstRayLengths};
const DataTable<ScalarTable<int32_t, 8>> burstSegmentRadii{TableId::St0aFxBurstSegmentRadii};

// ---- variant 0: exe 0x43c010 -------------------------------------------------------------------------------------
void billboard(EffectActor& e) {
    StageEffectFields& f = classState<StageEffectFields>(e);
    if (e.state == 0) {
        e.loopFlags |= kActorLateLoop;
        f.clearBlockBytes(); // exe 0x40dc40
        f.frame += (uint32_t(f.firstFrame) << 5) & 0xffu;
        e.state = kStarted;
    }
    const uint32_t sh = stage().coordinateShift & 0x1f;
    const uint16_t x = uint16_t(uint16_t(e.pos.x) << sh), y = uint16_t(uint16_t(e.pos.y) << sh),
                   z = uint16_t(uint16_t(e.pos.z) << sh);
    const uint32_t c = uint32_t(int32_t(int16_t(f.frame))), d = c + 0x1f;
    dc::emit({0, 0x2e808080u, xyOf(int16_t(x), int16_t(y)), c | 0x7cd24000u, z, d | 0x2f4000u,
              uint32_t(int32_t(f.size)), c | 0x5f00u, 0, d | 0x5f00u},
             e.at, false);
    f.frame = uint16_t((f.frameLow + 0x20u) & 0xffu);
}

// ---- variant 2: exe 0x43c130 -------------------------------------------------------------------------------------
void rawBillboard(EffectActor& e) {
    StageEffectFields& f = classState<StageEffectFields>(e);
    if (e.state == 0) {
        f.frameLow = 0;
        e.loopFlags |= kActorLateLoop;
        e.state = kStarted;
        return;
    }
    const uint32_t c = (f.frameLow + 0x20u) & 0xffu;
    f.frameLow = uint8_t(c);
    const uint32_t d = c + 0x1f;
    dc::emit({0, 0x2e606060u, xyOf(e.pos.x, e.pos.y), c | 0x7cd24000u, uint16_t(e.pos.z), d | 0x2f4000u,
              uint32_t(int32_t(f.rawSize)), c | 0x5f00u, 0, d | 0x5f00u},
             e.at, false);
}

// ---- variant 3: exe 0x43c200 -------------------------------------------------------------------------------------
// The effect moves by its row's step every tick; the trail = 8 billboards from the effect backwards (the size shrinking
// by the row's shrink - (its parameter & 0xf), spaced by 8 * size) and 7 forwards from 8 * (size - shrink) ahead
void trail(EffectActor& e) {
    StageEffectFields& f = classState<StageEffectFields>(e);
    if (e.state == 0) {
        e.enterState(kStarted);
        f.clearBlockBytes(); // exe 0x40dc40
        f.frame = 0;
        f.trailBase = int16_t(int32_t(f.trailBaseQuarter.get()) << 2);
    }
    const TrailRow row = (f.vertical == 0 ? trailRowsAlongZ : trailRowsAlongY)[size_t(int32_t(f.trailRow.get()))];
    if (f.vertical == 0) moveAlongYaw16(e.pos, f.yaw, 0, 0, row.step); // exe 0x5e3750
    else moveAlongYaw16(e.pos, f.yaw, 0, row.step, 0);
    for (uint32_t j = 0; j < 2; ++j) {
        int16_t p[3] = {e.pos.x, e.pos.y, e.pos.z};
        int32_t size;
        uint32_t k;
        if (j == 0) {
            size = row.size;
            k = 0;
        } else {
            size = row.size - row.shrink;
            k = 1;
            moveAlongYaw16(p, f.yaw, 0, 0, size * 8);
        }
        for (; k < 8; ++k) {
            const uint32_t uv = trailFrames[(uint32_t(int32_t(int8_t(f.frameLow.get()) >> 2)) + k) & 3];
            dc::emit({0, 0x2e202020u, xyOf(p[0], p[1]), uv + 0x7c910030u, uint32_t(int32_t(p[2])), uv + 0x2e005fu,
                      uint32_t(size), uv + 0x2f30u, 0, uv + 0x2f5fu},
                     e.at, false);
            size += int32_t(f.trailShrink & 0xf) - row.shrink;
            moveAlongYaw16(p, f.yaw, 0, 0, j == 0 ? -size * 8 : size * 8);
        }
    }
    ++f.frame;
}

// ---- variant 4: exe 0x43c3e0 -------------------------------------------------------------------------------------
void cyclingBillboard(EffectActor& e) {
    StageEffectFields& f = classState<StageEffectFields>(e);
    if (e.state == 0) {
        f.cycle = 0;
        e.loopFlags |= kActorLateLoop;
        e.state = kStarted;
        const uint32_t r = rngStep(); // the inline RNG step on 0x6d0000
        f.cycle = uint8_t(r & 0x1f);
        return;
    }
    const uint32_t n = f.cycle, i = n >> 2;
    const uint32_t u = cycleFrameU[i], v = cycleFrameV[i];
    f.cycle = uint8_t((n + 1) & 0xf);
    dc::emit({0, 0x2e808080u, xyOf(e.pos.x, e.pos.y), ((v | 0x7c9100u) << 8) | u, uint16_t(e.pos.z),
              ((v | 0x4e00u) << 8) | (u + 0x2f), 0x80u, ((v + 0x2f) << 8) | u, 0, ((v + 0x2f) << 8) | (u + 0x2f)},
             e.at, false);
}

// ---- variant 5: exe 0x43c4f0 -------------------------------------------------------------------------------------
// Its states: fade in (from 0, or from 0x20 when instant: then at once) -> Hold at 0x20; Hold until released ->
// FadeOut over 0x20 ticks, then freed. The brightness e = 2 * the ticks (FadeIn) / 0x40 - 2 * the ticks (FadeOut),
// the colour 0x306060 at e == 0x40 (and in Hold), else c = e * 3 / 2 as c | c << 8 | (c & 0x1fe) << 15.
// Drawing (glowFan): the projected point (exe 0x5f2dd0, its result ignored), the radii exe 0x5f2f20(pos, radius y * 4)
// / (radius x * 4), DR_MODE(1, 1, tpage 0x3e) + 64 POLY_G3 0x32 (the rim black, the centre the colour) linked at the
// OT slot [0xa67840] + 0x78. The ticks count up every drawn tick
enum class GlowState : uint8_t { FadeIn = 0, Hold = 1, FadeOut = 2 };
constexpr int16_t kGlowFadeTicks = 0x20;
void glow(EffectActor& e) {
    StageEffectFields& f = classState<StageEffectFields>(e);
    const GlowState st = GlowState(e.state); // the state the colour is chosen by (before this tick's switch)
    switch (st) {
    case GlowState::FadeIn:
        if (e.subState == 0) {
            f.fadeTicks = f.instant != 0 ? kGlowFadeTicks : int16_t(0);
            ++e.subState;
        }
        if (f.fadeTicks == kGlowFadeTicks) e.enterStateBytes(uint8_t(GlowState::Hold));
        break;
    case GlowState::Hold:
        if (e.subState == 0) {
            f.fadeTicks = 0;
            ++e.subState;
        }
        if (f.released != 0) e.enterStateBytes(uint8_t(GlowState::FadeOut));
        break;
    default:
        if (e.subState == 0) {
            f.fadeTicks = 0;
            ++e.subState;
        }
        if (f.fadeTicks == kGlowFadeTicks) {
            freeActor(e); // exe 0x5e1580
            return;
        }
        break;
    }
    uint32_t colour = 0x306060;
    if (st != GlowState::Hold) {
        const int32_t b = st == GlowState::FadeIn ? int32_t(f.fadeTicks) * 2 : 0x40 - int32_t(f.fadeTicks) * 2;
        if (b != 0x40) {
            const int32_t c = (b * 3 * 32) >> 6;
            colour = uint32_t(c) | ((((uint32_t(c) & 0x1fe) << 7) | uint32_t(c)) << 8);
        }
    }
    const int16_t pos[3] = {e.pos.x, e.pos.y, e.pos.z};
    const int32_t wy = projectedWidth(pos, t16(int(f.radiusY) * 4), nullptr); // exe 0x5f2f20
    const int32_t wx = projectedWidth(pos, t16(int(f.radiusX) * 4), nullptr);
    glowFan(e, colour, wy, wx);
    ++f.fadeTicks;
}

// ---- variant 6: exe 0x43c730 -------------------------------------------------------------------------------------
// The radius: 0x10, +0x10 a tick up to 0x40; then the fade (0x20) counts down (freed at 0). Drawn when the position
// << the stage shift is in the depth range 0x30 .. the effects' far depth * 4, not clipped and near the screen (the
// GTE sequence 0x40c3c0 / 0x40c3e0 / 0x40c6a0 / 0x40c800 / 0x40caa0 / 0x40c860 / 0x40cae0 / 0x40cb10 on the camera
// 0x6d0014 = cameraPoint)
void burstDraw(EffectActor& e) {
    StageEffectFields& f = classState<StageEffectFields>(e);
    const uint32_t sh = stage().coordinateShift & 0x1f;
    const int16_t Q[3] = {t16(int(uint32_t(uint16_t(e.pos.x)) << sh)), t16(int(uint32_t(uint16_t(e.pos.y)) << sh)),
                          t16(int(uint32_t(uint16_t(e.pos.z)) << sh))};
    const CameraPoint cp = cameraPoint(Q);
    if (cp.view[2] > int32_t(uint32_t(stage().effectsFar) << 2)) return;
    if (cp.view[2] < 0x30) return;
    if (cp.clipped) return; // exe 0x40cae0 < 0
    if (uint16_t(cp.sx + 0x160) > 0x400) return;
    if (uint16_t(cp.sy + 0x88) > 0x400) return;
    const int32_t r = f.radius;
    const int32_t t = f.fade;
    const uint32_t xy = xyOf(e.pos.x, e.pos.y);
    const uint32_t z = uint16_t(e.pos.z);
    const auto clamp = [](int32_t v) { return v > 0xff ? 0xff : v; };
    int32_t q = (t - 6) << 5;
    if (q > 0) { // a ring r .. 3r fading out and a disc r, blue
        const uint32_t c = uint32_t(clamp((q * 0xff) >> 8));
        const uint32_t col = (c | 0xfffff000u) << 16;
        dc::emit({0x4000u, col, xy, 0x22000000u, z | 0x20000u, uint32_t(r * 3) << 16 | uint32_t(r)}, e.at);
        dc::emit({0x4000u, col, xy, (c | 0x2200u) << 16, z | 0x20000u, uint32_t(r) << 16}, e.at);
    }
    q = (t - 6) << 6;
    if (q > 0) {
        const uint32_t d = uint32_t(clamp((q * 0x7f) >> 8)), c = uint32_t(clamp((q * 0xff) >> 8));
        const uint32_t col = ((((c | 0xfffff000u) << 8) | c) << 8) | d;
        dc::emit({0x4000u, col, xy, 0x22000000u, z | 0x20000u, uint32_t((r >> 1) * 3) << 16}, e.at);
    }
    q = (t - 5) << 6;
    if (q > 0) {
        const uint32_t d = uint32_t(clamp((q * 0x7f) >> 8)), c = uint32_t(clamp((q * 0xff) >> 8));
        const uint32_t col = ((((c | 0xfffff000u) << 8) | c) << 8) | d;
        dc::emit({0x4000u, col, xy, 0x22000000u, z | 0x20000u, uint32_t(r >> 1) << 0x11}, e.at);
    }
    q = (t - 6) << 5;
    if (q <= 0) return;
    // DR_MODE 0xe1000220 (abr 1, additive), then 8 POLY_G4 0x3a: corners in the camera plane at the view point (the
    // GTE reloaded with the identity 0x40c3c0 / 0x40c3e0, 0x40c770 / 0x40c930 / 0x40ce40 + 0x40c6a0 / 0x40c860 /
    // 0x40cb10): v0 inner (angle a - 0x10, r / 2), v1 outer (angle a, R), v2 the centre, v3 inner (a + 0x10); colours
    // 0, 0, grey, 0. Offsets (view units = camera-input units) = (cos, sin) * radius >> (0xc - stage shift)
    const uint32_t c2 = uint32_t(clamp((q * 0x7f) >> 8));
    const uint32_t grey = ((c2 << 8) | c2) << 8 | c2;
    const uint32_t s2 = (0xcu - stage().coordinateShift) & 0x1f;
    const uint32_t cw[4] = {0x3a000000u, 0, grey, 0};
    for (size_t k = 0; k < 8; ++k) {
        const uint32_t ang = burstRayAngles[k];
        const uint32_t ia = (ang - 0x10) & 0x3f, ib = (ang + 0x10) & 0x3f;
        const int32_t R = (burstRayLengths[k] + r) * 4 + 0x20;
        const int32_t h = r >> 1;
        const float off[4][2] = {{float(sar(fixedCos64(ia) * h, s2)), float(sar(fixedSin64(ia) * h, s2))},
                                 {float(sar(fixedCos64(ang) * R, s2)), float(sar(fixedSin64(ang) * R, s2))},
                                 {0.0f, 0.0f},
                                 {float(sar(fixedCos64(ib) * h, s2)), float(sar(fixedSin64(ib) * h, s2))}};
        const int16_t v[4][3] = {{Q[0], Q[1], Q[2]}, {Q[0], Q[1], Q[2]}, {Q[0], Q[1], Q[2]}, {Q[0], Q[1], Q[2]}};
        dc::otQuadG4(e.at, cw, 1, v, off);
    }
    // 8 pairs of 0xf0 ring segments (the angles 0x88d910, the radius (0x88d950 + 2r + 0x18) * 2), cyan / blue at grey / 2
    const uint32_t hh = c2 >> 1;
    const uint32_t col1 = (((hh | 0xfffff000u) << 8) | hh) << 8, col2 = (hh | 0xfffff000u) << 16;
    for (size_t k = 0; k < 8; ++k) {
        const uint32_t ang = burstSegmentAngles[k];
        const uint32_t rad = (uint32_t(burstSegmentRadii[k] + r * 2 + 0x18) * 2) << 16;
        dc::emit({((ang + 2) << 8) | (ang + 1), col1, xy, 0x22000000u, z, rad}, e.at);
        dc::emit({((ang + 3) << 8) | ang, col2, xy, 0x22000000u, z, rad}, e.at);
    }
}
void burst(EffectActor& e) {
    StageEffectFields& f = classState<StageEffectFields>(e);
    if (e.state == 0) {
        f.fade = 0x20;
        f.radius = 0x10;
        ++e.state;
    }
    if (f.radius < 0x40) {
        f.radius = f.radius + 0x10;
    } else {
        --f.fade;
        if (f.fade == 0) {
            freeActor(e); // exe 0x5e1580
            return;
        }
    }
    burstDraw(e);
}

// ---- variant 7: exe 0x43cdb0 -------------------------------------------------------------------------------------
// The TILE's colour: 0x4000 at the start, then -0x400 a tick (falling) / +0x400 (rising), the direction flipping when
// it is back at 0x4000. Its packets (rebuilt by stageEffectTintDraw): DR_MODE(1, 1, tpage +0xf << 5 | 0x1e) + TILE
// {the colour | 0x62000000, (0, 0), 320 x 240} at the OT slot [0xa67840] + 0x74
constexpr uint32_t kTintStart = 0x4000, kTintStep = 0x400;
void tint(EffectActor& e) {
    StageEffectFields& f = classState<StageEffectFields>(e);
    if (e.subState == 0) {
        f.tileColour = kTintStart;
        f.tileRising = 0;
        ++e.subState;
    }
    if (f.tileRising != 0) {
        f.tileColour += kTintStep;
        if (f.tileColour == kTintStart) f.tileRising = 0;
    } else {
        f.tileColour -= kTintStep;
        if (f.tileColour == kTintStart) f.tileRising = 1;
    }
}

// ---- variant 8: exe 0x43ce80 -------------------------------------------------------------------------------------
// No state: the projected point (exe 0x5f2dd0), the radii exe 0x5f2f20(pos, (radius y + (the frame & 1)) * 4) / (radius
// x + the frame & 1) * 4 (without the frame parity when steady), DR_MODE(1, 1, tpage 0x3e) + 64 POLY_G3 0x32 (the rim
// black, the centre 0xc0c0c0) at the OT slot [0xa67840] + 0x78 (glowFan). The s8 + parity sum is 16-bit.
void flickerGlow(EffectActor& e) {
    StageEffectFields& f = classState<StageEffectFields>(e);
    const int16_t pos[3] = {e.pos.x, e.pos.y, e.pos.z};
    const int par = f.instant != 0 ? 0 : int(gameState().frame & 1);
    const int32_t wy = projectedWidth(pos, t16(int(t16(f.radiusY + par)) * 4), nullptr); // exe 0x5f2f20
    const int32_t wx = projectedWidth(pos, t16(int(t16(f.radiusX + par)) * 4), nullptr);
    glowFan(e, 0xc0c0c0u, wy, wx);
}

// exe 0x43bff0 (the variants' table 0x88d870)
void stageEffect(EffectActor& e) {
    switch (StageEffectVariant(e.variant)) {
    case StageEffectVariant::Billboard: billboard(e); return;
    case StageEffectVariant::Nothing: return;
    case StageEffectVariant::RawBillboard: rawBillboard(e); return;
    case StageEffectVariant::Trail: trail(e); return;
    case StageEffectVariant::CyclingBillboard: cyclingBillboard(e); return;
    case StageEffectVariant::Glow: glow(e); return;
    case StageEffectVariant::Burst: burst(e); return;
    case StageEffectVariant::Tint: tint(e); return;
    case StageEffectVariant::FlickerGlow: flickerGlow(e); return;
    default: notPortedAt(kStoryPort, "0x43bff0 variant (table 0x88d870)", e.variant);
    }
}
// exe 0x43c110: the table 0x88d878 = 0x43bff0's from its entry 2 (variant v runs 0x43bff0's v + 2)
void stageEffectFromVariant2(EffectActor& e) {
    switch (e.variant) {
    case 0: rawBillboard(e); return;     // exe 0x43c130
    case 1: trail(e); return;            // exe 0x43c200
    case 2: cyclingBillboard(e); return; // exe 0x43c3e0
    case 3: glow(e); return;             // exe 0x43c4f0
    case 4: burst(e); return;            // exe 0x43c730
    case 5: tint(e); return;             // exe 0x43cdb0 (its TILE is drawn only for 0x43bff0 effects: stageEffectTintDraw)
    case 6: flickerGlow(e); return;      // exe 0x43ce80
    default: notPortedAt(kStoryPort, "0x43c110 variant (table 0x88d878)", e.variant);
    }
}

constexpr RoutineId kStageEffectClass = RoutineId::StageEffectUpdate;

} // namespace

void stageEffectUpdate(const PlayerEnv&, uint32_t actor) { stageEffect(effectActorAt(actor)); }
void stageEffectFromVariant2Update(const PlayerEnv&, uint32_t actor) { stageEffectFromVariant2(effectActorAt(actor)); }

std::vector<hud::Prim> stageEffectTintDraw() {
    std::vector<hud::Prim> out;
    for (size_t i = 0; i < EffectPool::size(); ++i) {
        EffectActor& e = effectPool()[i];

        if (!fieldActorHandler(e, ActorPool::Effects).is(kStageEffectClass) &&
            !fieldActorHandler(e, ActorPool::EffectsLate).is(kStageEffectClass))
            continue;
        if (StageEffectVariant(e.variant) != StageEffectVariant::Tint || e.subState == 0) continue;
        StageEffectFields& f = classState<StageEffectFields>(e);
        const uint32_t w = f.tileColour | 0x62000000u;
        if ((w >> 24) != 0x62) continue;       // the colour went negative: the code byte is no longer a TILE
        if ((f.tilePage & 3) != 0) continue;   // blend modes 1..3 (the tpage's abr): no hud::Prim mode
        hud::Prim q;
        q.kind = hud::Prim::Flat;
        q.colour = w & 0xffffff;
        q.semi = true;
        q.x[0] = q.x[2] = 0.0f;
        q.x[1] = q.x[3] = 320.0f;
        q.y[0] = q.y[1] = 0.0f;
        q.y[2] = q.y[3] = 240.0f;
        out.push_back(q);
    }
    return out;
}

} // namespace od::port
