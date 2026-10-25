// The glow effects class 0x4309b0 (see glow_trails.hpp). States [0x88cab8]: 0 0x4309d0 (set up: the stage's coordinate
// shift cleared -- as the exe does --, late), 1 0x4309f0 (the variant [0x88cac4]; the parent gone -> 2), 2 0x4358c0
// (freed).
#include "game/actor_state.hpp"
#include "game/glow_trails.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_pools.hpp"
#include "game/fixed_point.hpp"
#include "game/matrix.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/stage_state.hpp"

namespace od::port {

namespace {

constexpr const char* kGlowTrailPort = "glow trail port"; // the area of its port-gap errors

enum class GlowTrailState : uint8_t {
    Setup = 0, // exe 0x4309d0
    Run = 1,   // exe 0x4309f0
    Free = 2,  // exe 0x4358c0
};
constexpr uint8_t kTrailPoints = 4; // the trail's length (+9 counts up to it)

// exe 0x430ac0 (variant 1): the trail's point -- on the parent's bone at the offset (exe 0x430f10), or the offset turned
// by the parent's rotation and added to its base point -- projected, the screen history shifted
void glowTrail(EffectActor& e, GlowTrailFields& f) {
    if (e.subState <= kTrailPoints - 1) ++e.subState;
    int16_t pt[3];
    if (!f.parent) {
        pt[0] = e.pos.x;
        pt[1] = e.pos.y;
        pt[2] = e.pos.z;
    } else {
        // the parent's class through the byte table 0x431034: 0x20 -> the bone point; else a base point (0xa0: its s16
        // position; 0 / 0xe0: (0, 0, 0); the others: the integer parts of its 16.16 position) + the offset (u16 << 4)
        // turned by the parent's angles (exe 0x5e3970), its integer parts added
        ActorHeader& parent = *f.parent;
        const uint32_t cls = actorSlotClass(parent.slot);
        if (cls == kAnimatedClass) {
            const int16_t v[3] = {e.pos.x, e.pos.y, e.pos.z};
            bonePointOffset(asAnimated(parent), f.bone, v, pt);
        } else {
            int16_t base[3] = {0, 0, 0};
            if (cls == kEffectClass) {
                const EffectActor& p = effectPool().of(parent);
                base[0] = p.pos.x;
                base[1] = p.pos.y;
                base[2] = p.pos.z;
            } else if (cls != kPlayerClass && cls != kControllerClass) {
                const Actor& p = actorLayoutOf(parent);
                base[0] = p.pos.x.whole;
                base[1] = p.pos.y.whole;
                base[2] = p.pos.z.whole;
            }
            int32_t r[3];
            rotateVector(actorLayoutOf(parent).rotation, int(uint16_t(e.pos.x)) << 4, int(uint16_t(e.pos.y)) << 4,
                         int(uint16_t(e.pos.z)) << 4, r);
            for (int i = 0; i < 3; ++i) pt[i] = wrap16(int(base[i]) + (r[i] >> 16));
        }
    }
    // exe 0x5f2f20(pt, size, &depth): the projected width, only read by the draw (render)
    int16_t sxy[2];
    projectOnScreen(pt, sxy);
    f.pushScreenPoint(sxy[0], sxy[1]);
    // the trail quads (0x88ca98 colours, OT packets 0x6d004c / 0x6d0048, exe 0x40b980 widths): render output
}

// exe 0x430a20 (variant 0: the ST19 gate lights exe 0x57fcd0, ST16): the size by phase (+9: 0 grows with the ticks to
// 0xf, 1 holds 0xf for 0xd ticks, 2 shrinks 0xe - (ticks >> 2) for 9 ticks, 3 stays 0xc); exe 0x430e30 draws it
void glowRing(EffectActor& e, GlowTrailFields& f) {
    switch (e.subState) {
    case 0:
        f.ringSize = f.ringTicks;
        ++f.ringTicks;
        if (f.ringTicks > 0xf) {
            f.ringTicks = 0;
            e.subState = 1;
        }
        return;
    case 1:
        f.ringSize = 0xf;
        ++f.ringTicks;
        if (f.ringTicks > 0xc) {
            f.ringTicks = 0;
            e.subState = 2;
        }
        return;
    case 2: {
        const uint8_t c = f.ringTicks;
        f.ringSize = uint8_t(0xe - (c >> 2));
        f.ringTicks = uint8_t(c + 1);
        if (uint8_t(c + 1) > 8) {
            f.ringTicks = 0;
            e.subState = 3;
        }
        return;
    }
    default: f.ringSize = 0xc; return;
    }
}

// exe 0x430df0 (variant 2: ST41 / ST42): +9 == 0: the size 0xc; +9 == 1: shrinking, 0xb - (ticks >> 3) with the ticks
// counting up, freed (state 2) once negative; exe 0x430e30 draws it
void glowFade(EffectActor& e, GlowTrailFields& f) {
    if (e.subState == 0) {
        f.ringSize = 0xc;
    } else if (e.subState == 1) {
        const uint8_t c = f.ringTicks;
        const uint8_t size = uint8_t(0xb - (c >> 3));
        f.ringTicks = uint8_t(c + 1);
        f.ringSize = size;
        if (int8_t(size) < 0) e.state = uint8_t(GlowTrailState::Free);
    }
}

void glowTrailTick(EffectActor& e) { // exe 0x4309b0
    GlowTrailFields& f = classState<GlowTrailFields>(e);
    switch (GlowTrailState(e.state)) {
    case GlowTrailState::Setup: // exe 0x4309d0
        stage().coordinateShift = 0;
        f.ringTicks = 0;
        e.state = uint8_t(GlowTrailState::Run);
        e.loopFlags |= kActorLateLoop;
        return;
    case GlowTrailState::Run: // exe 0x4309f0
        if (e.variant == uint8_t(GlowTrailVariant::Ring)) glowRing(e, f);
        else if (e.variant == uint8_t(GlowTrailVariant::Trail)) glowTrail(e, f);
        else if (e.variant == uint8_t(GlowTrailVariant::Fade)) glowFade(e, f);
        else notPortedAt(kGlowTrailPort, "0x4309b0 variant", e.variant);
        if (f.parent && f.parent->flags == 0) e.state = uint8_t(GlowTrailState::Free);
        return;
    case GlowTrailState::Free: freeActor(e); return; // exe 0x4358c0
    default: notPortedAt(kGlowTrailPort, "0x4309b0 state", e.state);
    }
}

} // namespace

void glowTrailUpdate(const PlayerEnv&, uint32_t actor) { glowTrailTick(effectActorAt(actor)); }

} // namespace od::port
