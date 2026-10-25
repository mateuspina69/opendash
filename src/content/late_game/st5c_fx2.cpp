// ST5C: the remaining attack classes of the boss 0x50b9c0 (content/late_game/st5c; the others are
// content/late_game/st5c_fx). Names are hypotheses from the observed behaviour.
//
// The beam (pool 0x40 type 0x2b, exe 0x5cf370; its block from +0x64: a timer, a hit box, the boss, the 24 segments;
// table [0x8d17bc], entries 3..5 are not code and 6..11 belong to the classes 0x5cfb30 / 0x5d0050): attack 7's beam.
// Variant 0 = the head, variants 1..24 = its segments (also type 0x2b: model variant 0, blend (1, 1), scale 0x400,
// colour 0x2108, then idle). The head: 0 0x5cf3a0 (the 24 segments (roll i * 0x1000 / 24), at the boss's bone 7 (0,
// 0x80, 0) (0x5cfae0), pitch at the player's x / z on the floor - 0x7e0, the boss's yaw, the loop sound 0x2cd (0x5d5480)
// 0x13800 ahead of the player along it); 1 0x5cf540 (the pitch sweeps down one step a tick; once it is more than 0x40
// off the player's (the player on the ground) or the boss's report byte is 0: the boss's report = 3, timer 8, state 2;
// each tick the segments are laid along the rotation (-0xd00 steps, the first one shifted by the frame & 3 quarter)
// until the room blocks the step (exe 0x40f5a0): the rest hidden, a beam impact (0x5bf360) where it hit; a hit capsule
// (radius 0x10) per shown segment; the loop sound moved to the last point (0x5d55c0)); 2 0x5cf8a0 (the same layout
// without hits / sound, the segments shrinking (scale timer << 7); at 0 -> 0x5cfa90: the boss's report = 4, the
// segments and the head freed).
//
// The ball visuals (static type 0x5e, exe 0x5bd320; its block from +0x28: a timer, a word, 24 ray angles, timers,
// lengths and s8 spins, the ball): attack 10's ball visuals (three per ball, variants 0..2). States ([0x8d0db0]): 0
// 0x5bd340 (the rays randomised, drawn, state 1), 1 0x5bd550 (freed once the ball's flags are 0, else drawn). Variants
// 0 / 1 draw 0x5bd590 (per ray two screen rings at the projected ball, turning by spin >> 2; variant 0 two more at the
// centre), variant 2 0x5bd800 (24 OT POLY_G3 rays turning by spin * 2, subtractive: dc::otTriG3; the timers / angles run
// as in the exe, only when the ball projects).
//
// The ball's explosion (pool 0x40 type 2, exe 0x5c3690; the boss, its shock-ring spawner, its radius and timer): states
// ([0x8d0fb4]): 0 0x5c36b0 (radius 0x80, drawn, a shock-ring spawner (0x42e360) carrying the radius >> 4, box
// 0x8d0f88), 1 0x5c3740 (its phase: 0 grow by 0x80 up to 0x300 (the boss dead -> 2), 1 hold 0xc0 ticks (then 0x40 more)
// / the boss dead, 2 shrink by 0x10, at 0 the spawner's radius 0 and freed; every tick drawn (0x5c38e0: six rings of
// colours lerped between [0x8d0fbc] by the frame), the spawner's radius, by [0x8d0f94][frame & 0x1f] an electric spark
// (0x418660) pointing at the radius, and the player pulled within radius * 4 (exe 0x5e3b00)).
//
// The rock effects (pool 0xa0 type 0x24, exe 0x43ff80) variants 3 / 4 (spawned by the rocks 0x50ee90,
// content/late_game/st5c_fx): 3 0x440d40 = the warning ring under a falling rock: 0 fade in 7 ticks, 1 hold until 0x50
// ticks or the rock's report byte is 0, 2 fade out 7 ticks then freed; up to 16 sparkles (bit 7 live, low nibble age)
// rising in a circle as OT POLY_FT4 (drawn through dc::otQuadGT4 with one colour, as game/global_fx), and a ring of 16
// OT POLY_G4 (outer black raised, inner orange: drawn) with a POLY_G3 fan to the centre (dc::otTriG3).
// 4 0x4416a0 = the landing dust: 8 x 4 billboard records (draw commands) on a ring of its radius growing by its spread,
// rising by its rise speed (slowing by its brake), 16 ticks.
//
// The exe's GTE scratch these draws write (the projection scratch 0x6d00c0..) is written as the exe does.
#include "content/late_game/st5c_fx2.hpp"
#include "content/late_game/st5c_fx2_state.hpp"

#include "content/late_game/st5c_fx3.hpp"

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/draw_commands.hpp"
#include "game/field_alias.hpp"
#include "game/game_state.hpp"
#include "game/geometry_scratch.hpp"
#include "game/hit_list.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/projection_state.hpp"
#include "game/random.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st5c_fx2_state;

namespace {

int16_t t16(int64_t v) { return int16_t(uint16_t(uint64_t(v))); }
int32_t mul(int32_t a, int32_t b) { return int32_t(uint32_t(a) * uint32_t(b)); } // imul (32-bit wrap)
int16_t wholeOf(int32_t v) { return int16_t(uint32_t(v) >> 16); }                 // a 16.16 value's integer part
// a 6-word ring / screen-ring draw record at the draw cursor
void ringRecord(uint32_t owner, uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3, uint32_t w4, uint32_t w5) {
    dc::emit({w0, w1, w2, w3, w4, w5}, owner);
}
// the units of one screen pixel at a projected point's depth (view z / the projection distance)
float pixelUnits(const CameraPoint& c) {
    const int32_t h = projection().focal;
    return h != 0 ? float(c.view[2]) / float(h) : 0.0f;
}
// the depth 0x40c200 leaves in 0xa59d34 for one RTPS (as game/projection projectQuad): z, 0 when behind
int32_t rtpsDepth(const CameraPoint& c) {
    return (c.view[2] >= projection().nearDepth || c.view[2] > 0) ? c.view[2] : 0;
}

// ---- the beam (pool 0x40 type 0x2b, exe 0x5cf370) ------------------------------------------------------------------
enum class BeamState : uint8_t { Start = 0, Sweep = 1, Fade = 2 };
constexpr uint16_t kBeamLoopSound = 0x2cd;
constexpr uint8_t kBossReportBlocked = 3, kBossReportDone = 4; // the boss's report byte (its step counter)
// group 3, a capsule sampled every step
constexpr uint32_t kBeamEntry = hitGroup(3) | hitCapsule(1);


// exe 0x5cfae0: at the boss's bone 7 at the local (0, 0x80, 0) (exe 0x5e1970)
void beamAtBone(ProjectileActor& actor, const BeamFields& beam) {
    const int16_t v[3] = {0, 0x80, 0};
    int16_t p[3];
    bonePointOffset(*beam.boss, 7, v, p);
    actor.pos.x = int32_t(uint32_t(int32_t(p[0])) << 16);
    actor.pos.y = int32_t(uint32_t(int32_t(p[1])) << 16);
    actor.pos.z = int32_t(uint32_t(int32_t(p[2])) << 16);
}
// the blocked tail of 0x5cf540 / 0x5cf8a0: the segments i.. hidden (a null slot would crash the exe)
void hideFrom(BeamFields& beam, uint32_t i) {
    for (; i < kBeamSegments; ++i) {
        ProjectileActor* s = beam.segments[i].get();
        if (!s) notPorted(kStoryPort, "0x5cf370: a hidden beam segment slot is null (the exe faults)");
        s->setDrawn(false);
    }
}
// exe 0x5cf3a0: the head: 24 segments, at bone 7, pitch at the player's floor point - 0x7e0, the boss's yaw, box
// 0x8d17b0, the loop sound 0x2cd ahead of the player; a segment: model variant 0, blend (1, 1), scale 0x400, colour
// 0x2108 (a failed bind retries next tick); returns 2 for the segments, 0 for the head
int beamStart(ProjectileActor& actor, BeamFields& beam) {
    if (actor.variant != 0) {
        if (bindActorModel(actor, 0)) {                            // exe 0x5e0ba0
            actor.semiTransparency = semiTransparencyBits(1, 1);   // exe 0x5e1a70(actor, 1, 1)
            actor.scale.x = 0x400;
            actor.scale.y = 0x400;
            actor.scale.z = 0x400;
            actor.tint = 0x2108;
            ++actor.state;
        }
        return 2;
    }
    for (uint32_t i = 0; i < kBeamSegments; ++i) {
        const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
        if (slot) {
            ProjectileActor& s = projectileActorAt(slot);
            s.flags = kActorLive;
            s.variant = uint8_t(i + 1);
            s.type = kBeamType;
            s.loopFlags |= kActorSweepGroup2;
            s.subtype = 0;
            s.rotation.z = int16_t(int32_t(i << 12) / 0x18);
        }
        pointAtWord(beam.segments[i], slot);
    }
    pointAtWord(beam.boss, beam.spawnBoss);
    beamAtBone(actor, beam);
    const Player& p = player();
    const int32_t from[3] = {actor.pos.x, actor.pos.y, actor.pos.z};
    const int32_t floorPoint[3] = {p.pos.x, 0, p.pos.z}; // the player's x / z on the floor (the exe's local)
    actor.rotation.x = int16_t((uint32_t(pitchOf(from, floorPoint)) - 0x7e0u) & 0xfff); // exe 0x5e3ca0
    actor.rotation.y = beam.boss->rotation.y;
    actor.rotation.z = 0;
    actor.collisionBox.pointAt(TableId::St5cFx2BeamBox);
    actor.actorHit = 0;
    beam.hitBox.result = 0;
    int32_t d[3];
    rotateVector(actor.rotation, 0, 0, -0x13800, d); // exe 0x5e3970
    const int32_t ahead[3] = {int32_t(uint32_t(p.pos.x) + uint32_t(d[0])), int32_t(uint32_t(p.pos.y) + uint32_t(d[1])),
                              int32_t(uint32_t(p.pos.z) + uint32_t(d[2]))};
    beam.loopHandle = uint8_t(soundStartLoopAtXyz(kBeamLoopSound, wholeOf(ahead[0]), wholeOf(ahead[1]),
                                                  wholeOf(ahead[2]), 0)); // exe 0x5d5480
    ++actor.state;
    return 0;
}
// the step along the head's rotation (-0xd00 << 12) and the first point: the head's point + step * (frame & 3) / 4
void beamStep(const ProjectileActor& actor, int32_t step[3], int32_t cur[3]) {
    rotateVector(actor.rotation, 0, 0, -0xd00, step); // exe 0x5e3970
    const int32_t f = int32_t(gameState().frame & 3);
    const int32_t pos[3] = {actor.pos.x, actor.pos.y, actor.pos.z};
    for (uint32_t k = 0; k < 3; ++k) cur[k] = int32_t(uint32_t(pos[k]) + uint32_t(mul(step[k], f) >> 2));
}
// a segment laid at the new point along the head's rotation
void laySegment(const ProjectileActor& actor, ProjectileActor& s, const int32_t nw[3]) {
    s.setDrawn(true);
    s.pos.x = nw[0];
    s.pos.y = nw[1];
    s.pos.z = nw[2];
    s.rotation.x = actor.rotation.x;
    s.rotation.z = int16_t(uint16_t(s.rotation.z) + 0x50u);
    s.rotation.y = actor.rotation.y;
}
// exe 0x5cf540 (the head; the segments return 2): the pitch sweeps down; off the player by more than 0x40 (the player
// on the ground) or the boss's report cleared -> the boss's report = 3 (when set), timer 8, state 2. The segments laid
// along the rotation until the room blocks a step (the rest hidden, a beam impact there); a hit capsule per shown
// segment; the loop sound at the last point
int beamSweep(const PlayerEnv& env, ProjectileActor& actor, BeamFields& beam) {
    if (actor.variant != 0) return 2;
    beamAtBone(actor, beam);
    const Player& p = player();
    int32_t nw[3] = {p.pos.x, 0, p.pos.z}; // the player's floor point, then the last laid point (the exe's local)
    int32_t cur[3] = {actor.pos.x, actor.pos.y, actor.pos.z};
    actor.rotation.x = int16_t(uint16_t(actor.rotation.x) - 1u);
    const int pitch = uint16_t(actor.rotation.x) & 0xfff;
    const int target = (pitchOf(cur, nw) - 0x800) & 0xfff; // exe 0x5e3ca0
    int d = angleDiff(uint32_t(pitch), uint32_t(target));  // exe 0x5e3d80
    if (d < 0) d = -d;
    AnimatedActor& boss = *beam.boss;
    if ((d > 0x40 && p.airborne == 0) || boss.stepCounter == 0) {
        if (boss.stepCounter != 0) boss.stepCounter = kBossReportBlocked;
        beam.timer = 8;
        ++actor.state;
    }
    actor.rotation.y = beam.boss->rotation.y;
    int32_t step[3];
    beamStep(actor, step, cur);
    const uint32_t mask = collisionLayerMask(actor.slot); // exe 0x413240
    for (uint32_t i = 0; i < kBeamSegments; ++i) {
        ProjectileActor* s = beam.segments[i].get();
        if (!s) continue;
        for (uint32_t k = 0; k < 3; ++k) nw[k] = int32_t(uint32_t(cur[k]) + uint32_t(step[k]));
        const int16_t i0[3] = {wholeOf(nw[0]), wholeOf(nw[1]), wholeOf(nw[2])};
        const int16_t i1[3] = {wholeOf(cur[0]), wholeOf(cur[1]), wholeOf(cur[2])};
        if (int32_t(testMoveBlocked(env, i0, i1, actor.collisionBox, mask)) < 0) { // exe 0x40f5a0
            hideFrom(beam, i);
            if (const uint32_t slot = allocActor60(kActorStageClass)) { // exe 0x5e12f0
                StaticActor& impact = staticActorAt(slot);
                impact.flags = kActorLive;
                impact.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
                impact.type = kBeamImpactType;
                impact.subtype = 0;
                impact.variant = 0;
                impact.pos.x = nw[0];
                impact.pos.y = 0;
                impact.pos.z = nw[2];
            }
            break;
        }
        laySegment(actor, *s, nw);
        // the capsule from the previous point + a quarter step to the new point (integer parts, 16-bit sums)
        const int16_t pa[3] = {t16(int(wholeOf(cur[0])) + (step[0] >> 18)), t16(int(wholeOf(cur[1])) + (step[1] >> 18)),
                               t16(int(wholeOf(cur[2])) + (step[2] >> 18))};
        const int16_t pb[3] = {wholeOf(nw[0]), wholeOf(nw[1]), wholeOf(nw[2])};
        const AnimatedActor& owner = *beam.boss;
        if (!hasAnimatedStats(owner.type))
            notPortedAt(kStoryPort, "st5c_fx2: null type row (the exe faults), type", owner.type);
        hitListSubmitCapsule(pa, 0x10, pb, 0x10, beam.hitBox, hitOwner(actor.slot) | kBeamEntry,
                             uint32_t(animatedStats(owner.type).strongAttack) | kHitAttackStrong); // exe 0x5e4190
        for (uint32_t k = 0; k < 3; ++k) cur[k] = nw[k];
    }
    soundUpdateLoopAtXyz(uint32_t(int32_t(int8_t(beam.loopHandle))), wholeOf(nw[0]), wholeOf(nw[1]), wholeOf(nw[2]),
                         0); // exe 0x5d55c0
    return 0;
}
// exe 0x5cf8a0: the timer; at 0 -> 1 (0x5cfa90); else the same layout without hits / sound, the segments scaled by
// timer << 7 (x / y)
int beamFade(const PlayerEnv& env, ProjectileActor& actor, BeamFields& beam) {
    beam.timer -= 1u;
    if (beam.timer == 0) return 1;
    int32_t cur[3], nw[3];
    int32_t step[3];
    beamStep(actor, step, cur);
    const uint32_t mask = collisionLayerMask(actor.slot); // exe 0x413240
    for (uint32_t i = 0; i < kBeamSegments; ++i) {
        ProjectileActor* s = beam.segments[i].get();
        if (!s) continue;
        for (uint32_t k = 0; k < 3; ++k) nw[k] = int32_t(uint32_t(cur[k]) + uint32_t(step[k]));
        const int16_t i0[3] = {wholeOf(nw[0]), wholeOf(nw[1]), wholeOf(nw[2])};
        const int16_t i1[3] = {wholeOf(cur[0]), wholeOf(cur[1]), wholeOf(cur[2])};
        if (int32_t(testMoveBlocked(env, i0, i1, actor.collisionBox, mask)) < 0) { // exe 0x40f5a0
            hideFrom(beam, i);
            return 0;
        }
        laySegment(actor, *s, nw);
        s->scale.x = int16_t(uint32_t(beam.timer) << 7);
        s->scale.y = int16_t(uint32_t(beam.timer) << 7);
        for (uint32_t k = 0; k < 3; ++k) cur[k] = nw[k];
    }
    return 0;
}
// exe 0x5cfa90: the boss's report = 4 (when set), the segments and the head freed (exe 0x5e1500)
void beamEnd(ProjectileActor& actor, BeamFields& beam) {
    AnimatedActor& boss = *beam.boss;
    if (boss.stepCounter != 0) boss.stepCounter = kBossReportDone;
    for (uint32_t i = 0; i < kBeamSegments; ++i)
        if (ProjectileActor* s = beam.segments[i].get()) freeActor(*s);
    freeActor(actor);
}
void beamTick(const PlayerEnv& env, ProjectileActor& actor) {
    BeamFields& beam = classState<BeamFields>(actor);
    int r = 0;
    switch (BeamState(actor.state)) { // [0x8d17bc]
    case BeamState::Start: r = beamStart(actor, beam); break;
    case BeamState::Sweep: r = beamSweep(env, actor, beam); break;
    case BeamState::Fade: r = beamFade(env, actor, beam); break;
    default:
        notPortedAt(kStoryPort, "0x5cf370 state (3..5 are not code; 6..11 belong to 0x5cfb30 / 0x5d0050; [0x8d17bc])",
                    actor.state);
    }
    if (r == 1) beamEnd(actor, beam);
}

// ---- the ball visuals (static type 0x5e, exe 0x5bd320) -------------------------------------------------------------
enum class BallFxState : uint8_t { Start = 0, Run = 1 };


// exe 0x5bda40: the ball's s16 position
void ballPoint(const BallFxFields& fx, int16_t p[3]) {
    const ProjectileActor& ball = *fx.ball;
    p[0] = ball.pos.x.whole;
    p[1] = ball.pos.y.whole;
    p[2] = ball.pos.z.whole;
}
// the ball projected (exe 0x5f2dd0) and its width 0x400 (exe 0x5f2f20); false = the exe returns
bool ballProject(const BallFxFields& fx, int16_t sxy[2], int& w, int16_t& depth) {
    int16_t p[3];
    ballPoint(fx, p);
    if (projectOnScreen(p, sxy) < 0) return false;
    depth = 0;
    w = projectedWidth(p, 0x400, &depth);
    return w != 0;
}
// a ray's timer counts down, refilled (rng & 0xf) + 0x10
void rayTimer(BallFxFields& fx, uint32_t i) {
    fx.timers[i] -= 1u;
    if (uint8_t(fx.timers[i]) == 0) fx.timers[i] = int8_t((rngStep() & 0xf) + 0x10u);
}
// exe 0x5bd590 (variants 0 / 1): per ray (angle += spin >> 2) two screen rings at the screen point along it (length *
// timer * w >> 13, grey (0x20 - timer) << 2 / << 1); variant 0: two more at the centre
void drawRings(const StaticActor& actor, BallFxFields& fx) {
    int16_t sxy[2], depth = 0;
    int w = 0;
    if (!ballProject(fx, sxy, w, depth)) return;
    const int16_t dv = t16(int(depth) - 8);
    const uint32_t dz = dv > 0 ? uint16_t(dv) : 1u;
    for (uint32_t i = 0; i < kBallRays; ++i) {
        rayTimer(fx, i);
        const int32_t t = fx.timers[i];
        const int32_t len = mul(mul(int32_t(fx.lengths[i]), t), w) >> 13;
        const uint32_t ang = (uint32_t(int32_t(int8_t(fx.spins[i] >> 2))) + fx.angles[i]) & 0xfff;
        fx.angles[i] = uint16_t(ang);
        const uint16_t x = uint16_t((mul(fixedCos(int(ang)), len) >> 12) + sxy[0]);
        const uint16_t y = uint16_t((mul(fixedSin(int(ang)), len) >> 12) + sxy[1]);
        const uint32_t xy = uint32_t(x) | uint32_t(y) << 16;
        const int32_t sv = mul(mul(0x20 - t, fx.spins[i]), w) >> 0x11;
        const uint32_t e1 = uint32_t(0x20 - t) << 2, e2 = e1 >> 1;
        ringRecord(actor.at, 0x4000, ((e1 | 0xfff40000u) << 8) | e1, xy, dc::kSemiAdditive, dz | 0x30000u,
                   uint32_t(sv) << 16);
        ringRecord(actor.at, 0x4000, ((e2 | 0xfff40000u) << 8) | e2, xy, dc::kSemiAdditive, dz | 0x30000u,
                   uint32_t(sv >> 1) << 16);
    }
    if (actor.variant != 0) return;
    const uint32_t sw = uint32_t(uint16_t(sxy[0])) | uint32_t(uint16_t(sxy[1])) << 16, q = uint32_t(w >> 4);
    ringRecord(actor.at, 0x4000, 0xf4ffff00, sw, 0x42ffffff, dz | 0x10000u, q << 16);
    ringRecord(actor.at, 0x4000, 0xf4ffffff, sw, 0x42000000, dz | 0x10000u, (q << 17) | q);
}
// exe 0x5bd800 (variant 2): DR_MODE 0xe1000240 (abr 2, subtractive) + 24 OT POLY_G3 rays from the projected ball (one
// chain at OT[depth - 8, at least 1]): per ray the timer t (refilled as 0x5bd590), the angle += spin * 2 (16-bit); a
// G3 0x32 {grey (0x20 - t) * 4, ball S} {black, S + (cos, sin)(angle - h) * len >> 12} {black, S + (cos, sin)(angle +
// h) * len >> 12}, h = (0xa0 - length) * 3 >> 1, len = t * length * w >> 13 (w = the projected width of 0x400, <<
// shift). Port: dc::otTriG3 with the ball as the camera-input point and the pixel offsets brought back to its depth
// (0x400 << shift / w camera-input units per pixel).
void drawRays(const StaticActor& actor, BallFxFields& fx) {
    int16_t sxy[2], depth = 0;
    int w = 0;
    if (!ballProject(fx, sxy, w, depth)) return;
    const int sh = stage().coordinateShift & 0x1f;
    int16_t bp[3];
    ballPoint(fx, bp);
    const int16_t pt[3] = {t16(int(bp[0]) << sh), t16(int(bp[1]) << sh), t16(int(bp[2]) << sh)};
    const float scale = float(0x400 << sh) / float(w);
    for (uint32_t i = 0; i < kBallRays; ++i) {
        rayTimer(fx, i);
        fx.angles[i] = uint16_t(fx.angles[i] + uint16_t(uint32_t(int32_t(fx.spins[i])) << 1));
        const int32_t t = fx.timers[i];
        const uint32_t grey = uint32_t(((0x20 - t) << 3) >> 1) & 0xff;
        const uint32_t len = fx.lengths[i];
        const int32_t r = mul(mul(t, int32_t(len)), w) >> 13;
        const int32_t h = (int32_t(0xa0 - len) * 3) >> 1;
        const uint32_t ang = fx.angles[i] & 0xfff, a0 = (ang - uint32_t(h)) & 0xfff, a1 = (ang + uint32_t(h)) & 0xfff;
        const uint32_t c[3] = {0x32000000u | grey << 16 | grey << 8 | grey, 0, 0};
        const int16_t v[3][3] = {{pt[0], pt[1], pt[2]}, {pt[0], pt[1], pt[2]}, {pt[0], pt[1], pt[2]}};
        const float off[3][2] = {{0.0f, 0.0f},
                                 {float(t16(mul(fixedCos(int(a0)), r) >> 12)) * scale,
                                  float(t16(mul(fixedSin(int(a0)), r) >> 12)) * scale},
                                 {float(t16(mul(fixedCos(int(a1)), r) >> 12)) * scale,
                                  float(t16(mul(fixedSin(int(a1)), r) >> 12)) * scale}};
        dc::otTriG3(actor.at, c, (0x240 >> 5) & 3, v, off);
    }
}
void drawBall(const StaticActor& actor, BallFxFields& fx) {
    if (actor.variant < 2) drawRings(actor, fx);
    else drawRays(actor, fx);
}
// exe 0x5bd340: the ball, timer 0x1f, the 24 rays (angle (rng & 0x7f) + i * 0x80, timer (rng & 0xf) + 0x10, length
// (rng & 0x7f) + 0x20, spin variant < 2: (rng & 0x3f) + 0x20, else (rng & 0x7f) - 0x40), drawn, state 1
void ballFxStart(StaticActor& actor, BallFxFields& fx) {
    pointAtWord(fx.ball, fx.spawnBall);
    fx.timer = 0x1f;
    fx.word = 1;
    const bool rings = actor.variant < 2;
    for (uint32_t i = 0; i < kBallRays; ++i) {
        fx.angles[i] = uint16_t(((rngStep() & 0x7f) + (i << 7)) & 0xfff);
        fx.timers[i] = int8_t((rngStep() & 0xf) + 0x10u);
        fx.lengths[i] = uint8_t((rngStep() & 0x7f) + 0x20u);
        fx.spins[i] = int8_t(rings ? (rngStep() & 0x3f) + 0x20u : (rngStep() & 0x7f) - 0x40u);
    }
    drawBall(actor, fx);
    ++actor.state;
}
// exe 0x5bd550: the ball gone (its flags are 0) -> freed (exe 0x5e1540), else drawn
void ballFxRun(StaticActor& actor, BallFxFields& fx) {
    if (fx.ball->flags == 0) {
        freeActor(actor);
        return;
    }
    drawBall(actor, fx);
}
void ballFx(StaticActor& actor) {
    BallFxFields& fx = classState<BallFxFields>(actor);
    switch (BallFxState(actor.state)) { // [0x8d0db0]
    case BallFxState::Start: ballFxStart(actor, fx); return;
    case BallFxState::Run: ballFxRun(actor, fx); return;
    default: notPortedAt(kStoryPort, "0x5bd320 state past [0x8d0db0]", actor.state);
    }
}

// ---- the ball's explosion (pool 0x40 type 2, exe 0x5c3690) ---------------------------------------------------------
enum class BlastState : uint8_t { Start = 0, Run = 1 };
enum class BlastPhase : uint8_t { Grow = 0, Hold = 1, Shrink = 2 };
constexpr uint32_t kShockRingParams = 0xcf00c00; // count 0, period 0xc, speed -0x10, width 0xc

// the two colours the rings lerp between (exe data: r / g / b dwords each) and the frames that spawn a spark
const DataTable<ScalarTable<uint32_t, 6>> blastColours{TableId::St5cFx2BlastColours};
const DataTable<ScalarTable<uint8_t, 0x20>> sparkFrames{TableId::St5cFx2SparkFrames};

// exe 0x5c3a30(k, scale): the colour k of the two-entry table lerped toward the previous one by the frame (& 0xf) * 2 /
// 32, times scale: b << 12 | g << 4 | r (unsigned arithmetic as the exe)
uint32_t blastColour(uint32_t k, uint32_t scale) {
    const uint32_t f = gameState().frame, d = (f & 0xf) << 1, idx = (f >> 4) + k;
    const uint32_t c0 = (idx & 1) * 3, c1 = ((idx - 1) & 1) * 3;
    auto lerp = [&](uint32_t o) { return (((blastColours[c1 + o] - blastColours[c0 + o]) * d) >> 5) + blastColours[c0 + o]; };
    const uint32_t b = (lerp(2) * scale) & 0xff0, g = (lerp(1) * scale) & 0xff0, r = ((lerp(0) * scale) >> 4) & 0xff;
    return ((b << 8 | g) << 4) | r;
}
// exe 0x5c39d0(actor, inner, outer, w1, w3): a ring record at the actor
void blastRing(const ProjectileActor& actor, uint32_t inner, uint32_t outer, uint32_t w1, uint32_t w3) {
    ringRecord(actor.at, 0x4000, w1,
               uint32_t(uint16_t(actor.pos.x.whole)) | uint32_t(int32_t(actor.pos.y.whole)) << 16, w3,
               uint32_t(uint16_t(actor.pos.z.whole)) | 0x1010000u, (outer << 16) | inner);
}
uint32_t div7s7(uint32_t x) { return (x / 7) >> 7; } // the exe's magic division by 7, then >> 7
// exe 0x5c38e0(actor, r): six rings: a disc to (r << 7) / 7 >> 7, four bands growing by r << 7, a band to r + 0x40
void blastDraw(const ProjectileActor& actor, uint32_t r) {
    const uint32_t step = r << 7;
    uint32_t in = div7s7(step);
    blastRing(actor, 0, in, 0xf0ffffff, blastColour(0, 0x20) | 0x42000000u);
    uint32_t acc = r << 8;
    for (uint32_t i = 0; i < 4; ++i, acc += step) {
        const uint32_t prev = in;
        in = div7s7(acc);
        const uint32_t ca = blastColour(i + 1, 0x1c - i * 4) | 0x42000000u;
        const uint32_t cb = blastColour(i, 0x20 - i * 4) | dc::kRingOpcode;
        blastRing(actor, prev, in, cb, ca);
    }
    blastRing(actor, in, r + 0x40, blastColour(4, 0x10) | dc::kRingOpcode, 0x42000000u);
}
// the shock-ring spawner (null would crash the exe)
ShockRingParams shockRings(const BlastFields& blast) {
    if (!blast.shockRings) notPorted(kStoryPort, "0x5c3690: no pool-0xa0 effect (the exe faults)");
    return ShockRingParams(*blast.shockRings);
}
// exe 0x5c36b0: radius 0x80, drawn, the shock-ring spawner (outer radius = radius >> 4), box 0x8d0f88, the boss,
// state 1
void blastStart(ProjectileActor& actor, BlastFields& blast) {
    blast.radius = 0x80;
    blastDraw(actor, 0x80);
    const uint32_t slot = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (slot) {
        EffectActor& rings = effectActorAt(slot);
        rings.flags = kActorLiveDrawn;
        rings.loopFlags |= kActorSweepGroup1;
        rings.setTypePair(kShockRingType);
        rings.setVariantPair(0);
        rings.setParamsWord(kShockRingParams);
        ShockRingParams(effectActorAt(slot)).outerRadius = uint8_t(uint32_t(blast.radius) >> 4);
        rings.pos.x = actor.pos.x.whole;
        rings.pos.y = actor.pos.y.whole;
        rings.pos.z = actor.pos.z.whole;
    }
    actor.collisionBox.pointAt(TableId::St5cFx2BlastBox);
    pointAtWord(blast.boss, blast.spawnBoss);
    pointAtWord(blast.shockRings, slot);
    actor.subState = uint8_t(BlastPhase::Grow);
    ++actor.state;
}
// exe 0x5c3740: grow / hold / shrink, then drawn, the spawner's radius, an electric spark on the frames of the table,
// and the player pulled within radius * 4
void blastRun(ProjectileActor& actor, BlastFields& blast) {
    const bool bossDead = blast.boss->hitPoints < 0;
    switch (BlastPhase(actor.subState)) {
    case BlastPhase::Grow:
        blast.radius += 0x80u;
        if (blast.radius > 0x300) {
            blast.timer = 0xc0;
            ++actor.subState;
        } else if (bossDead) {
            actor.subState += 2u;
        }
        break;
    case BlastPhase::Hold:
        blast.timer -= 1u;
        if (blast.timer == 0) {
            blast.timer = 0x40;
            ++actor.subState;
        } else if (blast.boss->hitPoints < 0) {
            ++actor.subState;
        }
        break;
    case BlastPhase::Shrink:
        blast.radius += 0xfff0u;
        if (blast.radius == 0) {
            shockRings(blast).outerRadius = 0;
            freeActor(actor); // exe 0x5e1500
            return;
        }
        break;
    default: break;
    }
    const uint32_t r = blast.radius;
    blastDraw(actor, r);
    shockRings(blast).outerRadius = uint8_t(r >> 4);
    if (sparkFrames[gameState().frame & 0x1fu] != 0) {
        if (const uint32_t slot = allocActorE0()) { // exe 0x5e1440
            ControllerActor& spark = controllerActorAt(slot);
            ElectricSparkFields& params = classState<ElectricSparkFields>(spark);
            spark.flags = kActorLiveDrawn;
            spark.type = kElectricSparkType;
            params.radius = &blast.radius;
            params.point.x = actor.pos.x.whole;
            params.point.y = actor.pos.y.whole;
            params.point.z = actor.pos.z.whole;
        }
    }
    blast.hitBox.result = 0;
    blast.hitBox.previousX = actor.pos.x.whole;
    blast.hitBox.previousZ = actor.pos.z.whole;
    if (planarDistance(player().pos, actor.pos) < int(uint32_t(blast.radius) << 2)) { // exe 0x5e3b00
        Player& p = player();
        p.pulled = 1;
        p.pullX = actor.pos.x.whole;
        p.pullY = actor.pos.y.whole;
        p.pullZ = actor.pos.z.whole;
        p.pullStrength = 0x200;
        p.pullRadius = int16_t(uint32_t(blast.radius) << 2);
    }
}
void blastTick(ProjectileActor& actor) {
    BlastFields& blast = classState<BlastFields>(actor);
    switch (BlastState(actor.state)) { // [0x8d0fb4]
    case BlastState::Start: blastStart(actor, blast); return;
    case BlastState::Run: blastRun(actor, blast); return;
    default: notPortedAt(kStoryPort, "0x5c3690 state past [0x8d0fb4]", actor.state);
    }
}

// ---- the rock effects (pool 0xa0 type 0x24, exe 0x43ff80): the warning ring (variant 3), the dust (variant 4) -------
enum class WarningState : uint8_t { FadeIn = 0, Hold = 1, FadeOut = 2 };
constexpr uint32_t kSparkles = 0x10;
constexpr uint8_t kSparkleLive = 0x80;

// The warning ring's own bytes: its tick count (the step byte), its live sparkles (the step counter's byte), the rock it
// warns of (its spawn bytes), the sparkles (bit 7 live, the low nibble its age: the trail's bytes)
struct WarningRingFields {
    explicit WarningRingFields(const EffectActor& actor) : WarningRingFields(const_cast<EffectActor&>(actor)) {}
    explicit WarningRingFields(EffectActor& actor)
        : rock(classState<WarningRingParams>(actor).rock), ticks(actor.step), sparkleCount(actor.stepCounter),
          sparkles(actor.trail, 0), slot_(actor) {}
    ActorRef<AnimatedActor>& rock;
    uint8_t& ticks;
    uint8_t& sparkleCount;
    ArrayFields<uint8_t, kSparkles, int16_t, 8> sparkles;
    void clearSparkles() { slot_.trail = {}; }

private:
    EffectActor& slot_;
};
// The projection scratch's bytes the ring's draw writes as the exe does (besides the offset point in its first quad
// corner and the screen point): the colours (the GTE colour registers; four words, then the sparkle's), the screen
// means and the OT depths
struct RingDrawScratch {
    std::array<Lane32, 4> colourWords = geometryScratch().words<4>(0x78);
    std::array<Lane8, 0x10> colourBytes = geometryScratch().bytes<0x10>(0x78);
    Lane32 sparkleColour = geometryScratch().word(0x88);
    std::array<Lane8, 2> sparkleColourBytes = geometryScratch().bytes<2>(0x88);
    Lane16 meanX = geometryScratch().lane(0x114);
    Lane16 meanY = geometryScratch().lane(0x116);
    Lane32 centreDepth = geometryScratch().word(0x130);
    Lane32 segmentDepth = geometryScratch().word(0x134);
};
RingDrawScratch& ringScratch() {
    static RingDrawScratch s;
    return s;
}

// the camera-input point of the local offset (x, y, z) around the actor: the exe loads the camera with the
// translation camera * pos + camera T (0x40bab0 / 0x40c3c0 / 0x40c3e0, the actor's s16 point unshifted) and puts the
// offset through it: the port draws pos + offset through its own camera (rounding may differ by a unit)
void ringPoint(const EffectActor& actor, int x, int y, int z, int16_t out[3]) {
    out[0] = t16(int(actor.pos.x) + x);
    out[1] = t16(int(actor.pos.y) + y);
    out[2] = t16(int(actor.pos.z) + z);
}
// exe 0x440d40's state part: the brightness and the radius R; false = freed
bool warningState(EffectActor& actor, WarningRingFields& ring, uint32_t& bright, uint32_t& R) {
    switch (WarningState(actor.state)) {
    case WarningState::FadeIn: {
        if (actor.subState == 0) {
            ring.clearSparkles(); // exe 0x40dc40
            actor.subState = 1;
            ring.ticks = 1;
            ring.sparkleCount = 0;
        }
        const uint32_t n = ring.ticks;
        R = n << 5;
        bright = R > 0xff ? 0xff : R;
        if (n == 7) actor.enterStateKeepingStep(uint8_t(WarningState::Hold));
        else ring.ticks = uint8_t(n + 1);
        return true;
    }
    case WarningState::Hold: // 0x50 ticks or until the rock's report byte is 0
        if (actor.subState == 0) {
            actor.subState = 1;
            ring.ticks = 0;
        }
        bright = 0xff;
        R = 0x100;
        ++ring.ticks;
        if (ring.ticks == 0x50 || ring.rock->stepCounter == 0) actor.enterStateKeepingStep(uint8_t(WarningState::FadeOut));
        return true;
    default: { // fade out, freed after 7
        if (actor.subState == 0) {
            actor.subState = 1;
            ring.ticks = 0;
        }
        if (ring.ticks == 7) {
            freeActor(actor); // exe 0x5e1580
            return false;
        }
        const uint32_t n = uint32_t(ring.ticks) + 1u;
        ring.ticks = uint8_t(n);
        const uint32_t v = 0x100u - (n << 5);
        bright = v > 0xff ? 0xff : v;
        R = (n + 8) << 5;
        return true;
    }
    }
}
// one sparkle: an OT POLY_FT4 of +-bp pixels around the projected point (drawn as a one-colour GT4, as game/global_fx)
void sparkleQuad(const EffectActor& actor, const int16_t p[3], const CameraPoint& c, int bp) {
    RingDrawScratch& scratch = ringScratch();
    const uint32_t col = scratch.sparkleColour & 0xffffffu;
    const uint32_t g[13] = {0, scratch.sparkleColour | 0x2e000000u | 0x10000000u, 0, 0x7d502020u, col, 0, 0x2f203fu, col,
                            0, 0x3f20u, col, 0, 0x3f3fu};
    const float k = pixelUnits(c);
    const int16_t ox[4] = {t16(-bp), t16(bp), t16(-bp), t16(bp)}, oy[4] = {t16(-bp), t16(-bp), t16(bp), t16(bp)};
    int16_t v[4][3];
    float off[4][2];
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 3; ++j) v[i][j] = p[j];
        off[i][0] = float(ox[i]) * k;
        off[i][1] = float(oy[i]) * k;
    }
    for (uint32_t i = 0; i < 4; ++i) // the exe's corner words
        projectionScratch().quadScreenPoints[i] =
            uint32_t(uint16_t(c.sx + ox[i])) | uint32_t(uint16_t(c.sy + oy[i])) << 16;
    dc::otQuadGT4(actor.at, g, v, off);
}
// exe 0x440d40: the warning ring
void warningRing(EffectActor& actor) {
    WarningRingFields ring(actor);
    RingDrawScratch& scratch = ringScratch();
    ProjectionScratch& proj = projectionScratch();
    uint32_t bright = 0, R = 0;
    if (!warningState(actor, ring, bright, R)) return;
    if (actor.state < uint8_t(WarningState::FadeOut)) { // the live sparkles grow to 16; empty slots refilled
        if (ring.sparkleCount < kSparkles) ++ring.sparkleCount;
        for (uint32_t i = 0; i < ring.sparkleCount; ++i)
            if (ring.sparkles[i] == 0) ring.sparkles[i] = uint8_t((rngStep() & 0xf0) | kSparkleLive);
    }
    // the colours: {b, b / 2}, their mean, {b, b / 4}, the mean again
    scratch.colourWords[2] = bright;
    scratch.colourWords[0] = bright;
    scratch.colourBytes[1] = uint8_t(bright >> 1);
    scratch.colourBytes[9] = uint8_t(scratch.colourBytes[8] >> 2);
    scratch.colourWords[1] = 0;
    scratch.colourBytes[4] = uint8_t((uint32_t(scratch.colourBytes[0]) + scratch.colourBytes[8]) >> 1);
    scratch.colourBytes[5] = uint8_t((uint32_t(scratch.colourBytes[1]) + scratch.colourBytes[9]) >> 1);
    scratch.colourBytes[6] = uint8_t((uint32_t(scratch.colourBytes[2]) + scratch.colourBytes[10]) >> 1);
    scratch.colourWords[3] = scratch.colourWords[1].get();
    const int16_t pos[3] = {actor.pos.x, actor.pos.y, actor.pos.z};
    const int bp = projectedWidth(pos, 4, nullptr) + 1; // exe 0x5f2f20
    // the sparkles: age +1 (0xf: the live bit cleared), rising (age * 16) on a circle of radius ((s >> 3) & 0xe) + (age
    // * R >> 5) + 0x30 at the angle (frame + i + (s & 0x70)) << 5; on the screen and in depth: an FT4 of colour {m * 16,
    // m * 8} (m = age + 1 up to 8, then 16 - age); a dead one cleared to 0
    LaneVec4 offset = proj.quadCorners[0];
    for (uint32_t i = 0; i < kSparkles; ++i) {
        uint32_t s = ring.sparkles[i];
        if (!(s & kSparkleLive)) continue;
        const uint32_t n = s & 0xf;
        s = n == 0xf ? (s & 0x7f) : ((s & 0xf0) | (n + 1));
        ring.sparkles[i] = uint8_t(s);
        scratch.sparkleColour = 0;
        const uint32_t m = (n < 8 ? n + 1 : 0x10 - n) << 4;
        scratch.sparkleColourBytes[0] = uint8_t(m);
        scratch.sparkleColourBytes[1] = uint8_t((m & 0xff) >> 1);
        const int32_t r = int32_t((s >> 3) & 0xe) + (mul(int32_t(n), int32_t(R)) >> 5) + 0x30;
        const uint32_t ang = ((uint32_t(gameState().frame) + i + (s & 0x70)) << 5) & 0xfff;
        offset.y = int16_t(uint32_t(-int32_t(n)) << 4);
        offset.x = int16_t(mul(fixedSin(int(ang)), r) >> 12);
        offset.z = int16_t(-mul(fixedCos(int(ang)), r) >> 12);
        int16_t p[3];
        ringPoint(actor, offset.x, offset.y, offset.z, p);
        const CameraPoint c = cameraPoint(p); // 0x40c6a0 / 0x40c860 / 0x40cb10 / 0x40cae0
        proj.point.x = c.sx;
        proj.point.y = c.sy;
        if (!c.clipped && uint16_t(c.sy) < 0xf0 && uint16_t(c.sx) < 0x140) {
            const int32_t otz = rtpsDepth(c) >> 2; // exe 0x40d010
            proj.quadDepth = otz;
            if (otz >= 1) sparkleQuad(actor, p, c, bp);
        }
        if (!(ring.sparkles[i] & kSparkleLive)) ring.sparkles[i] = 0;
    }
    // the ring: 16 segments of 0x100 from the angle frame << 7: outer points (radius R, raised R >> 2) black, inner
    // (R >> 1, on the floor) coloured [k & 3] / [k + 1 & 3]: an OT POLY_G4 (DR_MODE tpage 0x3e: abr 1) unless all four
    // are below / right of the screen (with their mean) or the OTZ < 1; before it (independent of those tests) the
    // POLY_G3 0x32 fan {colour [k & 3], inner k} {[k + 1 & 3], inner k + 1} {black, centre} at the centre's OTZ (0 when
    // the centre's u16 sy >= 0x168 and sx >= 0x1e0; drawn when != 0; DR_MODE tpage 0x3e: abr 1)
    int16_t centre[3];
    ringPoint(actor, 0, 0, 0, centre);
    const CameraPoint cc = cameraPoint(centre); // 0x40c6a0 / 0x40c860 / 0x40cb10
    proj.point.x = cc.sx;
    proj.point.y = cc.sy;
    const int32_t centreOtz = (uint16_t(cc.sy) >= 0x168 && uint16_t(cc.sx) >= 0x1e0) ? 0 : rtpsDepth(cc) >> 2;
    scratch.centreDepth = centreOtz;
    const int32_t rr = int32_t(R), half = rr >> 1, base = int32_t(uint32_t(gameState().frame) << 7);
    const int16_t yOut = t16(-(half >> 1));
    auto outer = [&](uint32_t an, int16_t o[3]) {
        ringPoint(actor, t16(mul(fixedSin(int(an)), rr) >> 12), yOut, t16(-mul(fixedCos(int(an)), rr) >> 12), o);
    };
    auto inner = [&](uint32_t an, int16_t o[3]) {
        ringPoint(actor, t16(mul(fixedSin(int(an)), half) >> 12), 0, t16(-mul(fixedCos(int(an)), half) >> 12), o);
    };
    int16_t oPrev[3], iPrev[3];
    outer(uint32_t(base) & 0xfff, oPrev);
    inner(uint32_t(base) & 0xfff, iPrev);
    for (uint32_t k = 0; k < 0x10; ++k) {
        const uint32_t an = (((k + 1) << 8) + uint32_t(base)) & 0xfff;
        int16_t v[4][3];
        for (int j = 0; j < 3; ++j) {
            v[0][j] = oPrev[j];
            v[1][j] = iPrev[j];
        }
        outer(an, v[2]);
        inner(an, v[3]);
        if (centreOtz != 0) {
            static const float kNoOff3[3][2] = {};
            const uint32_t c3[3] = {scratch.colourWords[k & 3] | 0x32000000u,
                                    uint32_t(scratch.colourWords[(k + 1) & 3]), 0};
            const int16_t v3[3][3] = {{v[1][0], v[1][1], v[1][2]}, {v[3][0], v[3][1], v[3][2]},
                                      {centre[0], centre[1], centre[2]}};
            dc::otTriG3(actor.at, c3, 1, v3, kNoOff3);
        }
        int16_t sxy[4][2];
        const int32_t otz = projectQuad(v, sxy); // 0x40c460 / 0x40cad0 (the four depths >> 4)
        bool skip = false;
        if (uint16_t(sxy[0][1]) >= 0xf0 && uint16_t(sxy[1][1]) >= 0xf0 && uint16_t(sxy[2][1]) >= 0xf0 &&
            uint16_t(sxy[3][1]) >= 0xf0) {
            const int16_t m = t16((int(sxy[0][1]) + sxy[1][1] + sxy[2][1] + sxy[3][1]) >> 2);
            scratch.meanY = m;
            skip = uint16_t(m) >= 0xf0;
        }
        if (!skip && uint16_t(sxy[0][0]) >= 0x140 && uint16_t(sxy[1][0]) >= 0x140 && uint16_t(sxy[2][0]) >= 0x140 &&
            uint16_t(sxy[3][0]) >= 0x140) {
            const int16_t m = t16((int(sxy[0][0]) + sxy[1][0] + sxy[2][0] + sxy[3][0]) >> 2);
            scratch.meanX = m;
            skip = uint16_t(m) >= 0x140;
        }
        if (!skip) {
            scratch.segmentDepth = otz;
            if (otz >= 1) {
                static const float kNoOff[4][2] = {};
                const uint32_t c[4] = {0x3a000000u, uint32_t(scratch.colourWords[k & 3]), 0,
                                       uint32_t(scratch.colourWords[(k + 1) & 3])};
                dc::otQuadG4(actor.at, c, 1, v, kNoOff);
            }
        }
        for (int j = 0; j < 3; ++j) {
            oPrev[j] = v[2][j];
            iPrev[j] = v[3][j];
        }
    }
}

// The projection scratch's bytes the dust's billboards are built in as the exe does: the offset and the corner
// (x / y / z and their spare lanes), the corner's two words, the texels (two words, read as halves)
struct DustScratch {
    LaneVec4 offset = geometryScratch().vec4(0x0);
    Lane16 offsetSpare = geometryScratch().lane(0x6);
    LaneVec4 corner = geometryScratch().vec4(0x8);
    Lane16 cornerSpare = geometryScratch().lane(0xe);
    Lane32 cornerXY = geometryScratch().word(0x8);
    Lane32 cornerZ = geometryScratch().word(0xc);
    std::array<Lane32, 2> texelWords = geometryScratch().words<2>(0xa0);
    std::array<Lane16, 4> texelHalves = geometryScratch().lanes<4>(0xa0);
};
DustScratch& dustScratch() {
    static DustScratch s;
    return s;
}
constexpr uint32_t kDustTicks = 0x10;
// exe 0x4416a0: the dust: 4 random dwords (first tick); 8 x 4 billboard records (grey (ticks & 0xe) << 4) on a ring of
// its radius (angle (random >> 4) + j * 0x80, rising by the random's low nibble); 16 ticks, then freed; the centre
// rises by its rise speed (which drops by its brake), the ring grows by its spread
void landingDust(EffectActor& actor) {
    LandingDustFields& dust = classState<LandingDustFields>(actor);
    DustScratch& scratch = dustScratch();
    if (actor.state == 0) {
        for (uint32_t k = 0; k < 4; ++k) dust.randoms[k] = rngStep();
        actor.enterState(1);
    }
    const int32_t R = dust.radius;
    const uint32_t c = (uint32_t(actor.subState) & 0xe) << 4, cc = c | (c << 16);
    scratch.cornerSpare = 0;
    scratch.offsetSpare = 0;
    scratch.texelWords[0] = cc - 0x3fe04000u;
    scratch.texelWords[1] = cc + 0xdf1fdf00u;
    const uint32_t col = dc::billboardColour(0x808080);
    const uint32_t w3 = uint32_t(scratch.texelHalves[0]) | 0x7dd00000u, w5 = uint32_t(scratch.texelHalves[1]) | 0x2f0000u;
    const uint32_t w7 = scratch.texelHalves[2], w9 = scratch.texelHalves[3];
    for (uint32_t j = 0; j < 8; ++j) {
        const uint16_t random = dust.randomHalves[j];
        const uint32_t ang = ((uint32_t(random) >> 4) + j * 0x80) & 0xfff;
        const uint32_t size = (ang << 16) | uint32_t(int32_t(dust.size));
        scratch.offset.x = int16_t(mul(fixedSin(int(ang)), R) >> 12);
        scratch.offset.y = int16_t(-int32_t(uint8_t(random) & 0xf));
        scratch.offset.z = int16_t(-mul(fixedCos(int(ang)), R) >> 12);
        scratch.corner.y = int16_t(uint16_t(scratch.offset.y) + uint16_t(actor.pos.y));
        scratch.corner.x = int16_t(uint16_t(actor.pos.x) + uint16_t(scratch.offset.x));
        scratch.corner.z = int16_t(uint16_t(actor.pos.z) + uint16_t(scratch.offset.z));
        const uint32_t xy0 = scratch.cornerXY, z0 = scratch.cornerZ;
        scratch.corner.z = int16_t(uint16_t(actor.pos.z) - uint16_t(scratch.offset.z));
        const uint32_t xy2 = scratch.cornerXY, z2 = scratch.cornerZ;
        scratch.corner.x = int16_t(uint16_t(actor.pos.x) - uint16_t(scratch.offset.x));
        const uint32_t xy1 = scratch.cornerXY, z1 = scratch.cornerZ;
        scratch.corner.z = int16_t(uint16_t(actor.pos.z) + uint16_t(scratch.offset.z));
        const uint32_t xy3 = scratch.cornerXY, z3 = scratch.cornerZ;
        dc::emit({0, col, xy0, w3, z0, w5, size, w7, 0, w9}, actor.at, false);
        dc::emit({0, col, xy1, w3, z1, w5, size, w7, 0, w9}, actor.at, false);
        dc::emit({0, col, xy2, w3, z2, w5, size, w7, 0, w9}, actor.at, false);
        dc::emit({0, col, xy3, w3, z3, w5, size, w7, 0, w9}, actor.at, false);
    }
    ++actor.subState;
    if (actor.subState == kDustTicks) {
        freeActor(actor); // exe 0x5e1580
        return;
    }
    const uint32_t c0 = dust.rise;
    actor.pos.y = int16_t(uint16_t(actor.pos.y) - uint16_t(int16_t(int8_t(c0))));
    dust.rise = uint8_t(c0 - dust.brake);
    dust.radius = int16_t(uint16_t(dust.radius) + uint16_t(int16_t(dust.spread)));
}

} // namespace

void bossBeamUpdate(const PlayerEnv& env, uint32_t actor) { beamTick(env, projectileActorAt(actor)); }

void ballVisualsUpdate(const PlayerEnv&, uint32_t actor) { ballFx(staticActorAt(actor)); }

void ballExplosionUpdate(const PlayerEnv&, uint32_t actor) { blastTick(projectileActorAt(actor)); }

void rockWarningRingUpdate(const PlayerEnv&, uint32_t actor) { warningRing(effectActorAt(actor)); }

void rockLandingDustUpdate(const PlayerEnv&, uint32_t actor) { landingDust(effectActorAt(actor)); }

} // namespace od::port
