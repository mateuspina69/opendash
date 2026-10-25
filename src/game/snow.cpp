// The snow of the towns, class 0x591640 (see snow.hpp). Moved from content/yosyonke (it is a global class of several
// areas' stages).
#include "game/actor_state.hpp"
#include "game/snow_state.hpp"
#include "game/snow.hpp"

#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/camera_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/weather_draw.hpp"

namespace od::port {

using namespace snow_state;

namespace {

constexpr const char* kSnowPort = "snow"; // the area of its port-gap errors

int16_t t16(int64_t v) { return int16_t(uint16_t(uint64_t(v))); }

enum class SnowState : uint8_t {
    Init = 0, // exe 0x591660
    Move = 1, // exe 0x5916d0
};
// +9 (the exe's table 0x8cde44)
enum class SnowSub : uint8_t {
    Fill = 0,    // exe 0x591d00: every free slot filled (a variant 0 starts trickling instead)
    Trickle = 1, // exe 0x591e60: one new particle per countdown
    Full = 2,    // exe 0x52ef00 (ret)
    Thin = 3,    // exe 0x591f70: one particle dropped per countdown while it is outside the inner box
};
constexpr uint32_t kAllParticles = 0xffffffffu;
// the box: x -0xb4 .. 0xb4, y 0 .. 0x118, z 0 .. 0x400
constexpr uint32_t kBoxWidth = 0x168, kBoxHeight = 0x118, kBoxDepth = 0x400;
constexpr int kBoxHalfWidth = 0xb4;

uint32_t rngMod(uint32_t m) { return (rngStep() & 0xffff) % m; } // xor eax, eax; mov ax, [rng]; cdq; idiv

// the countdown: rng % period + period
void rearm(SnowFields& f) {
    const uint8_t p = f.period;
    f.countdown = uint8_t(rngMod(p) + p);
}
uint32_t packParticle(uint32_t x, uint32_t y, uint32_t z) { return (z << 9 | y) << 9 | x; }

// exe 0x5919c0(p, k): a particle outside the box is born again on its edge (RNG)
void particleWrap(int16_t p[3], int32_t k) {
    auto edgeZ = [&] { p[2] = int16_t(rngStep() & 0x3ff); };
    if (uint16_t(p[2]) > kBoxDepth) {
        uint32_t r = rngLastValue();
        bool full = true;
        uint16_t bx = 0;
        if (int16_t(k) > 0) {
            r = rngStep();
            bx = uint16_t(r);
            full = (bx & 1) != 0;
        }
        if (full) {
            p[0] = int16_t(rngMod(kBoxWidth) - kBoxHalfWidth);
            p[1] = int16_t(rngMod(kBoxHeight));
            p[2] = int16_t(uint16_t(p[2]) & 0x3ff);
            return;
        }
        if (bx & 0x10) {
            p[0] = int16_t(rngMod(kBoxWidth) - kBoxHalfWidth);
            p[1] = int16_t((bx & 0x40) ? 0xa : 0x10e);
        } else {
            p[0] = int16_t((bx & 0x40) ? -0xaa : 0xaa);
            p[1] = int16_t(rngMod(kBoxHeight));
        }
        edgeZ();
        return;
    }
    if (uint16_t(p[1]) > kBoxHeight) {
        p[0] = int16_t(rngMod(kBoxWidth) - kBoxHalfWidth);
        p[1] = int16_t(rngMod(kBoxHeight));
        p[2] = int16_t((uint16_t(p[2]) - 0x200) & 0x3ff);
        return;
    }
    if (uint16_t(uint16_t(p[0]) + kBoxHalfWidth) > kBoxWidth) {
        const uint16_t bx = uint16_t(rngStep());
        if (bx & 7) {
            p[0] = int16_t((int32_t(p[0]) + 0x21c) % int32_t(kBoxWidth) - kBoxHalfWidth);
            p[1] = int16_t((int32_t(int16_t(bx)) >> 3) % int32_t(kBoxHeight));
            return;
        }
        p[1] = 0xa;
        p[0] = int16_t((int32_t(int16_t(bx)) >> 3) % int32_t(kBoxWidth) - kBoxHalfWidth);
        edgeZ();
    }
}

void snowSubState(StaticActor& a) {
    SnowFields& f = classState<SnowFields>(a);
    switch (SnowSub(a.subState)) {
    case SnowSub::Fill:
        if (a.variant == 0) {
            a.subState = uint8_t(SnowSub::Trickle);
            return;
        }
        for (int cl = 31; cl >= 0; --cl) {
            const uint32_t m = f.live;
            if ((m >> cl) & 1) continue;
            f.live = m | (1u << cl);
            const uint32_t x = rngMod(kBoxWidth) & 0x1ff;
            const uint32_t y = rngMod(kBoxHeight) & 0x1ff;
            const uint32_t z = (rngMod(0x338) + 0xc8) & 0x3ff;
            f.particles[size_t(cl)] = packParticle(x, y, z);
        }
        rearm(f);
        if (f.live == kAllParticles) a.subState = uint8_t(SnowSub::Full);
        return;
    case SnowSub::Trickle: {
        const uint8_t c = f.countdown;
        f.countdown = uint8_t(c - 1);
        if (c != 0) return;
        const uint32_t m = f.live;
        int k = 31;
        bool found = !(m & 0x80000000u);
        while (!found) {
            const int cur = k;
            --k;
            if (cur == 0) break;
            if (!((m >> k) & 1)) found = true;
        }
        if (found) {
            f.live = m | (1u << k);
            const uint32_t x = rngMod(kBoxWidth) & 0x1ff;
            const uint32_t z = (rngMod(0x338) + 0xc8) & 0x3ff;
            f.particles[size_t(k)] = z << 18 | x;
        }
        rearm(f);
        if (f.live == kAllParticles) a.subState = uint8_t(SnowSub::Full);
        return;
    }
    case SnowSub::Full: return;
    case SnowSub::Thin: {
        const uint8_t c = f.countdown;
        if (c != 0) {
            f.countdown = uint8_t(c - 1);
            return;
        }
        const uint32_t m = f.live;
        for (int k = 31; k >= 0; --k) {
            if (!((m >> k) & 1)) continue;
            const uint32_t p = f.particles[size_t(k)];
            const bool out = uint16_t(((p >> 18) & 0x3ff) - 0x64) > 0x39c ||
                             uint16_t(((p >> 9) & 0x1ff) - 0x14) > 0xf0 || uint16_t((p & 0x1ff) - 0x14) > 0x140;
            if (!out) continue;
            f.live = m & ~(1u << k);
            rearm(f);
            if (f.live == 0) a.subState = uint8_t(SnowSub::Fill);
            return;
        }
        return;
    }
    default: notPortedAt(kSnowPort, "0x591640 sub-state", a.subState);
    }
}

// the draw block of the snow static (game/weather_draw)
SnowDrawBlock& drawBlockOf(const StaticActor& a) { return snowDrawBlock(StaticPool::index(a)); }

// exe 0x5916d0: every particle moved by the wind and the camera's motion (its eye's move turned into the view by its
// yaw / pitch, its turn), born again when it leaves the box; the sprites' inputs kept; then the sub-state
void snowMove(StaticActor& a) {
    SnowFields& f = classState<SnowFields>(a);
    const CameraState& cam = cameraState();
    const int32_t Y = t16(int32_t(int16_t(f.windY.get())) - cam.eye.y + cam.prevEye.y);
    const int32_t X = t16(int32_t(int16_t(f.windX.get())) - cam.eye.x + cam.prevEye.x);
    const int32_t Z = t16(int32_t(int16_t(f.windZ.get())) - cam.eye.z + cam.prevEye.z);
    const uint32_t iy = uint32_t(int32_t(cam.viewAngles.y)) & kAngleMask;
    const int32_t c0 = fixedSin(int(iy)), c1 = fixedCos(int(iy));
    const int32_t D = (X * c1 - Z * c0) >> 0xd;
    const int32_t R = t16((Z * c1 + X * c0) >> 0xc);
    const uint32_t ix = uint32_t(int32_t(cam.viewAngles.x)) & kAngleMask;
    const int32_t d0 = fixedSin(int(ix)), d1 = fixedCos(int(ix));
    const int32_t L = (R * d1 - Y * d0 * 2) >> 0xc;
    const int32_t Q = (Y * d1 + R * d0) >> 0xc;
    const int32_t A =
        fixedSin(int(uint32_t(uint16_t(cam.prevViewAngles.y)) - uint32_t(int32_t(cam.viewAngles.y))));
    const int32_t B =
        fixedSin(int(uint32_t(uint16_t(cam.prevViewAngles.x)) - uint32_t(int32_t(cam.viewAngles.x))));
    const int16_t rnd = int16_t(rngStep());
    const int32_t Lw = int16_t(L);
    SnowDrawBlock& drawn = drawBlockOf(a);
    drawn.drawn = 0;
    for (int bit = 31; bit >= 0; --bit) {
        if (!((f.live >> bit) & 1)) continue;
        uint32_t& slot = f.particles[size_t(bit)];
        const uint32_t p = slot;
        int16_t px = int16_t(int32_t(p & 0x1ff) - kBoxHalfWidth);
        const int32_t E2 = D - ((int32_t(px) * Lw) >> 0xa);
        int16_t py = int16_t((p >> 9) & 0x1ff);
        const int32_t edi = (((kBoxHalfWidth - int32_t(py)) * Lw) >> 0xa) + Q;
        int16_t pz = int16_t((p >> 18) & 0x3ff);
        const int32_t ebp = (int32_t(pz) * A) >> 0xd;
        int32_t absv = int16_t(ebp);
        if (int16_t(ebp) < 0) absv = -absv;
        const int32_t W = int16_t(0x800 - int32_t(pz));
        const int32_t rem = (int32_t(rnd) >> bit) % 3;
        const int32_t ecx = rem + E2 - 1;
        int32_t esi = int32_t(pz) * B + absv * d0;
        const int32_t eax = (int32_t(int16_t(edi)) * W) >> 0xb;
        int32_t edx = (int32_t(int16_t(ecx)) * W) >> 0xb;
        esi = (-esi) >> 0xc;
        esi += eax;
        py = int16_t(py + esi);
        pz = int16_t(pz + L);
        edx += ebp;
        px = int16_t(px + edx);
        int16_t v[3] = {px, py, pz};
        particleWrap(v, L);
        slot = ((uint32_t(uint16_t(v[2])) & 0x3ff) << 9 | (uint32_t(uint16_t(v[1])) & 0x1ff)) << 9 |
               ((uint32_t(uint16_t(v[0])) + kBoxHalfWidth) & 0x1ff);
        // exe 0x591c10(v): the sprite packet (render output; its input kept for game/weather_draw)
        ShortVec3& kept = drawn.particles[size_t(bit)];
        kept.x = v[0];
        kept.y = v[1];
        kept.z = v[2];
        drawn.drawn = drawn.drawn | 1u << bit;
    }
    snowSubState(a);
}

// exe 0x591640 (its states: the exe's table 0x8cde3c)
void snow(StaticActor& a) {
    SnowFields& f = classState<SnowFields>(a);
    switch (SnowState(a.state)) {
    case SnowState::Init:
        a.setDrawn(false); // drawn by its own sprites, not the pool's
        a.runFlags |= kActorWarpKept;
        if (f.period == 0) f.period = kDefaultPeriod;
        f.live = 0;
        rearm(f);
        a.state = uint8_t(SnowState::Move);
        return;
    case SnowState::Move: snowMove(a); return;
    default: notPortedAt(kSnowPort, "0x591640 state", a.state);
    }
}

} // namespace

void snowUpdate(const PlayerEnv&, uint32_t actor) { snow(staticActorAt(actor)); }

} // namespace od::port
