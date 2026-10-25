// The screen ripples (pool 0xa0 type 0x16, exe 0x438750, variants by the table 0x88d530), one routine per exe routine:
//   variant 0 (0x438770; created by 0x52b180 at the screen point (0x6b, 0x44): the start radius byte, growth 6, the
//             largest radius 0x100): a growing ripple of the frame buffer. Its picture is drawn by the renderer's output
//             pass (the app's ScreenRipple: it samples the frame buffer): 32 POLY_FT4 around the centre, angles
//             0x80..0x1000 by 0x80, rings r and r + (phase >> 8), corners clamped to the screen (the newest inner corner
//             not), uv = the corner shifted by -/+/+/- the shift byte by quarter (-0x40 more right of x 0x80), the tpage
//             of the draw buffer
//   variant 1 (0x438aa0): two rings in the camera plane, the radius growing to 0x60 then breathing
//   variant 2 (0x438bc0): an 8-ray star shrinking over 9 ticks
//   variant 3 (0x439080 -> 0x4390a0) reads the actor's first two dwords as actor pointers (a LINE_G2 0x52 between
//             them): no creator of it found, not ported
// Names are hypotheses from the observed behaviour.
#include "content/nino/screen_ripple.hpp"
#include "content/nino/screen_ripple_state.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/display.hpp"
#include "game/draw_commands.hpp"
#include "game/field_alias.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace screen_ripple_state;

namespace {

enum class RippleVariant : uint8_t { Growing = 0, Rings = 1, RayStar = 2 };

// ---- variant 0 (0x438770): the radius += the growth a tick, the phase (from the start radius << 8) += 0x40; freed once
// the radius passes the largest. Nothing while the frame draws nothing. (GrowingRippleFields: screen_ripple.hpp)


void growingRipple(EffectActor& actor) {
    GrowingRippleFields& ripple = classState<GrowingRippleFields>(actor);
    if (actor.state == 0) {
        ripple.radius = 0;
        ripple.phase = uint16_t(uint32_t(ripple.startRadius) << 8);
        actor.state += 1u;
    }
    if (display().drawingOff != 0) return;
    ripple.phase += 0x40u;
    ripple.radius = int16_t(ripple.radius + ripple.growth);
    if (ripple.radius > ripple.maxRadius) freeActor(actor); // exe 0x5e1580
}

// ---- variant 1 (0x438aa0): two rings at the screen-plane point; step 0: the radius grows by 1 to 0x60;
// 1: the radius 0x60 - (sin(phase) >> 10), the phase -= 0x200 (& 0x3ff). Draw commands 0xf0: the ring of radius r
// (0x4000, colour 0xf0000000 / 0x2280f000) and r + 0x10 (0xf0204000 / 0x22204000).
struct RingRippleFields {
    explicit RingRippleFields(const EffectActor& actor) : RingRippleFields(const_cast<EffectActor&>(actor)) {}
    explicit RingRippleFields(EffectActor& actor) : radius(actor.trail[0]), unused1a(actor.trail[1]), phase(actor.trail[2]) {}
    int16_t& radius;
    int16_t& unused1a; // cleared with the radius
    FieldAlias<uint16_t, int16_t> phase;
};
enum class RingRippleStep : uint8_t { Grow = 0, Breathe = 1 };
constexpr int16_t kRingRadius = 0x60;

void ringRipple(EffectActor& actor) {
    auto&& ring = RingRippleFields(actor);
    if (actor.state == 0) {
        ring.radius = 0;
        ring.unused1a = 0;
        actor.state += 1u;
    }
    switch (RingRippleStep(actor.step)) {
    case RingRippleStep::Grow:
        ring.radius += 1u;
        if (ring.radius > kRingRadius) {
            ring.radius = kRingRadius;
            actor.step += 1u;
        }
        break;
    case RingRippleStep::Breathe: {
        const uint16_t t = ring.phase;
        ring.radius = int16_t(kRingRadius - (int16_t(fixedSin(int(t))) >> 10));
        ring.phase = uint16_t((uint32_t(t) - 0x200u) & 0x3ffu);
        break;
    }
    default: break;
    }
    const int32_t r = ring.radius;
    const uint32_t xy = uint32_t(uint16_t(actor.pos.x)) | (uint32_t(int32_t(actor.pos.y)) << 16);
    const uint32_t z = uint32_t(uint16_t(actor.pos.z)) | 0x10000u;
    const uint32_t r2 = uint32_t(r + 0x10);
    dc::emit({0x4000u, 0xf0000000u, xy, 0x2280f000u, z, uint32_t(r)}, actor.at);
    dc::emit({0x4000u, 0xf0204000u, xy, 0x22204000u, z, (r2 << 16) | r2}, actor.at);
}

constexpr uint8_t kRayStarTicks = 9;

void rayStar(EffectActor& actor) {
    auto&& star = classState<RayStarFields>(actor);
    if (actor.state == 0) {
        for (uint32_t i = 0; i < 8; ++i) {
            star.rayLengths[i] = uint8_t(rngStep() & 0x3f);
            star.rayAngles[i] = uint8_t(rngStep() & 0x3f);
        }
        star.ticks = kRayStarTicks;
        actor.state += 1u;
    }
    star.ticks -= 1u;
    if (star.ticks == 0) {
        freeActor(actor); // exe 0x5e1580
        return;
    }
    const int16_t centre[3] = {actor.pos.x, actor.pos.y, actor.pos.z};
    const CameraPoint cp = cameraPoint(centre); // 0x40c3c0 / 0x40c3e0 (0x6d0014), 0x40c6a0, 0x40c800, 0x40caa0
    if (cp.view[2] > int32_t(uint32_t(stage().effectsFar) << 2) || cp.view[2] < 0x30) return;
    const int sh = 0xc - int(stage().coordinateShift);
    auto off = [&](uint32_t ang, int len, float o[2]) {
        o[0] = float((fixedCos(int(ang)) * len) >> sh);
        o[1] = float((fixedSin(int(ang)) * len) >> sh);
    };
    static constexpr uint32_t kCol[4] = {0x3a000000u, 0x000c1808u, 0x00404040u, 0x3a000000u};
    for (uint32_t i = 0; i < 8; ++i) {
        const uint32_t A = (uint32_t(star.rayAngles[i]) << 6) & 0xfff;
        const int len = int(star.rayLengths[i]) * int8_t(star.ticks);
        const int lIn = len - (len >> 2), lOut = len + (len >> 2);
        for (int q = 0; q < 2; ++q) {
            int16_t v[4][3];
            float o[4][2];
            for (int j = 0; j < 4; ++j)
                for (int k = 0; k < 3; ++k) v[j][k] = centre[k];
            off((A - 0x10) & 0xfff, len, o[0]);
            off(A, q == 0 ? lIn : lOut, o[1]);
            off(A, len, o[2]);
            off((A + 0x10) & 0xfff, len, o[3]);
            dc::otQuadG4(actor.at, kCol, 1, v, o);
        }
    }
}

} // namespace

void screenRippleUpdate(const PlayerEnv&, uint32_t address) {
    EffectActor& actor = effectActorAt(address);
    switch (RippleVariant(actor.variant)) {
    case RippleVariant::Growing: growingRipple(actor); break;
    case RippleVariant::Rings: ringRipple(actor); break;
    case RippleVariant::RayStar: rayStar(actor); break;
    default: notPortedAt(kStoryPort, "0x438750 variant (table 0x88d530)", actor.variant);
    }
}

} // namespace od::port
