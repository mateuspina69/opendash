// The screen-space glow fan (see glow_fan.hpp).
#include "game/actor_state.hpp"
#include "game/glow_fan.hpp"

#include "game/angles.hpp"
#include "game/draw_commands.hpp"
#include "game/fixed_point.hpp"
#include "game/projection.hpp"
#include "game/projection_state.hpp"
#include "game/stage_state.hpp"

namespace od::port {

namespace {

constexpr uint32_t kFanSteps = 0x40; // the rim's 64 steps

// (v * a 4096-step value) / 4096 rounded toward zero (cdq; and edx, 0xfff; add; sar 0xc)
int32_t div4096(int32_t v) { return (v + (v < 0 ? 0xfff : 0)) >> 12; }

} // namespace

void glowFan(const EffectActor& effect, uint32_t centre, int32_t wy, int32_t wx) {
    const int sh = stage().coordinateShift & 0x1f;
    const int16_t q[3] = {wrap16(int(uint32_t(uint16_t(effect.pos.x)) << sh)),
                          wrap16(int(uint32_t(uint16_t(effect.pos.y)) << sh)),
                          wrap16(int(uint32_t(uint16_t(effect.pos.z)) << sh))};
    const CameraPoint cp = cameraPoint(q);
    const int32_t h = projection().focal;
    if (cp.view[2] <= 0 || h == 0) return; // behind the eye: no camera-plane offset for the pixels
    const float k = float(cp.view[2]) / float(h);
    int32_t px = 0, py = wy; // rim i - 1 (pixel offset from S)
    for (uint32_t i = 1; i <= kFanSteps; ++i) {
        const int32_t nx = div4096(int32_t(uint32_t(fixedSin64(i)) * uint32_t(wx)));
        const int32_t ny = div4096(int32_t(uint32_t(fixedCos64(i)) * uint32_t(wy)));
        const uint32_t c[3] = {dc::packetColour(dc::kPolyG3Semi, 0), centre, 0};
        const int16_t v[3][3] = {{q[0], q[1], q[2]}, {q[0], q[1], q[2]}, {q[0], q[1], q[2]}};
        const float off[3][2] = {{float(wrap16(px)) * k, float(wrap16(py)) * k}, {0.0f, 0.0f},
                                 {float(wrap16(nx)) * k, float(wrap16(ny)) * k}};
        dc::otTriG3(effect.at, c, dc::kAdditiveBlend, v, off);
        px = nx;
        py = ny;
    }
}

} // namespace od::port
