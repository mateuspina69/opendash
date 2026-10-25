// The ground shockwave and its ring (see shockwave.hpp).
//   shock 0x5c3ae0, states [0x8d115c]: 0 0x5c3b20 (set up), 1 0x5c3c30 (variant | 0x80: the sequencer 0x5c3e40, else
//         the ring 0x5c3c50); 3 / 4 (0x5c4250) are not reached by the ported callers
//   ring visual 0x42ec70: its quads (render output: dc::otQuadG4) and its RNG steps; freed with its shock
#include "game/loaded_data.hpp"
#include "game/actor_state.hpp"
#include "game/shockwave.hpp"
#include "game/shockwave_state.hpp"

#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/draw_commands.hpp"
#include "game/fixed_point.hpp"
#include "game/hit_list.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/stage_state.hpp"

namespace od::port {

using namespace shockwave_state;

namespace {

constexpr const char* kShockwavePort = "shockwave port"; // the area of its port-gap errors

enum class ShockwaveState : uint8_t { Setup = 0, Run = 1 };
constexpr int32_t kShockDiscRadius = 0x800;
constexpr int8_t kShockStartPhase = 8;
constexpr uint8_t kShockEnded = 0x20;

// A ring's parameters (exe table 0x8d0fd8, 12 bytes per variant)
struct ShockRingParams : DataRecord {
    static constexpr uint32_t kBytes = 0xc;
    uint16_t radius = 0; // +0x0
    uint16_t height = 0; // +0x2
    uint16_t growth = 0; // +0x4
    uint16_t damage = 0; // +0x6
    int8_t phaseStep = 0; // +0x8
    std::array<uint8_t, 3> colour{}; // +0x9
    template <class R> static ShockRingParams load(const R& r) {
        ShockRingParams v;
        v.radius = r.template read<uint16_t>(0x0);
        v.height = r.template read<uint16_t>(0x2);
        v.growth = r.template read<uint16_t>(0x4);
        v.damage = r.template read<uint16_t>(0x6);
        v.phaseStep = r.template read<int8_t>(0x8);
        for (size_t i = 0; i < v.colour.size(); ++i) v.colour[i] = r.template read<uint8_t>(0x9 + i * sizeof(uint8_t));
        return v;
    }
};
constexpr uint32_t kShockRingParamsSize = 12;
// A sequencer step (exe data, 4 bytes; a negative variant ends the sequence)
struct ShockStep : DataRecord {
    static constexpr uint32_t kBytes = 0x4;
    int8_t variant = 0; // +0x0
    uint8_t hitMode = 0; // +0x1
    int16_t delay = 0; // +0x2
    template <class R> static ShockStep load(const R& r) {
        ShockStep v;
        v.variant = r.template read<int8_t>(0x0);
        v.hitMode = r.template read<uint8_t>(0x1);
        v.delay = r.template read<int16_t>(0x2);
        return v;
    }
};
constexpr uint32_t kShockStepSize = 4;
// by variant & 0x7f (exe data 0x8d1148): the sequences' addresses, as the shock stores them (the address is the record's identity)
const DataTable<ScalarTable<uint32_t, 0x80>> shockSequences{TableId::ShockwaveSequences};

constexpr uint8_t kShockRingAbr = (0x20 >> 5) & 3; // the DR_MODE tpage 0x20: semi-transparency mode 1
constexpr uint32_t kShockRingOpcode = 0x3a000000; // POLY_G4, semi-transparent

// the 64-step sine of a phase that may run past the table (the exe's unchecked index)
int32_t phaseSine(int8_t phase) { return trig64Tables().sine[size_t(int32_t(phase))]; }

void shockTick(ProjectileActor& s, ShockwaveFields& f);

// exe 0x5c3c50: the ring's visual updated, its hit entry, the radius grown, the phase stepped; past the phase the visual
// told to end and the shock freed
void ringTick(ProjectileActor& s, ShockwaveFields& f) {
    EffectActor& fx = *f.ring;
    ShockRingFxFields& r = classState<ShockRingFxFields>(fx);
    fx.pos.x = s.pos.x.whole;
    fx.pos.y = s.pos.y.whole;
    fx.pos.z = s.pos.z.whole;
    r.radius = f.radius;
    r.height = f.height;
    r.colour = f.colour;
    r.phase = f.phase;
    const uint32_t owner = hitOwner(s.slot);
    if (f.hitMode == kShockHitEdge) {
        if (player().airborne == 0) {
            // the ring's point toward the player: Ry(the angle of the player from the shock) (0, 0, radius) (the matrix
            // 0x6d00c0, the point 0x6d0120, the view 0x6d0160: render-side scratch, not written), its height by the
            // phase
            const int32_t h = phaseSine(f.phase) * f.height >> 0xd;
            const int ang = angleOf(s.pos, player().pos);
            const int16_t v16[3] = {0, 0, f.radius};
            int32_t v[3];
            transformVector(rotationY(int(uint32_t(ang) & kAngleMask)), v16, v); // exe 0x40bea0 / 0x40c800
            const int16_t pt[3] = {wrap16(s.pos.x.whole + v[0]), wrap16(s.pos.y.whole - h),
                                   wrap16(s.pos.z.whole + v[2])};
            hitListSubmit(pt, f.hitBox, uint32_t(int32_t(uint32_t(h) | 0x1a00000) >> 2) | owner,
                          uint32_t(int32_t(f.damage)) | kHitAttackStrong | kHitAttackThrown);
        }
    } else if (f.hitMode == kShockHitDisc) {
        const int16_t pt[3] = {s.pos.x.whole, s.pos.y.whole, s.pos.z.whole};
        int32_t radius = f.radius;
        if (radius > kShockDiscRadius) radius = kShockDiscRadius;
        hitListSubmit(pt, f.hitBox, uint32_t((radius | 0x2a00000) >> 2) | owner,
                      uint32_t(int32_t(f.damage)) | kHitAttackHarmless | kHitAttackStrong | kHitAttackThrown);
    }
    f.radius = int16_t(f.radius + s.velocity.x);
    const uint8_t ph = uint8_t(uint8_t(f.phase) + uint8_t(s.velocity.y));
    f.phase = int8_t(ph);
    if (ph & kShockEnded) {
        r.done = 1;
        freeActor(s);
    }
}

// exe 0x5c3e40: after its delay, the sequence's next steps spawned (each a shock of the step's variant at this one's
// place) until a step with a delay; a negative variant ends it (freed)
void shockSequence(ProjectileActor& s, ShockwaveFields& f) {
    if (f.delay != 0) {
        f.delay = int16_t(f.delay - 1);
        return;
    }
    uint32_t p = f.sequence;
    int16_t delay;
    do {
        const ShockStep step = dataRecord<ShockStep>(p, ShockStep::kBytes);
        if (step.variant < 0) {
            freeActor(s);
            return;
        }
        if (const uint32_t address = allocActor40(kActorStageClass)) { // exe 0x5e1280
            ProjectileActor& c = projectileActorAt(address);
            ShockwaveFields& cf = classState<ShockwaveFields>(c);
            c.flags = kActorLive;
            c.type = kShockwaveStageType;
            c.variant = uint8_t(step.variant);
            c.loopFlags |= kActorSweepGroup2;
            cf.hitMode = int8_t(step.hitMode);
            cf.downward = f.downward;
            c.pos = s.pos;
        }
        delay = step.delay;
        p += kShockStepSize;
        f.delay = delay;
    } while (delay == 0);
    f.sequence = p;
}

void shockTick(ProjectileActor& s, ShockwaveFields& f) { // exe 0x5c3c30
    if (s.variant & kShockwaveSequencer) shockSequence(s, f);
    else ringTick(s, f);
}

void shockInit(ProjectileActor& s, ShockwaveFields& f) { // exe 0x5c3b20
    s.enterState(uint8_t(ShockwaveState::Run));
    f.clearBlockWords();
    const uint8_t v = s.variant;
    if (v & kShockwaveSequencer) {
        f.delay = 0;
        f.sequence = shockSequences[v & 0x7f];
        shockTick(s, f);
        return;
    }
    const ShockRingParams t = dataRecord<ShockRingParams>(stateAddress(TableId::ShockwaveRingParams) + uint32_t(v) * kShockRingParamsSize, ShockRingParams::kBytes);
    f.radius = int16_t(t.radius);
    f.height = int16_t(t.height);
    s.velocity.x = int16_t(t.growth);
    s.velocity.y = int16_t(t.phaseStep);
    f.damage = int16_t(t.damage);
    f.phase = kShockStartPhase;
    f.colour = uint32_t(t.colour[0]) | uint32_t(t.colour[1]) << 8 | uint32_t(t.colour[2]) << 16;
    f.hitBox.previousX = s.pos.x.whole;
    f.hitBox.previousZ = s.pos.z.whole;
    const uint32_t address = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (!address) {
        freeActor(s);
        return;
    }
    EffectActor& fx = effectActorAt(address);
    ShockRingFxFields& r = classState<ShockRingFxFields>(fx);
    fx.flags = kActorLive;
    fx.loopFlags |= uint8_t(kActorSweepGroup2 | kActorLateLoop);
    fx.type = kShockRingFxStageType;
    r.done = 0;
    r.downward = f.downward;
    const int q = int(f.phase) / int(s.velocity.y); // CDQ / IDIV
    r.shock = &s;
    r.depthExtra = int16_t((q + 1) * s.velocity.x + f.radius);
    f.ring = &fx;
    shockTick(s, f);
}

void shockwave(ProjectileActor& s) { // exe 0x5c3ae0
    ShockwaveFields& f = classState<ShockwaveFields>(s);
    s.savePreviousPose();
    s.wasAirborne = s.airborne;
    switch (ShockwaveState(s.state)) {
    case ShockwaveState::Setup: shockInit(s, f); return;
    case ShockwaveState::Run: shockTick(s, f); return;
    default: notPortedAt(kShockwavePort, "0x5c3ae0 state (0x5c4250)", s.state);
    }
}

// exe 0x42ec70: the ring's centre in view space (the game camera, integer); while its depth is within the stage's far
// distance and in front (>= 0xc0) its 16 segments: a wall of gouraud quads around the centre (the 64-step tables at
// every 4th step, k = 15 .. 0), base at the centre's height, top at y -/+ (h + (h >> (RNG & 3))) (h by the phase; down
// when downward), one RNG step a segment: DR_MODE (tpage 0x20 = abr 1) + POLY_G4 0x3a {top corners black, base corners
// the colour}. Port: dc::otQuadG4 with the corners as camera-input points. Freed once done or its shock is gone.
void shockRingFx(EffectActor& e) {
    ShockRingFxFields& f = classState<ShockRingFxFields>(e);
    const int16_t c[3] = {e.pos.x, e.pos.y, e.pos.z};
    const CameraPoint cp = cameraPoint(c);
    if (int32_t(f.depthExtra) + cp.view[2] < int32_t(uint32_t(stage().depthRange) * 4) && cp.view[2] >= 0xc0) {
        const int32_t x = c[0], y = c[1], z = c[2], rad = f.radius;
        const int32_t h = (phaseSine(f.phase) * int32_t(f.height)) >> 13;
        const uint32_t col = f.colour;
        const uint32_t cw[4] = {kShockRingOpcode, col, 0, col};
        static constexpr float kNoOff[4][2] = {};
        int16_t px = wrap16(((int32_t(cos64(0)) * rad) >> 12) + x);
        int16_t pz = wrap16(((int32_t(sin64(0)) * rad) >> 12) + z);
        for (uint32_t k = 0; k < 16; ++k) {
            const uint32_t r = rngStep();
            const int16_t top = f.downward == 0 ? wrap16(y - (h >> (r & 3)) - h) : wrap16(h - (h >> (r & 3)) + y);
            const uint32_t step = 60 - k * 4;
            const int16_t nx = wrap16(x + ((int32_t(cos64(step)) * rad) >> 12));
            const int16_t nz = wrap16(z + ((int32_t(sin64(step)) * rad) >> 12));
            const int16_t v[4][3] = {{px, top, pz}, {px, int16_t(y), pz}, {nx, top, nz}, {nx, int16_t(y), nz}};
            dc::otQuadG4(e.at, cw, kShockRingAbr, v, kNoOff);
            px = nx;
            pz = nz;
        }
    }
    if (f.done != 0 || f.shock->flags == 0) freeActor(e);
}

} // namespace

void shockwaveUpdate(const PlayerEnv&, uint32_t actor) { shockwave(projectileActorAt(actor)); }

void shockRingUpdate(const PlayerEnv&, uint32_t actor) { shockRingFx(effectActorAt(actor)); }

} // namespace od::port
