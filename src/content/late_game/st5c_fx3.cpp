// ST5C: three small effect classes spawned by the boss pieces of content/late_game/st5c_fx2.
//
// The beam's impact (static type 0x61, exe 0x5bf360; its block from +0x28: a timer, a word, 32 angles, 32 + 32 bytes,
// a copy of its parameter word; states [0x8d0ddc], which are the entries 5 / 6 of 0x5be190's table 0x8d0dc8): spawned
// by 0x5cf370 where the room blocks the beam, at the integer x / z of the blocked point, y 0. 0 0x5bf380 (timer 4, the
// 32 angles / bytes randomised (never read again by this class), drawn, state 1), 1 0x5bf460 (timer -1; at 0 freed (exe
// 0x5e1540), else drawn). The draw 0x5bf490: the point on the screen (exe 0x5f2dd0) and its width for 0x50 (exe
// 0x5f2f20): three screen rings (grey (timer << 6) - 1: a disc of radius w * 3 / 2, a ring to ((rng & 0x1f) + 0x140) * w
// >> 8, an additive one to ((rng & 0x1f) + 0x200) * w >> 8), written with dc::emit exactly as the exe
// (game/draw_commands does not draw the screen-ring records).
//
// The shock ring (pool 0xa0 type 2, exe 0x42e360; states [0x88ca4c]: 0 0x42e380, 1 by the role [0x88ca58] (0 0x42e400,
// 1 / 2 0x42e4a0), 2 0x4358c0 (freed, exe 0x5e1580), 3 0x42e400, 4 / 5 0x42e4a0): a frame-buffer shock ring. Its bytes:
// the outer radius >> 4, the count (0xff = endless), the period, the s8 radius speed, the ring width >> 4, the s16
// point, the s16 radius, the toggle, the spawner. The explosion 0x5c3690 creates the spawner (role 0, the parameters 00
// 0c f0 0c) and keeps its outer radius = its own radius >> 4 (0 at the end). The spawner (0x42e400): the outer radius or
// the count 0 -> freed next tick; every period frames (the frame counter % period) a ring child (same type, the loop
// flags / point / outer radius / parameters copied, its spawner = this, its role = the spawner's variant or else 2 / 1
// alternately by the toggle), the count counted down unless 0xff. A ring (0x42e380 then 0x42e4a0): the radius from the
// outer radius << 4 (speed < 0) or -(width << 4); each tick the outer radius follows the live spawner (a dead one is
// dropped), the outer radius 0 or the radius out of [-(width << 4), outer << 4] -> freed next tick, else radius +=
// speed, then the picture.
// Picture (not drawn: no port primitive; the app's ScreenRipple pass is the nearest one): the identity in the GTE
// transform 0x6d0100 (game state, written), the point through the camera; drawn only at view depth 0xc0 .. the depth
// range * 4 and inside the 0x5f2dd0 guard band; the radii max(radius, 0) and min(width << 4, outer - radius) projected
// (points straight above in view space), 10 rings of 16 POLY_FT4 (0x2c808080, OT [0xa67840] + 0x7c) whose corners /
// uvs sit on radii scaled by the pairs of [0x88ca64] (odd roles outward, even inward), uv = the screen position
// (clamped 0..0xff / 0..0xef) in the draw buffer's tpage ([0x88ca48 + the draw buffer * 2]): the frame buffer redrawn
// displaced (a shock ring).
//
// The electric spark (pool 0xe0 type 6, exe 0x418660; its bytes: a pointer to a u16 radius (the explosion's +0xd4),
// the s16 point, its angle, length, timer and depth; states [0x88a104]): an electric spark of the explosion. 0 0x418680
// (angle rng & 0xfff, length ((rng & 0x3f) + 0xa0) * radius >> 8 + 1, timer 8, state 1); 1 0x4186f0 (radius 0 ->
// freed; the length re-rolled when above the radius; exe 0x5f2f20(point, length, depth) = W; W > 0: from the projected
// point a zigzag of OT POLY_G4 triples (DR_MODE 0xe1000220: abr 1) from W / 4 out to W, each step ((rng & 0x7f) +
// 0x40) * W >> 11 + 1 longer and turned by (rng & 0x7f) - 0x40, half-width (rng & 3) (x 4 + 0x40 for the outer edges),
// colours by depth and timer (dark edges, bright core); then timer -1, at 0 freed (exe 0x5e15c0)). Drawn through
// dc::otQuadG4 as content/late_game/st5c_fx's screen quads (screen-pixel corners brought back to the point's depth).
//
// Spawned, not ported: none (the shock ring spawns only its own type).
#include "content/late_game/st5c_fx3.hpp"
#include "content/late_game/st5c_fx3_state.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/camera_state.hpp"
#include "game/draw_commands.hpp"
#include "game/game_state.hpp"
#include "game/geometry_scratch.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st5c_fx3_state;

namespace {

int16_t t16(int64_t v) { return int16_t(uint16_t(uint64_t(v))); }
int32_t mul(int32_t a, int32_t b) { return int32_t(uint32_t(a) * uint32_t(b)); } // imul (32-bit wrap)
// cdq / and edx, 0xfff / add / sar 12: the signed division by 0x1000 rounding toward zero
int32_t div4096(int32_t v) { return int32_t(uint32_t(v) + (uint32_t(v >> 31) & 0xfffu)) >> 12; }
uint32_t shiftByte() { return stage().coordinateShift; } // the stage shift
// a 6-word ring / screen-ring draw record at the draw cursor
void ringRecord(uint32_t owner, uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3, uint32_t w4, uint32_t w5) {
    dc::emit({w0, w1, w2, w3, w4, w5}, owner);
}
// the camera-input point of an s16 point (<< the stage shift, as exe 0x5f2dd0 feeds the GTE)
void camPoint(const int16_t p[3], int16_t out[3]) {
    const uint32_t s = shiftByte() & 0x1f;
    for (int k = 0; k < 3; ++k) out[k] = t16(int64_t(uint32_t(int32_t(p[k])) << s));
}
// An OT POLY_G4 whose corners the exe puts at the screen-pixel offsets px[k] from the projected point of p: the port
// keeps p's camera-input point and brings the offsets back to camera-input units at p's depth (L = the width given
// to exe 0x5f2f20, w its result; as content/late_game/st5c_fx screenG4)
void screenG4(uint32_t owner, const int16_t p[3], int L, int w, const uint32_t c[4], uint8_t abr, const int px[4][2]) {
    const float scale = float(L << (shiftByte() & 0x1f)) / float(w);
    int16_t cp[3];
    camPoint(p, cp);
    int16_t v[4][3];
    float off[4][2];
    for (int k = 0; k < 4; ++k) {
        for (int j = 0; j < 3; ++j) v[k][j] = cp[j];
        off[k][0] = float(px[k][0]) * scale;
        off[k][1] = float(px[k][1]) * scale;
    }
    dc::otQuadG4(owner, c, abr, v, off);
}

// ---- the beam's impact (static type 0x61, exe 0x5bf360) -------------------------------------------------------------
enum class ImpactState : uint8_t { Start = 0, Run = 1 };


// exe 0x5bf490(actor, block): three screen rings at the projected point (none when it does not project / has no width)
void drawImpact(const StaticActor& actor, const ImpactFields& impact) {
    const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    int16_t sxy[2];
    if (projectOnScreen(p, sxy) < 0) return; // exe 0x5f2dd0
    int16_t depth = 0;
    const int w = projectedWidth(p, 0x50, &depth); // exe 0x5f2f20
    if (w == 0) return;
    const uint32_t c = (uint32_t(uint16_t(impact.timer << 6)) - 1u) & 0xffffu; // shl dx, 6 / dec edx / and 0xffff
    const uint32_t hi = (((c | 0xfffff400u) << 8) | c) << 8;
    const uint32_t xy = uint32_t(uint16_t(sxy[0])) | uint32_t(uint16_t(sxy[1])) << 16;
    const uint32_t dz = uint32_t(uint16_t(depth)) | 0x20000u;
    ringRecord(actor.at, 0x4000, hi | c, xy, dc::kSemiAdditive, dz, uint32_t(mul(w, 3) >> 1) << 16);
    const uint32_t r1 = uint32_t(mul(int32_t(rngStep() & 0x1f) + 0x140, w)) >> 8;
    ringRecord(actor.at, 0x4000, ((c | 0xfff40000u) << 8) | c, xy, dc::kSemiAdditive, dz, r1 << 16);
    const uint32_t r2 = uint32_t(mul(int32_t(rngStep() & 0x1f) + 0x200, w)) >> 8;
    ringRecord(actor.at, 0x4000, hi, xy, 0x42000000, dz, r2 << 16);
}
// exe 0x5bf380: the parameter word copied, timer 4, the 32 angles ((rng & 0x7f) + i * 0x80) / bytes ((rng & 0x7f) +
// 0x40, (rng & 0x3f) + 0x10), drawn, state 1
void impactStart(StaticActor& actor, ImpactFields& impact) {
    impact.paramsCopy = impact.paramsWord;
    impact.timer = 4;
    impact.unusedWord = 0;
    for (uint32_t i = 0; i < kImpactSpokes; ++i) {
        impact.angles[i] = uint16_t(((rngStep() & 0x7f) + (i << 7)) & 0xfff);
        impact.lengths[i] = uint8_t((rngStep() & 0x7f) + 0x40u);
        impact.widths[i] = uint8_t((rngStep() & 0x3f) + 0x10u);
    }
    drawImpact(actor, impact);
    ++actor.state;
}
// exe 0x5bf460: timer -1; at 0 freed (exe 0x5e1540), else drawn
void impactRun(StaticActor& actor, ImpactFields& impact) {
    impact.timer -= 1u;
    if (impact.timer == 0) {
        freeActor(actor);
        return;
    }
    drawImpact(actor, impact);
}
void beamImpact(StaticActor& actor) {
    ImpactFields& impact = classState<ImpactFields>(actor);
    switch (ImpactState(actor.state)) { // [0x8d0ddc]
    case ImpactState::Start: impactStart(actor, impact); return;
    case ImpactState::Run: impactRun(actor, impact); return;
    default: notPortedAt(kStoryPort, "0x5bf360 state past [0x8d0ddc]", actor.state);
    }
}

// ---- the shock rings and their spawner (pool 0xa0 type 2, exe 0x42e360) --------------------------------------------
enum class ShockState : uint8_t { Start = 0, Run = 1, Free = 2, SpawnerDirect = 3, RingDirect = 4, RingDirectB = 5 };
enum class ShockRole : uint8_t { Spawner = 0, Outward = 1, Inward = 2 };
constexpr uint8_t kEndless = 0xff;
// the GTE transform the ring's picture loads (the geometry scratch's transform at +0x40)
ViewTransform shockPictureTransform() { return geometryScratch().transform(0x40); }


// exe 0x42e380: a ring: its radius (outer << 4 when it shrinks, else -(width << 4)); the spawner: a count / period 0 ->
// 0xff, the toggle 0; state 1
void shockStart(EffectActor& actor, ShockRingFields& ring) {
    if (ShockRole(actor.subState) != ShockRole::Spawner) {
        if (ring.speed < 0) ring.radius = int16_t(uint32_t(ring.outerRadius) << 4);
        else ring.radius = int16_t(uint32_t(-int32_t(uint32_t(ring.width) << 4)));
        actor.state = uint8_t(ShockState::Run);
        return;
    }
    if (ring.count == 0) ring.count = kEndless;
    if (ring.period == 0) ring.period = kEndless;
    ring.toggle = 0;
    actor.state = uint8_t(ShockState::Run);
}
// exe 0x42e400: the spawner: its outer radius or its count 0 -> state 2; every period frames a ring child (exe 0x5e13a0)
void shockSpawner(EffectActor& actor, ShockRingFields& spawner) {
    if (spawner.outerRadius == 0 || spawner.count == 0) {
        actor.state = uint8_t(ShockState::Free);
        return;
    }
    const uint32_t d = spawner.period;
    if (d == 0) notPorted(kStoryPort, "0x42e400: period 0 (the exe divides by zero)");
    if (uint32_t(gameState().frame) % d != 0) return;
    const uint32_t slot = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (!slot) return;
    EffectActor& child = effectActorAt(slot);
    ShockRingFields& ring = classState<ShockRingFields>(child);
    child.flags = kActorLive;
    child.loopFlags = actor.loopFlags;
    child.type = kShockRingType;
    child.copyPositionWords(actor);
    ring.outerRadius = spawner.outerRadius;
    ring.paramsWord = spawner.paramsWord;
    ring.spawner = &actor;
    if (actor.variant != 0) {
        child.subState = actor.variant;
    } else {
        child.subState = uint8_t(spawner.toggle != 0 ? ShockRole::Outward : ShockRole::Inward);
        spawner.toggle = spawner.toggle == 0 ? 1u : 0u;
    }
    if (spawner.count != kEndless) spawner.count -= 1u;
}
// exe 0x42e4a0: a ring: its outer radius follows the live spawner (a dead one dropped); outer 0 or the radius out of
// [-(width << 4), outer << 4] -> state 2; else radius += speed and the picture (not drawn, see the header)
void shockRing(EffectActor& actor, ShockRingFields& ring) {
    if (EffectActor* spawner = ring.spawner.get()) {
        if (spawner->flags != 0) ring.outerRadius = classState<ShockRingFields>(*spawner).outerRadius;
        else ring.spawner = nullptr;
    }
    if (ring.outerRadius == 0) {
        actor.state = uint8_t(ShockState::Free);
        return;
    }
    const int16_t r = ring.radius;
    const int16_t f = t16(int32_t(ring.width) << 4), outer = t16(int32_t(ring.outerRadius) << 4);
    if (int32_t(r) < -int32_t(f) || r > outer) {
        actor.state = uint8_t(ShockState::Free);
        return;
    }
    ring.radius = int16_t(uint16_t(uint16_t(r) + uint16_t(int16_t(ring.speed))));
    // the identity into the GTE transform 0x6d0100 (rotation 0x1000 diagonal, translation 0)
    shockPictureTransform().setIdentityWords();
    // the rest of 0x42e4a0 builds the frame-buffer POLY_FT4 rings (GTE + OT packets only): not drawn
}
// exe 0x42e3e0: [0x88ca58 + role * 4]
void shockByRole(EffectActor& actor, ShockRingFields& ring) {
    switch (ShockRole(actor.subState)) {
    case ShockRole::Spawner: shockSpawner(actor, ring); return;
    case ShockRole::Outward:
    case ShockRole::Inward: shockRing(actor, ring); return;
    default: notPortedAt(kStoryPort, "0x42e3e0 variant past [0x88ca58]", actor.subState);
    }
}
void shockRingTick(EffectActor& actor) {
    ShockRingFields& ring = classState<ShockRingFields>(actor);
    switch (ShockState(actor.state)) { // [0x88ca4c]
    case ShockState::Start: shockStart(actor, ring); return;
    case ShockState::Run: shockByRole(actor, ring); return;
    case ShockState::Free: freeActor(actor); return; // 0x4358c0: exe 0x5e1580
    case ShockState::SpawnerDirect: shockSpawner(actor, ring); return;
    case ShockState::RingDirect:
    case ShockState::RingDirectB: shockRing(actor, ring); return;
    default: notPortedAt(kStoryPort, "0x42e360 state past [0x88ca4c]", actor.state);
    }
}

// ---- the explosion's electric spark (pool 0xe0 type 6, exe 0x418660) ------------------------------------------------
enum class SparkState : uint8_t { Start = 0, Run = 1 };


// the colour word of the spark's packets: 0x3a | blue (hi * T) | green / red (lo * T) (the exe's add / shift chain)
uint32_t sparkColour(int32_t lo, int32_t hi, int32_t t) {
    const uint32_t g = uint32_t(mul(t, lo)), b = uint32_t(mul(hi, t));
    return (((((b & 0x7f8u) + 0x1d000u) << 8) + (g & 0x7f8u)) << 5) + ((g >> 3) & 0xffu);
}
// exe 0x418680: angle rng & 0xfff, length ((rng & 0x3f) + 0xa0) * radius >> 8 + 1, timer 8, state 1
void sparkStart(ControllerActor& actor, ElectricSparkFields& spark) {
    spark.angle = uint16_t(rngStep() & 0xfff);
    const uint32_t r = rngStep();
    spark.length = uint16_t((uint32_t(mul(int32_t(*spark.radius), int32_t(r & 0x3f) + 0xa0)) >> 8) + 1u);
    spark.timer = 8;
    ++actor.state;
}
// the zigzag of exe 0x4186f0 (W = the projected length > 0)
void drawSpark(const ControllerActor& actor, const ElectricSparkFields& spark, const int16_t p[3], int W) {
    const uint32_t r1 = rngStep();
    rngStep(); // the second draw is not used
    int32_t dist = W >> 2;
    uint32_t ang = (((r1 & 0xff) + spark.angle) - 0x80u) & 0xfff;
    int16_t sxy[2];
    projectOnScreen(p, sxy); // exe 0x5f2dd0 (its result is not tested)
    auto at = [&](uint32_t an, int plus, int16_t out[2]) {
        an &= 0xfff;
        out[0] = t16(div4096(mul(fixedCos(int(an)), dist)) + int32_t(sxy[0]) + plus);
        out[1] = t16(div4096(mul(fixedSin(int(an)), dist)) + int32_t(sxy[1]) + plus);
    };
    int16_t pt[4][2];
    at(ang, 0, pt[0]);
    for (int k = 1; k < 4; ++k) {
        pt[k][0] = pt[0][0];
        pt[k][1] = pt[0][1];
    }
    const int L = spark.length;
    // three POLY_G4 per step: {pt0, pt1, n0, n1} edge (dark / c1), {pt1, pt2, n1, n2} core (c2), {pt2, pt3, n2, n3}
    // edge (c1 / dark); OT [0xa67840] + 0x78 behind the DR_MODE 0xe1000220 (abr 1)
    auto quad = [&](const uint32_t c[4], const int16_t q0[2], const int16_t q1[2], const int16_t q2[2],
                    const int16_t q3[2]) {
        const int16_t* q[4] = {q0, q1, q2, q3};
        int px[4][2];
        for (int k = 0; k < 4; ++k) {
            px[k][0] = int(q[k][0]) - int(sxy[0]);
            px[k][1] = int(q[k][1]) - int(sxy[1]);
        }
        screenG4(actor.at, p, L, W, c, 1, px);
    };
    while (W > dist) {
        const int32_t h = int32_t(stage().depthRange);
        if (h == 0) notPorted(kStoryPort, "0x4186f0: the depth range is 0 (the exe divides by zero)");
        const int32_t q = mul(mul(h - int32_t(spark.depth), 3), 0x20) / h;
        const int32_t t = int16_t(spark.timer);
        const uint32_t c1 = sparkColour(q, q + 0x20, t), c2 = sparkColour(q + 0x40, q + 0x60, t);
        const uint32_t s1 = rngStep(), s2 = rngStep(), s3 = rngStep();
        dist = dist + int32_t(uint32_t(mul(int32_t(s1 & 0x7f) + 0x40, W)) >> 11) + 1;
        ang = ang + (s2 & 0x7f) - 0x40u;
        const uint32_t k = s3 & 3;
        int16_t n[4][2];
        at(ang - k * 4 - 0x40, 0, n[0]);
        at(ang - k, 0, n[1]);
        at(k + ang, 1, n[2]);
        at(ang + k * 4 + 0x40, 1, n[3]);
        const uint32_t ca[4] = {0x3a000000u, c1, 0x3a000000u, c1}, cb[4] = {c2, c2, c2, c2}, cc[4] = {c1, 0, c1, 0};
        quad(ca, pt[0], pt[1], n[0], n[1]);
        quad(cb, pt[1], pt[2], n[1], n[2]);
        quad(cc, pt[2], pt[3], n[2], n[3]);
        for (int j = 0; j < 4; ++j) {
            pt[j][0] = n[j][0];
            pt[j][1] = n[j][1];
        }
    }
}
// exe 0x4186f0: radius 0 -> freed; the length re-rolled above the radius; drawn when exe 0x5f2f20 > 0; timer -1, at 0
// freed (exe 0x5e15c0)
void sparkRun(ControllerActor& actor, ElectricSparkFields& spark) {
    const uint32_t R = *spark.radius;
    if (R == 0) {
        freeActor(actor);
        return;
    }
    if (uint32_t(spark.length) > R)
        spark.length = uint16_t((uint32_t(mul(int32_t(rngStep() & 0x3f) + 0xa0, int32_t(R))) >> 8) + 1u);
    const int16_t p[3] = {spark.point.x, spark.point.y, spark.point.z};
    int16_t depth = spark.depth; // exe 0x5f2f20 writes the depth only when the point projects
    const int W = projectedWidth(p, int16_t(spark.length), &depth);
    spark.depth = depth;
    if (W > 0) drawSpark(actor, spark, p, W);
    spark.timer -= 1u;
    if (spark.timer == 0) freeActor(actor);
}
void electricSpark(ControllerActor& actor) {
    ElectricSparkFields& spark = classState<ElectricSparkFields>(actor);
    switch (SparkState(actor.state)) { // [0x88a104]
    case SparkState::Start: sparkStart(actor, spark); return;
    case SparkState::Run: sparkRun(actor, spark); return;
    default: notPortedAt(kStoryPort, "0x418660 state past [0x88a104]", actor.state);
    }
}

} // namespace

void beamImpactUpdate(const PlayerEnv&, uint32_t actor) { beamImpact(staticActorAt(actor)); }

void screenShockRingUpdate(const PlayerEnv&, uint32_t actor) { shockRingTick(effectActorAt(actor)); }

void electricSparkUpdate(const PlayerEnv&, uint32_t actor) { electricSpark(controllerActorAt(actor)); }

} // namespace od::port
