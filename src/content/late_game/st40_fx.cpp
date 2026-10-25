// ST40's teleporter effects (pool 0xa0 stage type 0x1e, exe 0x43adb0).
//
// ST40's table [*0xa72588 = 0x901998 + 0x1e * 4] = 0x43adb0 (the actors carry kActorStageClass; the room-script records
// 0x8cc680 x 4 have the loop flags 0x0c, the sparks 0x0e: both run in the late loop exe 0x55be50). 0x43adb0 dispatches
// on the variant through 0x88d850: 0 0x43add0 (the teleporter pad's panel), 1 0x52ef00 (ret), 2 0x43b0a0 (the teleport
// ring), 3 0x43b7b0 (a rising spark spawned by the ring).
//
// Drawing: the OT GPU packets (DR_MODE + POLY_G4) go through the presentation side channel dc::otQuadG4 with the
// corners as camera-input points (the exe's GTE input: the panel and the ring use the actor's s16 position as is, no
// stage shift). The GTE scratch the exe writes (points 0x6d00c0.., screen xy 0x6d0110.., colours 0x6d0138..,
// 0x6d0180 / 0x6d01e8 / 0x6d01ec / 0x6d01f0.., 0x6d0174) is render-only and kept local here. The spark's LINE_G2
// through dc::otLineG2.
#include "content/late_game/st40_fx.hpp"
#include "content/late_game/st40_fx_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/draw_commands.hpp"
#include "game/exit_transitions_state.hpp"
#include "game/field_alias.hpp"
#include "game/game_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st40_fx_state;
using exit_transitions_state::TeleporterSparkFields;

namespace {

enum class TeleporterEffect : uint8_t { Panel = 0, None = 1, Ring = 2, Spark = 3 };
enum class RingState : uint8_t { Start = 0, Grow = 1, Hold = 2, Fade = 3 };
constexpr uint8_t kTeleporterEffectType = 0x1e;
constexpr uint32_t kRingStartPitch = 0xfffffbb4u; // -0x44c
constexpr uint32_t kRingStartRadius = 0x40, kRingGrowth = 0x40, kRingFullRadius = 0x2c0;
constexpr uint32_t kRingIntensity = 0x20, kRingFadeTicks = 0x20;
constexpr uint32_t kAdditivePage = 0x3e, kHalfPage = 0x1e;

// The panel's parameters: its facing (0: z + 0x20, 1: x + 0x20, 2: z - 0x20, other: x - 0x20), still (else animated
// from the frame counter), opaque (else semi-transparent), the faster animation
struct PanelFields {
    explicit PanelFields(const EffectActor& actor) : PanelFields(const_cast<EffectActor&>(actor)) {}
    explicit PanelFields(EffectActor& actor) : facing(actor.params[0]), still(actor.params[1]), opaque(actor.params[2]), fast(actor.params[3]) {}
    FieldAlias<int8_t, uint8_t> facing;
    uint8_t& still;
    uint8_t& opaque;
    uint8_t& fast;
};

// the point indices of the panel's four quads (exe data 0x88d860: 4 corners a quad)
const DataTable<ScalarTable<uint8_t, 16>> panelQuadPoints{TableId::St40FxPanelQuadPoints};
uint8_t panelQuadPoint(uint32_t quad, uint32_t corner) { return panelQuadPoints[quad * 4 + corner]; }

int16_t t16(int v) { return int16_t(uint16_t(uint32_t(v))); }
// the table's sine / cosine as the exe reads it, an unsigned short
uint32_t sineWord(uint32_t angle) { return uint16_t(fixedSin(int(angle & 0xfff))); }
uint32_t cosineWord(uint32_t angle) { return uint16_t(fixedCos(int(angle & 0xfff))); }
// GTE NCLIP of three projected points (exe 0x40c7b0 over the float screen fifo 0xa59a58..; here the truncated ones,
// as game/global_fx -- render output only)
int64_t nclip(const CameraPoint& p0, const CameraPoint& p1, const CameraPoint& p2) {
    return int64_t(p0.sx) * (p1.sy - p2.sy) + int64_t(p1.sx) * (p2.sy - p0.sy) + int64_t(p2.sx) * (p0.sy - p1.sy);
}
constexpr float kNoOff[4][2] = {};
int32_t depthRange() { return int32_t(stage().depthRange); }

// ---- the panel (exe 0x43add0) ---------------------------------------------------------------------------------------
// exe 0x43b9f0: a vertical 0x180 x 0x180 panel at the actor, its plane by its facing, from y - 0x180 down to y. Points:
// the corners 0 / 2 / 4 / 6, the edge midpoints 1 / 3 / 5 / 7 and the centre 8. Four quads (panelQuadPoints): RTPT
// (i0, i1, i2) + NCLIP, RTPS i3, the flag check (exe 0x40cae0: the flag of the last RTPS is 0 or 8, so its "& 0xf ==
// 0xf" test never skips), NCLIP (i1, i2, i3); kept when NCLIP1 > 0 or NCLIP2 < 0 and the deepest SZ >> 2 is in [1, the
// depth range); OT index 0x6d01f0[i] = (that + AVSZ4 (sum >> 4)) >> 1. Returns the mask of the kept quads.
uint32_t panelPoints(const EffectActor& actor, const PanelFields& panel, int16_t pt[9][3]) {
    const int16_t x = actor.pos.x, y = actor.pos.y, z = actor.pos.z;
    int dx[4], dz[4];
    switch (panel.facing.get()) {
    case 0: // 0x43bc15
        dx[0] = 0xc0, dz[0] = 0x20, dx[1] = -0xc0, dz[1] = 0x20, dx[2] = -0xc0, dz[2] = 0x20, dx[3] = 0xc0, dz[3] = 0x20;
        break;
    case 1: // 0x43bb6a
        dx[0] = 0x20, dz[0] = -0xc0, dx[1] = 0x20, dz[1] = 0xc0, dx[2] = 0x20, dz[2] = 0xc0, dx[3] = 0x20, dz[3] = -0xc0;
        break;
    case 2: // 0x43bac2
        dx[0] = -0xc0, dz[0] = -0x20, dx[1] = 0xc0, dz[1] = -0x20, dx[2] = 0xc0, dz[2] = -0x20, dx[3] = -0xc0, dz[3] = -0x20;
        break;
    default: // 0x43ba1b
        dx[0] = -0x20, dz[0] = 0xc0, dx[1] = -0x20, dz[1] = -0xc0, dx[2] = -0x20, dz[2] = -0xc0, dx[3] = -0x20, dz[3] = 0xc0;
        break;
    }
    const int dy[4] = {-0x180, -0x180, 0, 0};
    for (int k = 0; k < 4; ++k) {
        pt[k * 2][0] = t16(x + dx[k]);
        pt[k * 2][1] = t16(y + dy[k]);
        pt[k * 2][2] = t16(z + dz[k]);
    }
    const auto mid = [&](int o, int p, int q) {
        for (int c = 0; c < 3; ++c) pt[o][c] = t16((int(pt[p][c]) + int(pt[q][c])) >> 1);
    };
    mid(1, 0, 2);
    mid(3, 2, 4);
    mid(5, 4, 6);
    mid(7, 6, 0);
    mid(8, 0, 4);
    uint32_t mask = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        CameraPoint c[4];
        for (uint32_t j = 0; j < 4; ++j) c[j] = cameraPoint(pt[panelQuadPoint(i, j)]); // 0x40c730 / 0x40c930 / 0x40c860
        const int64_t n1 = nclip(c[0], c[1], c[2]);
        const uint32_t flag = c[3].clipped ? (8u | 0x80000000u) : 0u; // 0x40cae0 after the RTPS of i3
        if ((flag & 0xf) == 0xf) continue;
        const int64_t n2 = nclip(c[1], c[2], c[3]);
        if (!(n1 > 0 || n2 < 0)) continue;
        int32_t z4[4], sum = 0, mx;
        for (int j = 0; j < 4; ++j) {
            z4[j] = cameraPointDepth(c[j]);
            sum = int32_t(uint32_t(sum) + uint32_t(z4[j]));
        }
        mx = z4[0]; // the SZ fifo order of exe 0x40cf90 (SZ0..SZ3 = i0..i3), max kept as the exe
        for (int j = 1; j < 4; ++j)
            if (mx < z4[j]) mx = z4[j];
        mx >>= 2;
        if (mx >= depthRange() || mx < 1) continue;
        // OT index (mx + (sum >> 4)) >> 1 -> 0x6d01f0[i] (render order only)
        mask |= 1u << i;
    }
    return mask;
}
// exe 0x43add0: the panel's colours, one per point: animated (tpage 0x3e: additive) from the frame counter << (7, 8 when
// fast); still: tpage 0x1e (50 / 50). Bytes b[2j] = 0x50 - (sin[t] >> 6), b[2j + 1] = 0x40 - (sin[t + 0x200] >> 7)
// (t = frame term + j * 0x400); point i (0..7) = {b[i], b[i + 2], b[i + 4]} (& 7), the centre grey 0x30 - (cos[frame *
// 16] >> 8). Each kept quad: DR_MODE 0x40b680(p, 1, 1, tpage, 0) + POLY_G4 (code 0x3a semi-transparent, 0x38 opaque),
// linked at OT 0x6d01f0[i].
void drawPanel(EffectActor& actor) {
    const PanelFields panel(actor);
    int16_t pt[9][3];
    const uint32_t mask = panelPoints(actor, panel, pt); // exe 0x43b9f0
    uint8_t b[8], g;
    uint32_t tpage;
    if (panel.still == 0) {
        const uint32_t sh = (panel.fast != 0 ? 1u : 0u) + 7u;
        const uint32_t f = uint32_t(gameState().frame) << sh;
        for (uint32_t j = 0; j < 4; ++j) {
            const uint32_t t = j * 0x400;
            b[j * 2] = uint8_t(0x50 - uint8_t(sineWord(f + t) >> 6));
            b[j * 2 + 1] = uint8_t(0x40 - uint8_t(sineWord(f + t + 0x200) >> 7));
        }
        tpage = kAdditivePage;
        g = uint8_t(0x30 - uint8_t(cosineWord(uint32_t(uint8_t(gameState().frame)) << 4) >> 8));
    } else {
        for (uint32_t j = 0; j < 4; ++j) {
            const uint32_t t = j * 0x400;
            b[j * 2] = uint8_t(0x50 - uint8_t(sineWord(t) >> 6));
            b[j * 2 + 1] = uint8_t(0x40 - uint8_t(sineWord(t + 0x200) >> 7));
        }
        g = uint8_t(0x30 - uint8_t(cosineWord(0) >> 8));
        tpage = kHalfPage;
    }
    uint32_t col[9];
    for (uint32_t i = 0; i < 8; ++i)
        col[i] = uint32_t(b[(i + 4) & 7]) << 16 | uint32_t(b[(i + 2) & 7]) << 8 | b[i & 7];
    col[8] = uint32_t(g) * 0x10101u; // 0x6d0158..0x6d015a (its byte 3 is never vertex 0's: not the code)
    const uint32_t code = panel.opaque != 0 ? 0x38000000u : 0x3a000000u;
    for (uint32_t i = 0; i < 4; ++i) {
        if (!((mask >> i) & 1)) continue;
        const uint8_t k[4] = {panelQuadPoint(i, 0), panelQuadPoint(i, 1), panelQuadPoint(i, 2), panelQuadPoint(i, 3)};
        const uint32_t c[4] = {col[k[0]] | code, col[k[1]], col[k[2]], col[k[3]]};
        int16_t v[4][3];
        for (int j = 0; j < 4; ++j)
            for (int q = 0; q < 3; ++q) v[j][q] = pt[k[j]][q];
        dc::otQuadG4(actor.at, c, uint8_t((tpage >> 5) & 3), v, kNoOff);
    }
}

// ---- the teleport ring (exe 0x43b0a0) -------------------------------------------------------------------------------
// States: Start -> ticks 0, radius 0x40, pitch -0x44c, Grow; Grow: the radius + 0x40 up to 0x2c0 -> Hold; Hold (its
// first tick: ticks 0) waits for the release -> Fade; Fade (its first tick: ticks 0) fades over 0x20 ticks, then freed
// (exe 0x5e1580). Intensity I = 0x20 (0x20 - ticks while fading). Drawn: 32 quads around (x, z) at y: the ring points
// A_k at angle (frame + k) * 0x80, radius 0x140 (sin * 0x140 >> 12, -cos * 0x140 >> 12), and B_k = A_k moved by exe
// 0x5e3870(A_k, {pitch, (0xf - k - frame) * 0x80 & 0xfff, 0}, 0, 0, -(radius << 4)) (B_0 with (0xfff0 - frame) * 0x80);
// quad k = (A_k, B_k, A_k+1, B_k+1), colours {colour[k & 7] | 0x3a, black, colour[k + 1 & 7], black} (colour 0 = {I,
// 3I, 5I}, the next ones * 28 / 24 / 20 / 16 / 20 / 24 / 28 >> 5), DR_MODE 0x40b6d0(p, 1, 1, 0x3e) (additive) + POLY_G4
// at the AVSZ4 OTZ when 1 <= OTZ <= the depth range. Until it fades one spark a tick (4 RNG steps); the ticks count.
void ringPoint(uint32_t idx, int16_t x, int16_t y, int16_t z, int16_t out[3]) {
    out[0] = t16(((int32_t(fixedSin(int(idx))) * 5 << 6) >> 12) + x);
    out[1] = y;
    out[2] = t16(((-5 * int32_t(fixedCos(int(idx)))) << 6 >> 12) + z);
}
// the spark's spawn: at a random point 0x100 around the ring's centre, rising, its tail below it, a random life
void spawnSpark(const EffectActor& actor) {
    const uint32_t slot = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (slot == 0) return;
    EffectActor& spark = effectActorAt(slot);
    TeleporterSparkFields& fields = classState<TeleporterSparkFields>(spark);
    const uint8_t loop = spark.loopFlags;
    spark.flags = kActorLiveDrawn;
    spark.type = kTeleporterEffectType;
    spark.variant = uint8_t(TeleporterEffect::Spark);
    spark.loopFlags = uint8_t(loop | kActorSweepGroup2 | kActorLateLoop);
    const uint32_t i = rngStep() & 0xfff;
    spark.pos.x = t16((fixedSin(int(i)) >> 4) + actor.pos.x);
    spark.pos.y = actor.pos.y;
    const int16_t z = t16((fixedCos(int(i)) >> 4) + actor.pos.z);
    fields.spare = 0;
    fields.velocity[0] = 0;
    spark.pos.z = z;
    const uint32_t r1 = rngStep();
    fields.velocity[2] = 0;
    fields.velocity[1] = int8_t(0xf8 - (r1 & 3));
    fields.pad = 0;
    fields.tail[0] = 0;
    const uint32_t r2 = rngStep();
    fields.tail[2] = 0;
    fields.tail[1] = int8_t(0x18 - (r2 & 0xf));
    const uint32_t r3 = rngStep();
    fields.life = uint8_t(0x38 - (r3 & 0xf));
    fields.colour[0] = 0xc0;
    fields.colour[1] = 0xc0;
    fields.colour[2] = 0;
}
void drawRing(EffectActor& actor) {
    RingFields& ring = classState<RingFields>(actor);
    uint32_t I;
    switch (RingState(actor.state)) {
    case RingState::Start: // 0x43b12f
        ring.ticks = 0;
        ring.radius = kRingStartRadius;
        ring.pitch = int32_t(kRingStartPitch);
        I = kRingIntensity;
        actor.enterState(uint8_t(RingState::Grow));
        break;
    case RingState::Grow: { // 0x43b10e
        const uint32_t r = ring.radius + kRingGrowth;
        ring.radius = r;
        I = kRingIntensity;
        if (r == kRingFullRadius) actor.enterState(uint8_t(RingState::Hold));
        break;
    }
    case RingState::Hold: // 0x43b0ea
        if (actor.subState == 0) {
            ring.ticks = 0;
            ++actor.subState;
        }
        I = kRingIntensity;
        if (ring.release != 0) actor.enterState(uint8_t(RingState::Fade));
        break;
    default: { // 0x43b0bd
        if (actor.subState == 0) {
            ring.ticks = 0;
            ++actor.subState;
        }
        const uint32_t n = ring.ticks;
        I = kRingIntensity - n;
        if (n == kRingFadeTicks) {
            freeActor(actor); // exe 0x5e1580
            return;
        }
        break;
    }
    }
    // the colours 0x6d0138 + k * 4 (byte 3 zero)
    uint8_t c0[3] = {uint8_t(I), uint8_t((I * 3) << 5 >> 5), uint8_t((I * 5) << 5 >> 5)};
    static constexpr uint32_t kScale[8] = {0, 28, 24, 20, 16, 20, 24, 28};
    uint32_t col[8];
    col[0] = uint32_t(c0[2]) << 16 | uint32_t(c0[1]) << 8 | c0[0];
    for (uint32_t k = 1; k < 8; ++k) {
        uint32_t w = 0;
        for (uint32_t q = 0; q < 3; ++q) w |= uint32_t(uint8_t((uint32_t(c0[q]) * kScale[k]) >> 5)) << (q * 8);
        col[k] = w;
    }
    const int16_t x = actor.pos.x, y = actor.pos.y, z = actor.pos.z;
    const int32_t R = int32_t(ring.radius << 4);
    int16_t rot[3] = {int16_t(ring.pitch), 0, 0};
    const uint32_t f = gameState().frame;
    int16_t A[3], B[3], nA[3], nB[3];
    for (uint32_t k = 0; k < 0x20; ++k) {
        if (k == 0) { // 0x43b32a
            ringPoint((f & 0x1f) << 7, x, y, z, A);
            for (int q = 0; q < 3; ++q) B[q] = A[q];
            rot[1] = int16_t(((0xfff0u - f) << 7) & 0xfff);
            moveAlongRotation16(B, rot, 0, 0, -R);
            ringPoint(((f + 1) << 7) & 0xfff, x, y, z, nA);
            for (int q = 0; q < 3; ++q) nB[q] = nA[q];
            rot[1] = int16_t(((0xfu - f) << 7) & 0xfff);
            moveAlongRotation16(nB, rot, 0, 0, -R);
        } else { // 0x43b482
            for (int q = 0; q < 3; ++q) {
                A[q] = nA[q];
                B[q] = nB[q];
            }
            ringPoint(((f + k + 1) << 7) & 0xfff, x, y, z, nA);
            for (int q = 0; q < 3; ++q) nB[q] = nA[q];
            rot[1] = int16_t(((0xfu - k - f) << 7) & 0xfff);
            moveAlongRotation16(nB, rot, 0, 0, -R);
        }
        const int16_t v[4][3] = {{A[0], A[1], A[2]}, {B[0], B[1], B[2]}, {nA[0], nA[1], nA[2]}, {nB[0], nB[1], nB[2]}};
        int16_t sxy[4][2];
        const int32_t otz = projectQuad(v, sxy); // 0x40c3c0 .. 0x40c460 + 0x40cad0 -> 0x6d0174
        if (otz < 1 || otz > depthRange()) continue;
        const uint32_t c[4] = {col[k & 7] | 0x3a000000u, 0, col[(k + 1) & 7], 0};
        dc::otQuadG4(actor.at, c, (kAdditivePage >> 5) & 3, v, kNoOff); // DR_MODE 0x40b6d0(p, 1, 1, 0x3e)
    }
    if (actor.state < uint8_t(RingState::Fade)) spawnSpark(actor);
    ring.ticks += 1u;
}

// ---- a rising spark (exe 0x43b7b0) ----------------------------------------------------------------------------------
// Its age (the state) up to its life (then freed, exe 0x5e1580); colour * (life - age) / life; the head P0 = position
// << stage shift, the tail P1 = (position + tail) << shift; then the position moves by the velocity. Drawn by the exe
// as DR_MODE 0x40b6d0(p, 1, 1, 0x3e) (additive) + LINE_G2 0x52 (semi-transparent) {colour, P0} -> {black, P1} at OTZ
// (P0 depth >> 2 + P1 depth >> 2) >> 1 when 1 <= OTZ <= the depth range. Port: dc::otLineG2 with the ends as
// camera-input points (no screen offset).
void drawSpark(EffectActor& actor) {
    TeleporterSparkFields& spark = classState<TeleporterSparkFields>(actor);
    // colour word 0x6d0138 (zeroed, then bytes r / g / b; the exe divides by the life unguarded)
    const uint32_t d = spark.life;
    const uint32_t left = d - actor.state;
    uint32_t colour = 0;
    if (d != 0)
        colour = uint32_t(uint8_t((uint32_t(spark.colour[2]) * left) / d)) << 16 |
                 uint32_t(uint8_t((uint32_t(spark.colour[1]) * left) / d)) << 8 |
                 uint8_t((uint32_t(spark.colour[0]) * left) / d);
    const int sh = stage().coordinateShift & 0x1f; // shl by cl
    const int16_t v[2][3] = {
        {t16(int(uint16_t(actor.pos.x)) << sh), t16(int(uint16_t(actor.pos.y)) << sh),
         t16(int(uint16_t(actor.pos.z)) << sh)},
        {t16(int(t16(spark.tail[0] + actor.pos.x)) << sh), t16(int(t16(spark.tail[1] + actor.pos.y)) << sh),
         t16(int(t16(spark.tail[2] + actor.pos.z)) << sh)}};
    const CameraPoint c0 = cameraPoint(v[0]), c1 = cameraPoint(v[1]); // 0x40c6a0 / 0x40c860 / 0x40d010
    const int32_t otz = ((cameraPointDepth(c0) >> 2) + (cameraPointDepth(c1) >> 2)) >> 1; // 0x6d0174
    if (otz >= 1 && otz <= depthRange()) {
        const uint32_t c[2] = {colour | 0x52000000u, 0}; // DR_MODE 0x40b6d0(p, 1, 1, 0x3e): abr 1
        static constexpr float kNoOff2[2][2] = {};
        dc::otLineG2(actor.at, c, (kAdditivePage >> 5) & 3, v, kNoOff2);
    }
    actor.pos.x = int16_t(uint16_t(actor.pos.x) + uint32_t(int32_t(spark.velocity[0])));
    actor.pos.y = int16_t(uint16_t(actor.pos.y) + uint32_t(int32_t(spark.velocity[1])));
    actor.pos.z = int16_t(uint16_t(actor.pos.z) + uint32_t(int32_t(spark.velocity[2])));
    if (actor.state == spark.life) {
        freeActor(actor); // exe 0x5e1580
        return;
    }
    ++actor.state;
}

// exe 0x43adb0: [0x88d850 + variant * 4]
void teleporterEffect(EffectActor& actor) {
    switch (TeleporterEffect(actor.variant)) {
    case TeleporterEffect::Panel: drawPanel(actor); return;
    case TeleporterEffect::None: return; // 0x52ef00: ret
    case TeleporterEffect::Ring: drawRing(actor); return;
    case TeleporterEffect::Spark: drawSpark(actor); return;
    default: notPortedAt(kStoryPort, "0x43adb0 sub (table 0x88d850)", actor.variant);
    }
}

} // namespace

void teleporterEffectUpdate(const PlayerEnv&, uint32_t actor) { teleporterEffect(effectActorAt(actor)); }

} // namespace od::port
