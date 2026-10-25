// The ST0E pad-steered debug beam, class 0x5cebe0 (pool 0x40 stage type 0x28): a debug object (no spawner found in the
// exe's code or data). States (the exe's table 0x8d178c): 0x5cec00 the init (the length step 8, the late loop, the
// width by the variant 0x8d17a8); 0x5cec30 steered by the second pad while square is held -- R1 / R2 the yaw +-8, up /
// down the pitch +-8, right / left the length +- the step (not below 0), L1 pressed resets the angles; else triangle
// pressed fades it out; 0x5ced60 the width thins by 2 a tick, then freed (exe 0x5e1500). Its beam-box test against the
// player (exe 0x5e5b40 with the rotation exe 0x40bfd0) has no effect (the result is unused) and is left out. Drawn as
// the ST35 beam 0x5b9c40 / 0x5b9d90 (content/nino/nino_ruin_st35): the ring colours [0x8d1798 + variant * 8], the body
// colours [0x8d17a0 + variant * 8]; differences kept: each ring is emitted only when exe 0x5f2f20(the last point, 8)
// passes (the second ring tests the start point again), and the body stops at the first segment off screen. Its
// shadow (exe 0x5f20f0) is render output and left out.
#include "content/yosyonke/st0e_debug_beam.hpp"
#include "content/yosyonke/st0e_debug_beam_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/draw_commands.hpp"
#include "game/player/player_input.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st0e_debug_beam_state;

namespace {

int16_t t16(int v) { return int16_t(uint16_t(uint32_t(v))); }

enum class DebugBeamState : uint8_t {
    Init = 0,   // exe 0x5cec00
    Steer = 1,  // exe 0x5cec30
    FadeOut = 2, // exe 0x5ced60
};

// (the length's step of the steering is kept in the velocity's z lane)

// The beam's colour rows (exe data, two dwords each by variant)
struct BeamRingColours : DataRecord {
    uint32_t ring = 0; // the ring's colour (low 24 bits)
    uint32_t rim = 0;  // the ring's second word (| 0xf0000000)
    static BeamRingColours load(const TableReader& r) {
        BeamRingColours rec;
        rec.ring = r.u32(0x0);
        rec.rim = r.u32(0x4);
        return rec;
    }
};
struct BeamBodyColours : DataRecord {
    uint32_t centre = 0; // the body's centre corners (low 24 bits)
    uint32_t edge = 0;   // its edge corners
    static BeamBodyColours load(const TableReader& r) {
        BeamBodyColours rec;
        rec.centre = r.u32(0x0);
        rec.edge = r.u32(0x4);
        return rec;
    }
};
const DataTable<RecordTable<BeamRingColours, 4, 8>> beamRingColours{TableId::St0eDebugBeamRingColours};
const DataTable<RecordTable<BeamBodyColours, 4, 8>> beamBodyColours{TableId::St0eDebugBeamBodyColours};
const DataTable<ScalarTable<uint8_t, 4>> beamWidths{TableId::St0eDebugBeamWidths};

constexpr uint16_t kSteerHeld = kPadSquare;   // the second pad's buttons the steering reads
constexpr uint16_t kYawUp = 0x2000, kYawDown = 0x4000;
constexpr uint16_t kPitchUp = 0x10, kPitchDown = 0x40;
constexpr uint16_t kLonger = 0x20, kShorter = 0x80;
constexpr uint16_t kResetAngles = 0x1000, kFadeOut = 0x4000; // pressed
constexpr int16_t kLengthStep = 8, kAngleStep = 8;
constexpr int32_t kSegmentLength = 0x80;

// exe 0x5e37b0: the beam moved along its rotation
void moveForward(ProjectileActor& actor, int x, int y, int z) { moveAlongRotation(actor.pos, actor.rotation, x, y, z); }
// a point in the camera's input units (<< the stage shift)
void cameraPoint(const int16_t p[3], int16_t out[3]) {
    const int sh = stage().coordinateShift & 0x1f;
    for (int k = 0; k < 3; ++k) out[k] = t16(p[k] << sh);
}

// exe 0x5cedb0: the two rings (at the start and at the far end), the far end kept, the body's span = -length
void beamRings(ProjectileActor& actor) {
    DebugBeamFields& f = classState<DebugBeamFields>(actor);
    const BeamRingColours colours = beamRingColours[actor.variant];
    const int32_t x = actor.pos.x, y = actor.pos.y, z = actor.pos.z;
    const int32_t len = f.length;
    const uint32_t ring = (colours.ring & 0xffffffu) | 0x22000000u;
    f.lastPoint.x = actor.pos.x.whole;
    f.lastPoint.y = actor.pos.y.whole;
    f.lastPoint.z = actor.pos.z.whole;
    for (int i = 0; i < 2; ++i) {
        const int16_t p[3] = {f.lastPoint.x, f.lastPoint.y, f.lastPoint.z};
        int16_t depth = 0;
        if (projectedWidth(p, 8, &depth) != 0) // exe 0x5f2f20
            dc::emit({0x4000u, colours.rim | 0xf0000000u,
                      (uint32_t(uint16_t(actor.pos.y.whole)) << 16) | uint16_t(actor.pos.x.whole), ring,
                      uint32_t(uint16_t(actor.pos.z.whole)), uint32_t(int32_t(f.width)) << 16},
                     actor.at);
        f.lastPoint.x = actor.pos.x.whole;
        f.lastPoint.y = actor.pos.y.whole;
        f.lastPoint.z = actor.pos.z.whole;
        moveForward(actor, 0, 0, int(uint32_t(len) * 0xfffffff0u));
    }
    actor.pos.x = x;
    actor.pos.y = y;
    actor.pos.z = z;
    f.bodyLength = -len;
}

// exe 0x5ceef0: the body, segment by segment from the start to the far end: four additive POLY_G4 a segment
void beamBody(ProjectileActor& actor) {
    DebugBeamFields& f = classState<DebugBeamFields>(actor);
    const BeamBodyColours colours = beamBodyColours[actor.variant];
    int32_t len = -f.bodyLength;
    int n = len / kSegmentLength;
    if (len % kSegmentLength != 0) ++n;
    n = int8_t(n);
    const int32_t x = actor.pos.x, y = actor.pos.y, z = actor.pos.z;
    const int width = f.width;
    const int sh = stage().coordinateShift & 0x1f;
    const float u = float(width << sh);
    const uint32_t centre = (colours.centre & 0xffffffu) | 0x3a000000u, edge = (colours.edge & 0xffffffu) | 0x3a000000u;
    for (int k = 0; k < n; ++k) {
        int16_t p0[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole}, p1[3];
        int16_t s0[2], s1[2], d0 = 0, d1 = 0;
        projectOnScreen(p0, s0); // exe 0x5f2dd0
        const int w0 = projectedWidth(p0, int16_t(width), &d0);
        if (len > kSegmentLength) {
            moveForward(actor, 0, 0, -0x800);
            len -= kSegmentLength;
            p1[0] = actor.pos.x.whole;
            p1[1] = actor.pos.y.whole;
            p1[2] = actor.pos.z.whole;
        } else {
            p1[0] = f.lastPoint.x;
            p1[1] = f.lastPoint.y;
            p1[2] = f.lastPoint.z;
        }
        projectOnScreen(p1, s1);
        const int w1 = projectedWidth(p1, int16_t(width), &d1);
        const bool clip0 = uint16_t(s0[0]) == 0x8000 && uint16_t(s0[1]) == 0x8000;
        const bool clip1 = uint16_t(s1[0]) == 0x8000 && uint16_t(s1[1]) == 0x8000;
        if (w1 == 0 || clip1 || w0 == 0 || clip0) break;
        int16_t v0[3], v1[3];
        cameraPoint(p0, v0);
        cameraPoint(p1, v1);
        // 4 additive POLY_G4 per segment (exe 0x40b5f0 / 0x40b630 semi, the draw mode exe 0x40b6d0(1, 0x20)): {dx, dy}
        // of the edge corners up, down, right, left; the centre corners carry the centre colour
        static constexpr float kDir[4][2] = {{0, -1}, {0, 1}, {1, 0}, {-1, 0}};
        for (const auto& dd : kDir) {
            const bool edgeFirst = dd[1] > 0 || dd[0] > 0; // quads 2 / 3 start with the edge corner
            int16_t v[4][3];
            float off[4][2];
            uint32_t c[4];
            for (int j = 0; j < 4; ++j) {
                const bool onEdge = (j & 1) ? !edgeFirst : edgeFirst;
                for (int q = 0; q < 3; ++q) v[j][q] = j < 2 ? v0[q] : v1[q];
                off[j][0] = onEdge ? dd[0] * u : 0.0f;
                off[j][1] = onEdge ? dd[1] * u : 0.0f;
                c[j] = onEdge ? edge : centre;
            }
            c[0] |= 0x3a000000u;
            dc::otQuadG4(actor.at, c, 1, v, off);
        }
    }
    actor.pos.x = x;
    actor.pos.y = y;
    actor.pos.z = z;
}

// exe 0x5cebe0
void debugBeam(ProjectileActor& actor) {
    DebugBeamFields& f = classState<DebugBeamFields>(actor);
    switch (DebugBeamState(actor.state)) {
    case DebugBeamState::Init:
        actor.state = uint8_t(DebugBeamState::Steer);
        actor.velocity.z = kLengthStep;
        actor.loopFlags |= kActorLateLoop;
        f.width = int8_t(beamWidths[actor.variant]);
        return;
    case DebugBeamState::Steer: {
        PadState& pad = pad1();
        if (pad.held & kSteerHeld) {
            if (pad.held & kYawUp) actor.rotation.y += kAngleStep;
            if (pad.held & kYawDown) actor.rotation.y -= kAngleStep;
            if (pad.pressed & kResetAngles) {
                actor.rotation.y = 0;
                actor.rotation.x = 0;
            }
            if (pad.held & kPitchUp) actor.rotation.x += kAngleStep;
            if (pad.held & kPitchDown) actor.rotation.x -= kAngleStep;
            if (pad.held & kLonger) f.length += int32_t(actor.velocity.z);
            if (pad.held & kShorter) {
                const int32_t l = f.length - actor.velocity.z;
                f.length = l < 0 ? 0 : l;
            }
        } else if (pad.pressed & kFadeOut) {
            actor.state = uint8_t(DebugBeamState::FadeOut);
        }
        beamRings(actor);
        beamBody(actor);
        return;
    }
    case DebugBeamState::FadeOut:
        if (f.width >= 1) f.width -= 2;
        else freeActor(actor); // exe 0x5e1500
        beamRings(actor);
        beamBody(actor);
        return;
    default: notPortedAt(kStoryPort, "0x5cebe0 state (table 0x8d178c)", actor.state);
    }
}

} // namespace

void debugBeamUpdate(const PlayerEnv&, uint32_t actor) { debugBeam(projectileActorAt(actor)); }

} // namespace od::port
