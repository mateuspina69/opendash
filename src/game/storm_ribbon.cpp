// The storm ribbon (see storm_ribbon.hpp). Render side (presentation only): the exe's GPU packets (POLY_G4 under a
// DR_MODE of abr 1, linked into the OT) become dc::otQuadG4 quads whose corners are camera-input points (the exe
// projects them itself); the projection's float history is not replicated (render-only state).
#include "game/actor_state.hpp"
#include "game/storm_ribbon.hpp"

#include <vector>

#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/camera_state.hpp"
#include "game/draw_commands.hpp"
#include "game/fixed_point.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/stage_state.hpp"

namespace od::port {

namespace {

inline constexpr const char* kStormRibbonArea = "story port"; // the area of its port-gap errors

enum class StormRibbonState : uint8_t {
    Start = 0, // exe 0x431f70
    Draw = 1,  // exe 0x431fb0
};

constexpr uint8_t kColourStepTicks = 3;   // the colour phases step every 3 ticks ...
constexpr uint8_t kLowPhaseStep = 3, kMidPhaseStep = 4, kHighPhaseStep = 1; // ... by these 64-step angles
constexpr uint16_t kWobbleMidStep = 0xdf, kWobbleFastStep = 0x47; // the accumulators' steps per tick
constexpr uint8_t kSegmentWobbleSlow = 2, kSegmentWobbleMid = 7, kSegmentWobbleFast = 0xb; // per band
constexpr int16_t kBandStep = 0x300;      // each band 0x300 further in z
constexpr int16_t kMiddleRailDrop = 0x1400, kBottomRailDrop = 0x400; // rails 1 / 2 below rail 0
constexpr int32_t kFadeBands = 0x10;      // the bands fading in, and the bands fading out
constexpr int32_t kNearDepth = 0x30;      // a band nearer than this view z is not drawn
constexpr uint32_t kColourBias = 0x20b020u; // the colour words' bias (the band colours swing around it)
constexpr uint32_t kTopQuadCode = 0x3a000000u; // POLY_G4, semi-transparent
constexpr uint32_t kBottomColour = 0x50010u;   // the bottom rail's dim colour
constexpr size_t kMinQuads = 3; // the OT link needs at least 3 quads (0x68 bytes of packets past the DR_MODE)

// the colour of a ribbon vertex from its three phases; fade: the band fades by `mult` steps of its colour's nibbles
uint32_t ribbonColour(uint32_t low, uint32_t high, uint32_t mid, int32_t mult, bool fade) {
    const int32_t A = sin64(high), B = sin64(mid), d = int32_t(sin64(low)) >> 7;
    const uint32_t base = ((uint32_t(A) & 0x7f80u) << 9) | (uint32_t(B) & 0xff00u) | (uint32_t(d) & 0xffu);
    if (!fade) return base + kColourBias;
    const uint32_t ex = ((uint32_t(A) & 0xffffff80u) << 9) | (uint32_t(B) & 0xffffff00u) | (uint32_t(d) & 0xffu);
    const uint32_t cx = (uint32_t(B) & 0xffffff00u) | (uint32_t(d) & 0xffu);
    const uint32_t t = ((ex + kColourBias) & 0xf00000u) | ((cx - 0x4fe0u) & 0xf000u) | (uint32_t(d + 0x20) & 0xf0u);
    const int32_t step = int32_t(t) >> 4;
    return base - uint32_t(mul32(step, mult)) + kColourBias;
}

// The ribbon being built: its three rails (rail 0 now and at the band before; rails 1 / 2 below it), the phases and
// the quads
struct RibbonBuild {
    int16_t top[3], topBefore[3], middle[3], bottom[3];
    uint8_t phaseMid, phaseLow, phaseHigh, wobbleMid, wobbleFast, wobbleSlow;
    bool offScreen; // the last band was off screen: rails 1 / 2 restart below rail 0
    struct Quad {
        uint32_t c[4];
        int16_t v[4][3];
    };
    std::vector<Quad> quads;

    // one band: mode 0 fading in (k + 1 / k steps), 1 steady, 2 fading out (k - 1 / k)
    void band(int mode, int32_t k) {
        for (int i = 0; i < 3; ++i) topBefore[i] = top[i];
        wobbleMid = uint8_t((wobbleMid + kSegmentWobbleMid) & 0x3f);
        wobbleSlow = uint8_t((wobbleSlow + kSegmentWobbleSlow) & 0x3f);
        wobbleFast = uint8_t((wobbleFast + kSegmentWobbleFast) & 0x3f);
        top[2] = wrap16(top[2] + kBandStep);
        const int16_t dx = wrap16(((int32_t(sin64(wobbleMid)) << 8) >> 12) + (mul32(sin64(wobbleSlow), 0x180) >> 12) +
                                  ((int32_t(sin64(wobbleFast)) << 8) >> 12));
        top[0] = wrap16(top[0] + dx);
        const CameraPoint cp = cameraPoint(top);
        phaseLow = uint8_t(phaseLow + kLowPhaseStep);
        phaseMid = uint8_t(phaseMid + kMidPhaseStep);
        phaseHigh = uint8_t(phaseHigh + kHighPhaseStep);
        const int32_t z = cp.view[2];
        if (z > int32_t(uint32_t(stage().effectsFar) << 2) || z < kNearDepth || uint16_t(cp.sx + 0x20) > 0x160 ||
            uint16_t(cp.sy + 0x100) > 0x1f0) {
            offScreen = true;
            return;
        }
        if (offScreen) {
            middle[0] = topBefore[0];
            middle[1] = wrap16(topBefore[1] + kMiddleRailDrop);
            middle[2] = topBefore[2];
            bottom[0] = middle[0];
            bottom[1] = wrap16(middle[1] + kBottomRailDrop);
            bottom[2] = middle[2];
            offScreen = false;
        }
        int32_t m1 = k + 1, m2 = k;
        if (mode == 2) m1 = k - 1;
        const bool fade = mode != 1;
        const uint32_t colA = ribbonColour(phaseLow, phaseHigh, phaseMid, m1, fade);
        const uint32_t colB = ribbonColour(uint32_t(phaseLow) + kLowPhaseStep, uint32_t(phaseHigh) + kHighPhaseStep,
                                           uint32_t(phaseMid) + kMidPhaseStep, m2, fade);
        int16_t middleBefore[3], bottomBefore[3];
        for (int i = 0; i < 3; ++i) {
            middleBefore[i] = middle[i];
            bottomBefore[i] = bottom[i];
        }
        middle[0] = wrap16(middle[0] + dx);
        middle[2] = wrap16(middle[2] + kBandStep);
        bottom[0] = wrap16(bottom[0] + dx);
        bottom[2] = wrap16(bottom[2] + kBandStep);
        Quad upper{{kTopQuadCode, 0, colA, colB}, {}};
        Quad lower{{colA | kTopQuadCode, colB, kBottomColour, kBottomColour}, {}};
        for (int i = 0; i < 3; ++i) {
            upper.v[0][i] = topBefore[i];
            upper.v[1][i] = top[i];
            upper.v[2][i] = middleBefore[i];
            upper.v[3][i] = middle[i];
            lower.v[0][i] = middleBefore[i];
            lower.v[1][i] = middle[i];
            lower.v[2][i] = bottomBefore[i];
            lower.v[3][i] = bottom[i];
        }
        quads.push_back(upper);
        quads.push_back(lower);
    }
};

} // namespace

void stormRibbonStart(EffectActor& ribbon) {
    auto&& fields = classState<StormRibbonFields>(ribbon);
    ribbon.state = uint8_t(StormRibbonState::Draw);
    ribbon.loopFlags |= kActorLateLoop;
    fields.wobbleSlow = uint16_t((rngStep() & 0x1fu) << 8);
}

void stormRibbonDraw(EffectActor& ribbon) {
    auto&& fields = classState<StormRibbonFields>(ribbon);
    if (fields.colourCountdown == 0) {
        fields.colourPhaseLow += kLowPhaseStep;
        fields.colourPhaseMid += kMidPhaseStep;
        fields.colourPhaseHigh += kHighPhaseStep;
        fields.colourCountdown = kColourStepTicks;
    }
    --fields.colourCountdown;
    fields.wobbleMid += kWobbleMidStep;
    fields.wobbleFast += kWobbleFastStep;
    loadProjectionMatrix(cameraTransform().view);      // exe 0x40c3c0
    loadProjectionTranslation(cameraTransform().view); // exe 0x40c3e0
    RibbonBuild r{};
    r.phaseLow = fields.colourPhaseLow;
    r.phaseHigh = fields.colourPhaseHigh;
    r.phaseMid = fields.colourPhaseMid;
    r.wobbleFast = uint8_t(fields.wobbleFast >> 8);
    r.wobbleMid = uint8_t(fields.wobbleMid >> 8);
    r.wobbleSlow = uint8_t(fields.wobbleSlow >> 8);
    r.top[0] = ribbon.pos.x;
    r.top[1] = ribbon.pos.y;
    r.top[2] = ribbon.pos.z;
    r.middle[0] = r.top[0];
    r.middle[1] = wrap16(r.top[1] + kMiddleRailDrop);
    r.middle[2] = r.top[2];
    r.bottom[0] = r.middle[0];
    r.bottom[1] = wrap16(r.middle[1] + kBottomRailDrop);
    r.bottom[2] = r.middle[2];
    for (int32_t k = kFadeBands; k != 0;) { // fading in: k = 15..0 after the decrement
        --k;
        r.band(0, k);
    }
    if (fields.steadyBands != 0) {
        const int32_t n = fields.steadyBands;
        if (n < 0)
            notPortedAt(kStormRibbonArea, "0x431fb0 negative middle count (a 2^32-segment loop in the exe)",
                        uint8_t(fields.steadyBands));
        for (int32_t i = 0; i < n; ++i) r.band(1, 0);
    }
    for (int32_t k = 1; k <= kFadeBands; ++k) r.band(2, k); // fading out
    // linked into OT[0x22] only past 0x68 bytes of packets: the DR_MODE and at least 3 quads
    if (r.quads.size() < kMinQuads) return;
    const float kNoOff[4][2] = {};
    for (const RibbonBuild::Quad& q : r.quads) dc::otQuadG4(ribbon.at, q.c, 1, q.v, kNoOff); // DR_MODE abr 1
}

void stormRibbonUpdate(const PlayerEnv&, uint32_t actor) {
    EffectActor& ribbon = effectActorAt(actor);
    switch (StormRibbonState(ribbon.state)) {
    case StormRibbonState::Start: stormRibbonStart(ribbon); return;
    case StormRibbonState::Draw: stormRibbonDraw(ribbon); return;
    default: notPortedAt(kStormRibbonArea, "a0 0x431f50 state (past the table 0x88cb20)", ribbon.state);
    }
}

} // namespace od::port
