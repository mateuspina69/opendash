// ST5C: the attack classes the boss 0x50b9c0 (content/late_game/st5c) spawns. The boss's side: attack 6 spins out 15
// blades (its report byte 1 while they live), attack 7 two charges (report 1; they report 2, the beam 3 / 4), attack
// 8's stomp 2 x 3 shots, attack 9 0x10 rocks (spawned with the boss), attack 10 two charges (report 1) and a ball. All
// of them keep the boss and die with it (its report byte back to 0 / its hit points < 0). Names are hypotheses from
// the observed behaviour.
//
// The rock (pool 0x20 type 0x86, exe 0x50ee90; its block from +0x14c): a rock dropped at the player. States
// ([0x8984f4]): 0 bind 0x50eef0 (model variant 0, pitch 0x400 (straight down), speed 0xc00, bone colours, a warning
// ring under it), 1 fall 0x50f020 (sound 0x2d4 once past y -0x700, a hit capsule radius 0x50 from the previous to the
// current point; past y -0x100 -> 2; the boss dead -> freed), 2 landed 0x50f160 (a blast + the landing dust, hidden, 6
// ticks of the full-screen tint exe 0x5f3150, freed).
//
// The ray charges (static types 0x5b / 0x60; their block from +0x28: a timer, a word, 32 ray angles, lengths and s8
// spins, the boss). Attack 10's charge (type 0x5b, exe 0x5bb6e0): states ([0x8d0c8c]): 0 0x5bb700 (timer 0x1f, the
// rays randomised), 1 0x5bb8c0 (the timer runs; variant 0 draws 0x5bba80 (screen rings), variant 1 0x5bbd30 (OT
// POLY_G3 rays turning by the spins); at 0: variant 1 freed, variant 0 placed between the boss's bones 3 / 6, 4 ticks of
// 0x5bbf40 (rings + OT POLY_G4 rays) -> 2), 2 0x5bba40 (0x5bbf40 until the timer runs out), freed as soon as the boss's
// report is 0. States 3..5 (0x5bc420 / 0x5bc560 / 0x5bc9c0: a homing shot) have no spawner of type 0x5b in the exe
// (only the boss's 0x50d01d, state 0): not ported (notPortedAt).
// Attack 7's charge (type 0x60, exe 0x5be190): states ([0x8d0dc8]): 0 0x5be1b0 (timer 0x2f, sound 0x2cc for variant 0),
// 1 0x5be370 (variant 0 draws 0x5be5e0 (screen rings), variant 1 0x5be840 (OT POLY_G4 rays in the view plane at the
// boss's bone 7); at 0: variant 1 freed, variant 0 spawns the beam (0x5cf370), sets the boss's report = 2, goes to bone
// 7, 4 ticks of 0x5bed10 (screen rings + OT POLY_G4 rays) -> 2), 2 0x5be520 (0x5bed10; at 0: 8 ticks of 0x5bf110 -> 3),
// 3 0x5be580 (0x5bf110 (screen rings) until the boss's report is 0 or 3), 4 0x5be5b0 (8 ticks, freed). States 5 / 6
// (0x5bf380 / 0x5bf460) belong to the class 0x5bf360 (table 0x8d0ddc): not ported here.
//
// The blade (static type 0x66, exe 0x5c16b0; its block from +0x74: a timer, its segments, the bone / height / radius it
// was spawned with (its position's words), a hit box, the boss): states ([0x8d0e7c]): 0 0x5c16d0, 1 0x5c1720 (4 ticks
// at the boss's bone, two ring pairs 0x5c1930), 2 0x5c1760 (at the bone, around the boss by the yaw difference; up to 7
// steps of 0x80 along the rotation (pitch, boss yaw - yaw - 0x400, roll) until a wall (rings at the wall 0x5c1f30); the
// segments drawn by 0x5c1a80 (two rings + per segment 3 OT POLY_G4 fins and 6 OT POLY_G3 discs); a hit capsule over the
// length while the boss's report is set, then 8 ticks and freed).
//
// The stomp's shots (pool 0x40 type 0x32, exe 0x5d16d0) and attack 10's ball (type 0x33, exe 0x5d1a10) share the table
// 0x8d188c (their block from +0xbc). The shot (variant = j << 4 | k): 0 0x5d1720 (model variant 0; at the boss's bone
// 3 / 6 on the floor (y 0); its travel yaw toward the player + j * 0x100 + k * 0x70 - 0xf0), 1 0x5d1880 (0x80 ticks,
// spinning, a capsule 0x18 high 0x300; hit something / the player's shield exe 0x5737b0 / the timer -> freed); after a
// state returning 0 the ground probe exe 0x40f190 > 0 frees it. The ball (entries 2..4): 0 0x5d1a30 (three ball visuals
// 0x5bd320, between the bones 3 / 6, pitch / yaw at the player, sound 0x2d1), 1 0x5d1b70 (0x60 ticks along them, a point
// hit entry; the timer / below y 0 / a hit -> the explosion (0x5c3690), the tint, sound 0x2d2, 2), 2 0x5d1cc0 (4 ticks
// of the tint, freed).
//
// Render output: the billboard / ring / screen-ring draw records are written with dc::emit exactly as the exe (the
// screen rings carry screen positions; game/draw_commands does not draw them); the OT POLY_G4 through dc::otQuadG4
// (screen-pixel corners brought back to the camera plane as content/nino/nino_ruin_st35, view-plane corners as
// content/saul_kada/saul_kada_st28fx, the blade's local frame corners as camera-input points); the OT POLY_G3 of
// 0x5bbd30 / 0x5c1a80 through dc::otTriG3; the full-screen tints exe 0x5f3150 go through od::dc::screenTint. Models
// through the standard +0xa4 path. Spawned elsewhere: the rock effects, the beam, the ball visuals, the explosion
// (content/late_game/st5c_fx2), the shield spark of exe 0x5737b0 (pool 0xa0 global type 6), the blast (static global
// type 7 0x563b10: game/explosions).
#include "content/late_game/st5c_fx.hpp"
#include "content/late_game/st5c_fx_state.hpp"

#include "content/late_game/st5c_fx2.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/draw_commands.hpp"
#include "game/explosions.hpp"
#include "game/vectors.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st5c_fx_state;

namespace {

int16_t t16(int64_t v) { return int16_t(uint16_t(uint64_t(v))); }
int32_t mul(int32_t a, int32_t b) { return int32_t(uint32_t(a) * uint32_t(b)); } // imul (32-bit wrap)
int16_t wholeOf(int32_t v) { return int16_t(uint32_t(v) >> 16); }                 // a 16.16 value's integer part
uint32_t shiftByte() { return stage().coordinateShift; }                          // the stage shift
// a 6-word ring / screen-ring draw record at the draw cursor
void ringRecord(uint32_t owner, uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3, uint32_t w4, uint32_t w5) {
    dc::emit({w0, w1, w2, w3, w4, w5}, owner);
}
// the colour words of the records: c in all three bytes / bytes 0 + 1 (the exe's or / shl chains, kept literal)
uint32_t col3(uint32_t c) { return (((c | 0xfffff400u) << 8 | c) << 8) | c; }
uint32_t col3f0(uint32_t c) { return (((c | 0xfffff000u) << 8 | c) << 8) | c; }
// an actor's s16 x | y << 16 (the y not sign-extended: the exe's two word reads)
uint32_t xyWordOf(const Actor& actor) {
    return uint32_t(uint16_t(actor.pos.x.whole)) | uint32_t(uint16_t(actor.pos.y.whole)) << 16;
}
// exe 0x5e1810: bone n's point (a null result would crash the exe)
ShortVec4& bossBone(const AnimatedActor& actor, uint32_t n) {
    BoneBlock* block = boneBlockOf(actor, int(n >> 2));
    if (!block) notPortedAt(kStoryPort, "st5c_fx: exe 0x5e1810 returned null, bone", n);
    return block->points[n & 3];
}
// the boss's stat row (a null row would crash the exe)
AnimatedActorStats bossStats(const AnimatedActor& boss) {
    if (!hasAnimatedStats(boss.type)) notPortedAt(kStoryPort, "st5c_fx: null type row (the exe faults), type", boss.type);
    return animatedStats(boss.type);
}
// the point v (local) through bone `bone` of the actor (exe 0x5e1970 on the exe's stack vector)
void bonePointAt(const AnimatedActor& actor, uint32_t bone, int x, int y, int z, int16_t out[3]) {
    const int16_t v[3] = {int16_t(x), int16_t(y), int16_t(z)};
    bonePointOffset(actor, bone, v, out);
}
// the player's 16.16 position with y - 0x80 (the exe's local target)
void playerTarget(int32_t t[3]) {
    const Player& p = player();
    t[0] = p.pos.x;
    t[1] = int32_t(uint32_t(p.pos.y) + 0xff800000u);
    t[2] = p.pos.z;
}
void positionOf(const Actor& actor, int32_t p[3]) {
    p[0] = actor.pos.x;
    p[1] = actor.pos.y;
    p[2] = actor.pos.z;
}

// ---- the draw side --------------------------------------------------------------------------------------------
// The camera-input point of an s16 point (<< the stage shift, as exe 0x5f2dd0 feeds the GTE)
void camPoint(const int16_t p[3], int16_t out[3]) {
    const uint32_t s = shiftByte() & 0x1f;
    for (int k = 0; k < 3; ++k) out[k] = t16(int64_t(uint32_t(int32_t(p[k])) << s));
}
// An OT POLY_G4 whose corners the exe puts at the screen-pixel offsets px[k] from the projected point of p: the port
// keeps p's camera-input point and brings the offsets back to camera-input units at p's depth (L = the width given
// to exe 0x5f2f20, w its result, as content/nino/nino_ruin_st35)
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
// the same for a POLY_G3 (dc::otTriG3)
void screenG3(uint32_t owner, const int16_t p[3], int L, int w, const uint32_t c[3], uint8_t abr, const int px[3][2]) {
    const float scale = float(L << (shiftByte() & 0x1f)) / float(w);
    int16_t cp[3];
    camPoint(p, cp);
    int16_t v[3][3];
    float off[3][2];
    for (int k = 0; k < 3; ++k) {
        for (int j = 0; j < 3; ++j) v[k][j] = cp[j];
        off[k][0] = float(px[k][0]) * scale;
        off[k][1] = float(px[k][1]) * scale;
    }
    dc::otTriG3(owner, c, abr, v, off);
}
// the screen ray quad of 0x5bbf40 / 0x5bed10: corners at angles a0 / a1, lengths len (outer) / in (inner)
void rayPx(int a0, int a1, int32_t len, int32_t in, int px[4][2]) {
    px[0][0] = mul(fixedCos(int(uint32_t(a0))), len) >> 12;
    px[0][1] = mul(fixedSin(int(uint32_t(a0))), len) >> 12;
    px[1][0] = mul(fixedCos(int(uint32_t(a1))), len) >> 12;
    px[1][1] = mul(fixedSin(int(uint32_t(a1))), len) >> 12;
    px[2][0] = mul(fixedCos(int(uint32_t(a0))), in) >> 12;
    px[2][1] = mul(fixedSin(int(uint32_t(a0))), in) >> 12;
    px[3][0] = mul(fixedCos(int(uint32_t(a1))), in) >> 12;
    px[3][1] = mul(fixedSin(int(uint32_t(a1))), in) >> 12;
}
// the projected point (exe 0x5f2dd0) and the projected width of `width` (exe 0x5f2f20) of p: false when either fails
// (the exe's early returns)
bool project(const int16_t p[3], int width, int16_t sxy[2], int& w, int16_t& depth) {
    if (projectOnScreen(p, sxy) < 0) return false;
    depth = 0;
    w = projectedWidth(p, int16_t(width), &depth);
    return w != 0;
}
uint32_t sxyWord(const int16_t sxy[2]) { return uint32_t(uint16_t(sxy[0])) | uint32_t(uint16_t(sxy[1])) << 16; }
// the screen point at angle `ang` and length len from sxy (16-bit sums, as the exe's word stores)
uint32_t rayPoint(const int16_t sxy[2], int ang, int32_t len) {
    const uint16_t x = uint16_t((mul(fixedCos(int(uint32_t(ang))), len) >> 12) + sxy[0]);
    const uint16_t y = uint16_t((mul(fixedSin(int(uint32_t(ang))), len) >> 12) + sxy[1]);
    return uint32_t(x) | uint32_t(y) << 16;
}
// the depth word of the screen-ring records: depth - d, at least 1 (16-bit test)
uint32_t depthWord(int16_t depth, int d) {
    const int16_t v = t16(int(depth) - d);
    return v > 0 ? uint16_t(v) : 1u;
}

// ---- the rock (pool 0x20 type 0x86, exe 0x50ee90) -------------------------------------------------------------------
enum class RockState : uint8_t { Bind = 0, Fall = 1, Landed = 2 };
constexpr uint16_t kRockFallSound = 0x2d4;
constexpr uint32_t kRockEntry = hitGroup(3) | hitCapsule(1);

// a rock effect (pool 0xa0 stage type 0x24) of the variant under the rock (exe 0x5e13a0); 0 when the pool is full
uint32_t spawnRockEffect(const AnimatedActor& actor, uint8_t variant) {
    const uint32_t slot = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (!slot) return 0;
    EffectActor& e = effectActorAt(slot);
    e.flags = kActorLive;
    e.type = kRockEffectType;
    e.variant = variant;
    e.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
    e.pos.y = 0;
    e.pos.x = actor.pos.x.whole;
    e.pos.z = actor.pos.z.whole;
    return slot;
}
// exe 0x50eef0: bind (a failed bind / effect allocation frees the rock and the exe goes on), a warning ring at its x /
// z, straight down at 0xc00, bone 1 coloured / tinted
void rockBind(AnimatedActor& actor, RockFields& rock) {
    if (!bindActorModel(actor, 0)) freeActor(actor); // exe 0x5e0ba0 / 0x5e1490
    pointAtWord(rock.boss, rock.spawnBoss);
    if (const uint32_t slot = spawnRockEffect(actor, kWarningRingVariant)) {
        classState<WarningRingParams>(actorHeaderAt(slot)).rock = &actor;
    } else {
        freeActor(actor); // exe 0x5e1490
    }
    actor.rotation.y = 0;
    actor.rotation.z = 0;
    actor.velocity.x = 0;
    actor.prevPos = actor.pos;
    actor.velocity.y = 0;
    actor.velocity.z = 0xc00;
    actor.anim = 0;
    actor.prevAnim = 0xff;
    actor.rotation.x = 0x400;
    actor.scale.x = 0x400;
    actor.scale.y = 0x400;
    actor.scale.z = 0x400;
    actor.animKey.frame = 0;
    actor.actorHit = 0;
    rock.hitBox.result = 0;
    rock.tintTicks = 0;
    rock.word = 0;
    rock.soundPlayed = 0;
    resetBoneScales(actor); // exe 0x5e3220
    // exe 0x5e1850(actor, 1, &c): bone 1's scale word; c = the argument slot with its three low bytes set to 0x70 (the
    // high byte is the actor pointer's)
    setBoneScaleWord(actor, 1, 0x707070u | (actor.at & 0xff000000u));
    BoneBlock* block = boneBlockOf(actor, 0); // exe 0x5e1810 (bone 1's colour)
    if (!block) notPortedAt(kStoryPort, "st5c_fx: exe 0x5e1810 returned null, bone", 1);
    block->colour(1) = 0x2108;
    setBoneSemiTransparency(actor, 1, 1, 1); // exe 0x5e1a20(actor, 1, 1, 1)
    ++actor.state;
    ++actor.stepCounter;
}
// exe 0x50f020: fall (sound 0x2d4 once past y -0x700), a capsule (radius 0x50) from the previous to the current point,
// landed past y -0x100 -> 2; the boss dead -> freed
void rockFall(AnimatedActor& actor, RockFields& rock) {
    actor.prevPos = actor.pos;
    moveAlongRotation(actor.pos, actor.rotation, 0, 0, -int(actor.velocity.z)); // exe 0x5e3810
    if (actor.pos.y.whole > -0x700 && rock.soundPlayed == 0) {
        soundPlayAtPos(kRockFallSound, actor.pos);
        ++rock.soundPlayed;
    }
    const int16_t cur[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const int16_t prev[3] = {actor.prevPos.x.whole, actor.prevPos.y.whole, actor.prevPos.z.whole};
    rock.hitBox.result = 0;
    rock.hitBox.previousX = actor.prevPos.x.whole;
    rock.hitBox.previousZ = actor.prevPos.z.whole;
    const AnimatedActorStats row = bossStats(*rock.boss);
    hitListSubmitCapsule(cur, 0x50, prev, 0x50, rock.hitBox, hitOwner(actor.slot) | kRockEntry,
                         uint32_t(row.hitSize) | kHitAttackStrong); // exe 0x5e4190
    actor.airborne = kAirborneFalling;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    if (actor.pos.y.whole > -0x100) {
        actor.subState = 0;
        actor.stepCounter = 0;
        ++actor.state;
        return;
    }
    if (rock.boss->hitPoints < 0) {
        actor.stepCounter = 0;
        freeActor(actor); // exe 0x5e1490
    }
}
// exe 0x50f160: landed: a blast (0x180, the row's hit size >> 2) and the landing dust, hidden, then 6 ticks of the
// full-screen tint exe 0x5f3150 (od::dc::screenTint) and freed
void rockLand(AnimatedActor& actor, RockFields& rock) {
    switch (actor.subState) {
    case 0: {
        if (const uint32_t slot = allocActor60(0)) { // exe 0x5e1300
            StaticActor& blast = staticActorAt(slot);
            BlastParams params(actorHeaderAt(slot));
            blast.flags = kActorLive;
            blast.type = typeByte(GlobalStaticType::Blast);
            blast.variant = 1;
            blast.loopFlags |= kActorSweepGroup2;
            blast.pos.y = 0;
            blast.pos.x = actor.pos.x;
            blast.pos.z = actor.pos.z;
            params.size = 0x180;
            params.attack = uint16_t(uint32_t(bossStats(*rock.boss).hitSize) >> 2);
        }
        if (const uint32_t slot = spawnRockEffect(actor, kLandingDustVariant)) {
            LandingDustParams dust(effectActorAt(slot));
            dust.radius = 0x280;
            dust.rise = 0xf;
            dust.brake = 1;
            dust.spread = 0x1f;
            dust.size = 0x60;
        }
        actor.setDrawn(false);
        rock.tintTicks = 6;
        ++actor.subState;
        [[fallthrough]];
    }
    case 1: {
        // exe 0x5f3150(((W << 4 | 0x2200) << 8 | W << 4) << 8 | W << 4): the full-screen tint
        const uint32_t W = uint32_t(rock.tintTicks) << 4;
        dc::screenTint(((W | 0x2200u) << 8 | W) << 8 | W);
        rock.tintTicks -= 1u;
        if (rock.tintTicks == 0) freeActor(actor); // exe 0x5e1490
        return;
    }
    default: return;
    }
}
void rockTick(AnimatedActor& actor) {
    RockFields& rock = classState<RockFields>(actor);
    switch (RockState(actor.state)) { // [0x8984f4]
    case RockState::Bind: rockBind(actor, rock); break;
    case RockState::Fall: rockFall(actor, rock); break;
    case RockState::Landed: rockLand(actor, rock); break;
    default: notPortedAt(kStoryPort, "0x50ee90 state past [0x8984f4]", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim != actor.prevAnim) animSetPose(actor, actor.anim, actor.animKey.frame); // exe 0x5e1c30
    else animStepPose(actor);                                                             // exe 0x5e1e80
}

// ---- the ray charges (static types 0x5b / 0x60) ---------------------------------------------------------------------
constexpr uint8_t kBossReportBeam = 2, kBossReportBlocked = 3;


// the 32 rays: angle (rng & 0x7f) + i * 0x80; length (rng & lm) + la; spin (rng & sm) + sa
void raysRandom(ChargeFields& charge, uint32_t lm, uint32_t la, uint32_t sm, uint32_t sa) {
    for (uint32_t i = 0; i < kChargeRays; ++i) {
        charge.angles[i] = uint16_t(((rngStep() & 0x7f) + (i << 7)) & 0xfff);
        charge.lengths[i] = uint8_t((rngStep() & lm) + la);
        charge.spins[i] = int8_t((rngStep() & sm) + sa);
    }
}
// exe 0x5bc380(actor, block): the midpoint of the boss's bones 3 and 6
void midBones(const ChargeFields& charge, int16_t out[3]) {
    const AnimatedActor& boss = *charge.boss;
    const ShortVec4& p3 = bossBone(boss, 3);
    const ShortVec4& p6 = bossBone(boss, 6);
    out[0] = t16((int(p6.x) + int(p3.x)) >> 1);
    out[1] = t16((int(p6.y) + int(p3.y)) >> 1);
    out[2] = t16((int(p6.z) + int(p3.z)) >> 1);
}
// exe 0x5bf320(actor, block): the boss's bone 7 at the local (0, 0x40, -0xc0) (exe 0x5e1970)
void bone7(const ChargeFields& charge, int16_t out[3]) { bonePointAt(*charge.boss, 7, 0, 0x40, -0xc0, out); }
void placeAt(StaticActor& actor, const int16_t p[3]) {
    actor.pos.x = int32_t(uint32_t(int32_t(p[0])) << 16);
    actor.pos.y = int32_t(uint32_t(int32_t(p[1])) << 16);
    actor.pos.z = int32_t(uint32_t(int32_t(p[2])) << 16);
}

// ---- attack 10's charge (static type 0x5b, exe 0x5bb6e0) ------------------------------------------------------------
enum class BallChargeState : uint8_t { Start = 0, Charge = 1, Burst = 2 };
// exe 0x5bba80 (variant 0): rings on the screen at the bones' midpoint: per ray two (+ a third while the timer < 0x10)
// at angle / length, then two at the centre
void drawBallChargeRings(const StaticActor& actor, const ChargeFields& charge) {
    int16_t p[3], sxy[2], depth = 0;
    midBones(charge, p);
    int w = 0;
    if (!project(p, 0x280, sxy, w, depth)) return;
    const uint32_t dz = depthWord(depth, 8);
    const int b0 = charge.timer;
    const uint32_t g = uint32_t(0x100 - (b0 << 3));
    const uint32_t hdr = (g >> 3) | 0xf4000000u, q = g >> 2;
    for (uint32_t i = 0; i < kChargeRays; ++i) {
        const int32_t len = mul(mul(charge.lengths[i], b0 + 7), w) >> 13;
        const uint32_t xy = rayPoint(sxy, charge.angles[i] & 0xfff, len);
        const int32_t sv = mul(charge.spins[i], w);
        ringRecord(actor.at, 0x4000, col3(q), xy, 0x42000000, dz | 0x30000u, uint32_t(sv >> 10) << 16);
        ringRecord(actor.at, 0x4000, hdr, xy, dc::kSemiAdditive, dz | 0x30000u, uint32_t(sv >> 8) << 16);
        if (charge.timer < 0x10) {
            const uint32_t t = uint32_t(0x100 - (int(charge.timer) << 4));
            ringRecord(actor.at, 0x4000, col3(t), xy, dc::kSemiAdditive, dz | 0x30000u, uint32_t(sv >> 10) << 16);
        }
    }
    const uint32_t sw = sxyWord(sxy);
    ringRecord(actor.at, 0x4000, ((g | 0xfff40000u) << 8) | g, sw, 0x42000000, dz | 0x20000u, uint32_t(w) << 16);
    ringRecord(actor.at, 0x4000, col3(g), sw, 0x42000000, dz | 0x20000u, uint32_t(w >> 1) << 16);
}
// exe 0x5bbd30 (variant 1): DR_MODE 0xe1000240 (abr 2) + 32 OT POLY_G3 rays around the projected midpoint S (width
// 0x400 -> w; one chain at OT[depth - 8, at least 1]): per ray the angle += spin * 2 (16-bit), h = (0xa0 - length) * 3
// >> 2, len = (0x28 - timer) * length * w >> 12; G3 0x32 {grey (0x100 - timer * 8) >> 2, S} {black, S + (cos, sin)
// (angle - h) * len >> 12} {black, S + (cos, sin)(angle + h) * len >> 12} (dc::otTriG3 via screenG3)
void drawBallChargeRays(const StaticActor& actor, ChargeFields& charge) {
    int16_t p[3], sxy[2], depth = 0;
    midBones(charge, p);
    int w = 0;
    if (!project(p, 0x400, sxy, w, depth)) return;
    const int32_t timer = charge.timer;
    const uint32_t g = uint32_t((0x100 - timer * 8) >> 2) & 0xff;
    const uint32_t c[3] = {0x32000000u | g << 16 | g << 8 | g, 0, 0};
    for (uint32_t i = 0; i < kChargeRays; ++i) {
        charge.angles[i] = uint16_t(charge.angles[i] + (uint32_t(int32_t(charge.spins[i])) << 1));
        const int ang = int(charge.angles[i] & 0xfff), r = charge.lengths[i];
        const int hw = ((0xa0 - r) * 3) >> 2;
        const int32_t len = mul(mul(0x28 - timer, r), w) >> 12;
        const int a0 = (ang - hw) & 0xfff, a1 = (ang + hw) & 0xfff;
        const int px[3][2] = {{0, 0},
                              {t16(mul(fixedCos(int(uint32_t(a0))), len) >> 12),
                               t16(mul(fixedSin(int(uint32_t(a0))), len) >> 12)},
                              {t16(mul(fixedCos(int(uint32_t(a1))), len) >> 12),
                               t16(mul(fixedSin(int(uint32_t(a1))), len) >> 12)}};
        screenG3(actor.at, p, 0x400, w, c, 2, px);
    }
}
// exe 0x5bbf40: six rings at the actor (shrinking colours with the timer), then 32 OT POLY_G4 rays (DR_MODE 0xe1000240:
// abr 2) from the projected point, outer corners black, inner (len >> 2) grey (timer * 8 + 8)
void drawBallChargeBurst(const StaticActor& actor, const ChargeFields& charge) {
    const int b0 = charge.timer;
    const uint32_t xy = xyWordOf(actor), zw = uint32_t(uint16_t(actor.pos.z.whole)) | 0x20000u;
    const uint32_t c = (uint32_t(b0) << 5) - 1u;
    ringRecord(actor.at, 0x4000, col3f0(c), xy, 0x42000000, zw, 0xe00000);
    ringRecord(actor.at, 0x4000, ((c | 0xfff00000u) << 8) | c, xy, 0x42000000, zw, 0x1400000);
    const uint32_t c2 = c * 2, col2 = ((c2 | 0xfff00000u) << 8) | c2;
    const uint32_t d6 = uint32_t(6 - b0) << 6;
    ringRecord(actor.at, 0x4000, col2, xy, 0x42000000, zw, (uint32_t(int32_t(d6) >> 1) << 16) | d6);
    ringRecord(actor.at, 0x4000, col2, xy, 0x42000000, zw, ((d6 * 3) << 16) | d6);
    const uint32_t c5 = c >> 1, col5 = col3f0(c5);
    const uint32_t d7 = uint32_t(5 - b0) << 7;
    ringRecord(actor.at, 0x4000, col5, xy, 0x42000000, zw, (uint32_t(int32_t(d7 * 3) >> 2) << 16) | d7);
    ringRecord(actor.at, 0x4000, col5, xy, 0x42000000, zw, (uint32_t(int32_t(d7 * 5) >> 2) << 16) | d7);
    const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    int16_t sxy[2], depth = 0;
    int w = 0;
    const int L = (7 - b0) << 8;
    if (!project(p, L, sxy, w, depth)) return;
    const uint32_t g = uint32_t(b0 * 8 + 8), col = (((g << 8) | g) << 8) | g;
    const uint32_t cw[4] = {0x3a000000u, 0x3a000000u, col, col};
    for (uint32_t i = 0; i < kChargeRays; ++i) {
        const int r = charge.lengths[i], ang = int16_t(charge.angles[i]);
        const int hw = ((0xa0 - r) * 3) >> 2;
        const int32_t len = mul(mul(0x28 - b0, r), w) >> 12;
        int px[4][2];
        rayPx((ang - hw) & 0xfff, (ang + hw) & 0xfff, len, len >> 2, px);
        screenG4(actor.at, p, L, w, cw, 2, px);
    }
}
void drawBallCharge(const StaticActor& actor, ChargeFields& charge) { // the variant's state-1 draw
    if (actor.variant == 0) drawBallChargeRings(actor, charge);
    else drawBallChargeRays(actor, charge);
}
// exe 0x5bb700: the timer 0x1f, the rays (variant 0: lengths 0x40.., spins 0x10..; else 0x20.., -0x40..), drawn, state 1
void ballChargeStart(StaticActor& actor, ChargeFields& charge) {
    pointAtWord(charge.boss, charge.spawnBoss);
    charge.timer = 0x1f;
    charge.word = 1;
    if (actor.variant == 0) raysRandom(charge, 0x7f, 0x40, 0x3f, 0x10);
    else raysRandom(charge, 0x7f, 0x20, 0x7f, 0xc0);
    drawBallCharge(actor, charge);
    ++actor.state;
}
// exe 0x5bb8c0: the boss's report cleared -> freed; the timer; at 0 variant 1 freed, variant 0 placed between the boss's
// bones 3 / 6 with new rays, 4 ticks of 0x5bbf40 -> 2
void ballChargeRun(StaticActor& actor, ChargeFields& charge) {
    if (charge.boss->stepCounter == 0) {
        freeActor(actor); // exe 0x5e1540
        return;
    }
    charge.timer = int16_t(uint16_t(charge.timer) - 1u);
    if (charge.timer != 0) {
        drawBallCharge(actor, charge);
        return;
    }
    if (actor.variant != 0) {
        freeActor(actor);
        return;
    }
    int16_t p[3];
    midBones(charge, p);
    placeAt(actor, p);
    raysRandom(charge, 0x7f, 0x20, 0x7f, 0xc0);
    charge.timer = 4;
    drawBallChargeBurst(actor, charge);
    ++actor.state;
}
// exe 0x5bba40: 0x5bbf40 until the timer runs out or the boss's report is cleared, then freed
void ballChargeBurst(StaticActor& actor, ChargeFields& charge) {
    charge.timer = int16_t(uint16_t(charge.timer) - 1u);
    if (charge.timer != 0 && charge.boss->stepCounter != 0) {
        drawBallChargeBurst(actor, charge);
        return;
    }
    freeActor(actor); // exe 0x5e1540
}
void ballCharge(StaticActor& actor) {
    ChargeFields& charge = classState<ChargeFields>(actor);
    switch (BallChargeState(actor.state)) { // [0x8d0c8c]
    case BallChargeState::Start: ballChargeStart(actor, charge); return;
    case BallChargeState::Charge: ballChargeRun(actor, charge); return;
    case BallChargeState::Burst: ballChargeBurst(actor, charge); return;
    default:
        notPortedAt(kStoryPort,
                    "0x5bb6e0 state (3..5 = 0x5bc420 / 0x5bc560 / 0x5bc9c0 have no spawner of type 0x5b; past 5 "
                    "[0x8d0c8c])",
                    actor.state);
    }
}

// ---- attack 7's charge (static type 0x60, exe 0x5be190) -------------------------------------------------------------
enum class BeamChargeState : uint8_t { Start = 0, Charge = 1, Burst = 2, Glow = 3, Fade = 4 };
constexpr uint16_t kBeamChargeSound = 0x2cc;
// exe 0x5be5e0 (variant 0): rings on the screen at the boss's bone 7: per ray one (+ one while the timer < 0x20), then
// two at the centre
void drawBeamChargeRings(const StaticActor& actor, const ChargeFields& charge) {
    int16_t p[3], sxy[2], depth = 0;
    bone7(charge, p);
    int w = 0;
    if (!project(p, 0x280, sxy, w, depth)) return;
    const uint32_t dz = depthWord(depth, 0x20);
    const int b0 = charge.timer;
    const uint32_t g = uint32_t(0xc0 - (b0 << 2)), h = g >> 3;
    for (uint32_t i = 0; i < kChargeRays; ++i) {
        const int32_t len = mul(mul(mul(charge.lengths[i], b0 + 8), w), 3) >> 15;
        const uint32_t xy = rayPoint(sxy, charge.angles[i] & 0xfff, len);
        const int32_t sv = mul(charge.spins[i], w);
        ringRecord(actor.at, 0x4000, ((h | 0xfff40000u) << 8) | h, xy, dc::kSemiAdditive, dz | 0x30000u,
                   uint32_t(sv >> 7) << 16);
        if (charge.timer < 0x20) {
            const uint32_t t = uint32_t(0x80 - (int(charge.timer) << 2));
            ringRecord(actor.at, 0x4000, col3(t), xy, dc::kSemiAdditive, dz | 0x40000u, uint32_t(sv >> 9) << 16);
        }
    }
    const uint32_t sw = sxyWord(sxy), c = ((g | 0xfffff400u) << 8 | g) << 8;
    ringRecord(actor.at, 0x4000, c, sw, 0x42000000, dz | 0x30000u, uint32_t(w) << 16);
    ringRecord(actor.at, 0x4000, c | g, sw, 0x42000000, dz | 0x30000u, uint32_t(mul(w, 3) >> 2) << 16);
}
// exe 0x5be840 (variant 1): within the view depth (0x30 .. the effects' far depth * 4) of the boss's bone 7 (the point
// as given to the camera, unshifted), the lengths count down (new (rng & 0xf) + 0x10 at 0) and every ray is two pairs of
// OT POLY_G4 (DR_MODE 0xe1000220: abr 1) in the view plane: corners at the ray's angle +- 8 / 0x10 at length * spin,
// the inner / outer point (length -+ length >> 2 / >> 1); colours black, 0x80808 / 0x10, 0x404040 / 0xff, black
void drawBeamChargeRays(const StaticActor& actor, ChargeFields& charge) {
    int16_t p[3];
    bone7(charge, p);
    const CameraPoint cpt = cameraPoint(p); // 0x40c3c0 / 0x40c3e0 / 0x40c6a0 / 0x40c800 / 0x40caa0
    if (cpt.view[2] > int32_t(uint32_t(stage().effectsFar) * 4) || cpt.view[2] < 0x30) return;
    const int s = int((0xcu - shiftByte()) & 31);
    const int32_t vx = cpt.view[0], vy = cpt.view[1];
    // the corner word t16(d + view) brought back to an offset from the view point (camera-input units)
    auto off = [](int32_t d, int32_t v) { return float(t16(int(t16(int64_t(d) + v)) - int64_t(v))); };
    for (uint32_t i = 0; i < kChargeRays; ++i) {
        charge.lengths[i] -= 1u;
        if (charge.lengths[i] == 0) charge.lengths[i] = uint8_t((rngStep() & 0xf) + 0x10u);
        const int ang = int16_t(charge.angles[i]), am = ang & 0xfff;
        for (int k = 0; k < 2; ++k) {
            const int aL = (ang - (k ? 0x10 : 8)) & 0xfff, aR = (ang + (k ? 0x10 : 8)) & 0xfff;
            const uint32_t colA = k ? 0x10u : 0x80808u, colB = k ? 0xffu : 0x404040u;
            const int32_t L = mul(charge.lengths[i], charge.spins[i]);
            const int32_t q = L >> (2 - k);
            const int32_t lOut = q + L, lIn = L - q;
            auto corner = [&](int an, int32_t len, float o[2]) {
                o[0] = off(mul(fixedCos(int(uint32_t(an))), len) >> s, vx);
                o[1] = off(mul(fixedSin(int(uint32_t(an))), len) >> s, vy);
            };
            float c0[2], c1[2], c2[2], c3[2], c4[2];
            corner(aL, L, c0);
            corner(am, L, c1);
            corner(am, lIn, c2);
            corner(aR, L, c3);
            corner(am, lOut, c4);
            const uint32_t cw[4] = {0x3a000000u, colA, colB, 0x3a000000u};
            int16_t v[4][3];
            for (auto& vv : v)
                for (int j = 0; j < 3; ++j) vv[j] = p[j];
            const float q1[4][2] = {{c0[0], c0[1]}, {c2[0], c2[1]}, {c1[0], c1[1]}, {c3[0], c3[1]}};
            dc::otQuadG4(actor.at, cw, 1, v, q1);
            const float q2[4][2] = {{c0[0], c0[1]}, {c4[0], c4[1]}, {c1[0], c1[1]}, {c3[0], c3[1]}};
            dc::otQuadG4(actor.at, cw, 1, v, q2);
        }
    }
}
// exe 0x5bed10: five screen rings at the bone (sizes by (5 - timer) * w), then 32 OT POLY_G4 rays (abr 2) as 0x5bbf40
// (length (8 - timer) * len * w >> 8, inner >> 3, inner colour timer * 0x10 in bytes 1 / 2)
void drawBeamChargeBurst(const StaticActor& actor, const ChargeFields& charge) {
    int16_t p[3], sxy[2], depth = 0;
    bone7(charge, p);
    int w = 0;
    if (!project(p, 0x100, sxy, w, depth)) return;
    const uint32_t dz = uint16_t(depth), sw = sxyWord(sxy);
    const int b0 = charge.timer;
    const uint32_t e16 = uint32_t(b0) << 4;
    const int32_t c = mul(5 - b0, w) >> 1;
    ringRecord(actor.at, 0x4000, 0xf4ffffff, sw, dc::kSemiAdditive, dz | 0x20000u, uint32_t(mul(c, 3) >> 1) << 16);
    ringRecord(actor.at, 0x4000, 0xf400ffff, sw, dc::kSemiAdditive, dz | 0x20000u,
               uint32_t(int32_t(uint32_t(mul(c, 5)) << 6) >> 8) << 16);
    ringRecord(actor.at, 0x4000, 0xf4ffff00, sw, 0x42000000, dz | 0x20000u,
               uint32_t(int32_t(uint32_t(c) << 9) >> 8) << 16);
    const uint32_t col4 = (((e16 | 0xfffff400u) << 8) | e16) << 8;
    const int32_t sv = int32_t(uint32_t(c) << 10) >> 8, cc = int32_t(uint32_t(c) << 8) >> 8;
    ringRecord(actor.at, 0x4000, col4, sw, 0x42000000, dz | 0x10000u,
               ((((uint32_t(cc) << 16) - uint32_t(cc) + uint32_t(sv)) << 16) | uint32_t(sv)));
    ringRecord(actor.at, 0x4000, col4, sw, 0x42000000, dz | 0x10000u,
               ((uint32_t(cc) + uint32_t(sv)) << 16) | uint32_t(sv));
    const int L = (7 - b0) << 8;
    const int w2 = projectedWidth(p, int16_t(L), &depth); // exe 0x5f2f20
    if (w2 == 0) return;
    const uint32_t col = ((e16 << 8) | e16) << 8;
    const uint32_t cw[4] = {0x3a000000u, 0x3a000000u, col, col};
    for (uint32_t i = 0; i < kChargeRays; ++i) {
        const int r = charge.lengths[i], ang = int16_t(charge.angles[i]);
        const int hw = ((0xa0 - r) * 3) >> 2;
        const int32_t len = mul(mul(8 - b0, r), w2) >> 8;
        int px[4][2];
        rayPx((ang - hw) & 0xfff, (ang + hw) & 0xfff, len, len >> 3, px);
        screenG4(actor.at, p, L, w2, cw, 2, px);
    }
}
// exe 0x5bf110: five screen rings at the bone, three of them with random sizes
void drawBeamChargeGlow(const StaticActor& actor, const ChargeFields& charge) {
    int16_t p[3], sxy[2], depth = 0;
    bone7(charge, p);
    int w = 0;
    if (!project(p, (int(charge.timer) + 8) << 4, sxy, w, depth)) return;
    const uint32_t dz = uint16_t(depth), sw = sxyWord(sxy);
    ringRecord(actor.at, 0x4000, 0xf4ffffff, sw, dc::kSemiAdditive, dz | 0x20000u, uint32_t(mul(w, 3) >> 1) << 16);
    const uint32_t r1 = (rngStep() & 0x1f) + 0x140;
    ringRecord(actor.at, 0x4000, 0xf400ffff, sw, dc::kSemiAdditive, dz | 0x20000u, ((r1 * uint32_t(w)) >> 8) << 16);
    const uint32_t r2 = (rngStep() & 0x1f) + 0x200;
    ringRecord(actor.at, 0x4000, 0xf4ffff00, sw, 0x42000000, dz | 0x20000u, ((r2 * uint32_t(w)) >> 8) << 16);
    const uint32_t r3 = (rngStep() & 7) + 0x300, e = (r3 * uint32_t(w)) >> 8;
    ringRecord(actor.at, 0x4000, 0xf4101000, sw, dc::kSemiAdditive, dz | 0x10000u, ((e + 0xfffe) << 16) | e);
    ringRecord(actor.at, 0x4000, 0xf4101000, sw, dc::kSemiAdditive, dz | 0x10000u, ((e + 2) << 16) | e);
}
void drawBeamCharge(const StaticActor& actor, ChargeFields& charge) {
    if (actor.variant == 0) drawBeamChargeRings(actor, charge);
    else drawBeamChargeRays(actor, charge);
}
// exe 0x5be1b0: the timer 0x2f, the rays (variant 0: lengths 0x40.., spins 0x10.., sound 0x2cc; else 0x10.., 0x40..)
void beamChargeStart(StaticActor& actor, ChargeFields& charge) {
    pointAtWord(charge.boss, charge.spawnBoss);
    charge.timer = 0x2f;
    charge.word = 0;
    if (actor.variant == 0) {
        raysRandom(charge, 0x7f, 0x40, 0x3f, 0x10);
        drawBeamChargeRings(actor, charge);
        soundPlayAtPos(kBeamChargeSound, actor.pos);
    } else {
        raysRandom(charge, 0xf, 0x10, 0x7f, 0x40);
        drawBeamChargeRays(actor, charge);
    }
    ++actor.state;
}
// exe 0x5be370: the boss's report cleared -> freed; the timer; at 0 variant 1 freed, variant 0: the beam, the boss's
// report = 2 (when still set), at bone 7 with new rays, 4 ticks of 0x5bed10 -> 2
void beamChargeRun(StaticActor& actor, ChargeFields& charge) {
    if (charge.boss->stepCounter == 0) {
        freeActor(actor); // exe 0x5e1540
        return;
    }
    charge.timer = int16_t(uint16_t(charge.timer) - 1u);
    if (charge.timer != 0) {
        drawBeamCharge(actor, charge);
        return;
    }
    if (actor.variant != 0) {
        freeActor(actor);
        return;
    }
    if (const uint32_t slot = allocActor40(kActorStageClass)) { // exe 0x5e1280
        ProjectileActor& beam = projectileActorAt(slot);
        beam.flags = kActorLive;
        beam.loopFlags |= kActorSweepGroup2;
        beam.type = kBeamType;
        beam.subtype = 0;
        beam.variant = 0;
        beam.setParamsWord(slotIdentityWord(charge.boss));
    }
    if (charge.boss->stepCounter != 0) charge.boss->stepCounter = kBossReportBeam;
    int16_t p[3];
    bone7(charge, p);
    placeAt(actor, p);
    raysRandom(charge, 0x7f, 0x20, 0x7f, 0xc0);
    charge.timer = 4;
    drawBeamChargeBurst(actor, charge);
    ++actor.state;
}
// exe 0x5be520: 0x5bed10 until the timer runs out (then 8 ticks of 0x5bf110 -> 3); the boss's report cleared -> freed
void beamChargeBurst(StaticActor& actor, ChargeFields& charge) {
    if (charge.boss->stepCounter == 0) {
        freeActor(actor); // exe 0x5e1540
        return;
    }
    charge.timer = int16_t(uint16_t(charge.timer) - 1u);
    if (charge.timer != 0) {
        drawBeamChargeBurst(actor, charge);
        return;
    }
    charge.timer = 8;
    drawBeamChargeGlow(actor, charge);
    ++actor.state;
}
// exe 0x5be580: 0x5bf110; the boss's report 0 or 3 -> 4
void beamChargeGlow(StaticActor& actor, ChargeFields& charge) {
    const uint8_t v = charge.boss->stepCounter;
    if (v == 0 || v == kBossReportBlocked) ++actor.state;
    drawBeamChargeGlow(actor, charge);
}
// exe 0x5be5b0: 0x5bf110 for the timer's ticks, then freed
void beamChargeFade(StaticActor& actor, ChargeFields& charge) {
    charge.timer = int16_t(uint16_t(charge.timer) - 1u);
    if (charge.timer != 0) {
        drawBeamChargeGlow(actor, charge);
        return;
    }
    freeActor(actor); // exe 0x5e1540
}
void beamCharge(StaticActor& actor) {
    ChargeFields& charge = classState<ChargeFields>(actor);
    switch (BeamChargeState(actor.state)) { // [0x8d0dc8]
    case BeamChargeState::Start: beamChargeStart(actor, charge); return;
    case BeamChargeState::Charge: beamChargeRun(actor, charge); return;
    case BeamChargeState::Burst: beamChargeBurst(actor, charge); return;
    case BeamChargeState::Glow: beamChargeGlow(actor, charge); return;
    case BeamChargeState::Fade: beamChargeFade(actor, charge); return;
    default:
        notPortedAt(kStoryPort, "0x5be190 state (5 / 6 belong to the class 0x5bf360; past 6 [0x8d0dc8])", actor.state);
    }
}

// ---- the blades of attack 6 (static type 0x66, exe 0x5c16b0) --------------------------------------------------------
enum class BladeState : uint8_t { Start = 0, Grow = 1, Spin = 2 };
constexpr uint32_t kBladeEntry = hitGroup(3) | hitCapsule(3);
// the discs' angles (exe data, 7 words)
const DataTable<ScalarTable<uint16_t, 7>> bladeDiscAngles{TableId::St5cFxBladeDiscAngles};


// exe 0x5c1fb0: at the boss's bone, height high, radius out around the boss by the yaw difference yaw - the boss's
void bladeAtBone(StaticActor& actor, const BladeFields& blade) {
    const AnimatedActor& boss = *blade.boss;
    const uint32_t yd = (uint16_t(actor.rotation.y) - uint16_t(boss.rotation.y)) & 0xfffu;
    const ShortVec4& bp = bossBone(boss, uint32_t(int32_t(blade.bone)));
    const int r = blade.radius;
    actor.pos.x = int32_t(((uint32_t(mul(fixedCos(int(yd)), r)) & 0xfffff000u) + (uint32_t(int32_t(bp.x)) << 12)) << 4);
    actor.pos.y = int32_t(uint32_t(int32_t(bp.y) + int32_t(blade.height)) << 16);
    actor.pos.z = int32_t(((uint32_t(mul(fixedSin(int(yd)), r)) & 0xfffff000u) + (uint32_t(int32_t(bp.z)) << 12)) << 4);
}
uint32_t bladeXyWord(const StaticActor& actor) {
    return uint32_t(uint16_t(actor.pos.x.whole)) | uint32_t(int32_t(actor.pos.y.whole)) << 16;
}
// exe 0x5c1930: four rings at the blade growing as the timer runs (white ones, then two black-centred)
void bladeGrowRings(const StaticActor& actor, const BladeFields& blade) {
    const int b0 = blade.timer;
    const uint32_t xy = bladeXyWord(actor), z = uint16_t(actor.pos.z.whole);
    const uint32_t d = uint32_t(5 - b0) << 4;
    ringRecord(actor.at, 0x4000, 0xf0ffffff, xy, dc::kSemiAdditive, z | 0x20000u, d << 17);
    ringRecord(actor.at, 0x4000, 0xf0ffffff, xy, dc::kSemiAdditive, z | 0x30000u, d << 16);
    const uint32_t e = uint32_t(0x47 - b0 * 8), w3 = (((e | 0x880u) << 10) | e) << 8;
    const uint32_t r = uint32_t(b0 + 4) << 4;
    ringRecord(actor.at, 0x4000, dc::kRingOpcode, xy, w3, z | 0x20000u, (r << 16) | (r + 8));
    ringRecord(actor.at, 0x4000, dc::kRingOpcode, xy, w3, z | 0x20000u, (r - 8) | (r << 16));
}
// exe 0x5c1f30: two rings where the blade met a wall (the record's xy word carries only the point's x: y 0)
void bladeWallRings(const StaticActor& actor, const int32_t pt[3]) {
    ringRecord(actor.at, 0x4000, 0xf0ffffff, uint16_t(wholeOf(pt[0])), dc::kSemiAdditive,
               uint32_t(uint16_t(wholeOf(pt[2]))) | 0x20000u, 0x400000);
    ringRecord(actor.at, 0x4000, 0xf0ffffff, uint16_t(wholeOf(pt[0])), dc::kSemiAdditive,
               uint32_t(uint16_t(wholeOf(pt[2]))) | 0x30000u, 0x200000);
}
// exe 0x5c1a80: two rings at the blade, then per segment c < segments (0x80 long along the local z, frame = the
// rotation (pitch, the boss's yaw - yaw - 0x400, roll) (exe 0x40c070) at the blade's point) three OT POLY_G4 fins
// (DR_MODE 0xe1000220: abr 1) from the axis to (cos, sin) * (timer + 4) >> (0xc - shift) at angles phase * 0x111 + k *
// 0x555 (phase = (frame << 6) % 5), cyan at the axis, black outside; and six OT POLY_G3 discs (radius timer * 4 + 8,
// the disc angles; dc::otTriG3). Port: the corners as camera-input points: the local point through the rotation (exe
// 0x40bb30) + the blade's point (the exe composes the camera with the rotation first, 0x40bc90 / 0x40bab0: rounding may
// differ by a unit)
void bladeDraw(const StaticActor& actor, const BladeFields& blade) {
    const uint32_t xy = bladeXyWord(actor), z = uint16_t(actor.pos.z.whole);
    const int b0 = blade.timer;
    const uint32_t frameByte = uint8_t(gameState().frame);
    ringRecord(actor.at, ((frameByte & 0xf) << 0x12) | 0x4000u, 0xf0ffffff, xy, dc::kSemiAdditive, z | 0x30000u,
               (uint32_t(b0) << 3) << 16);
    ringRecord(actor.at, ((frameByte & 0xf) << 0x12) | 0x4000u, 0xf0ffffff, xy, dc::kSemiAdditive, z | 0x30000u,
               (uint32_t(b0) << 2) << 16);
    const AnimatedActor& boss = *blade.boss;
    const Matrix3 frame = rotationZYX(actor.rotation.x,
                                      int16_t(uint16_t(boss.rotation.y) - uint16_t(actor.rotation.y) - 0x400u),
                                      actor.rotation.z); // exe 0x40c070
    const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    auto world = [&](int x, int y, int zz, int16_t o[3]) {
        const int16_t in[3] = {int16_t(x), int16_t(y), int16_t(zz)};
        int16_t out[3];
        transformShortVector(frame, in, out); // exe 0x40bb30
        for (uint32_t k = 0; k < 3; ++k) o[k] = t16(int(out[k]) + pos[k]);
    };
    const int s = int((0xcu - shiftByte()) & 31);
    const int32_t r4 = b0 + 4;
    static const float kNoOff[4][2] = {};
    const uint32_t cw[4] = {0x3affff00u, 0x3affff00u, 0, 0};
    for (int c = 0; c < blade.segments; ++c) {
        const int z0 = c << 7, z1 = (c + 1) << 7;
        const int phase = int((uint32_t(gameState().frame) << 6) % 5);
        uint32_t an = uint32_t(phase) * 0x111;
        for (int k = 0; k < 3; ++k, an += 0x555) {
            const int dx = t16(mul(fixedCos(int(an & 0xfff)), r4) >> s), dy = t16(mul(fixedSin(int(an & 0xfff)), r4) >> s);
            int16_t v[4][3];
            world(0, 0, z0, v[0]);
            world(0, 0, z1, v[1]);
            world(dx, dy, z0, v[2]);
            world(dx, dy, z1, v[3]);
            dc::otQuadG4(actor.at, cw, 1, v, kNoOff);
        }
        // six POLY_G3 0x32 discs at zc = ((frame & 3) + c * 4) << 5 (same DR_MODE chain): {black, (cos, sin)(A) *
        // (timer * 4 + 8) >> (0xc - shift), zc} {black, the same at B} {0xff3000, (0, 0, zc)}, (A, B) = consecutive
        // disc angles (& 0xfff)
        const int zc = int(((frameByte & 3) + uint32_t(c) * 4) << 5);
        const int32_t rd = r4 * 4 - 8; // timer * 4 + 8
        static const float kNoOff3[3][2] = {};
        const uint32_t cg[3] = {0x32000000u, 0x32000000u, 0xff3000u};
        for (uint32_t t = 0; t < 6; ++t) {
            const uint32_t A = bladeDiscAngles[t] & 0xfff, B = bladeDiscAngles[t + 1] & 0xfff;
            int16_t v[3][3];
            world(t16(mul(fixedCos(int(A)), rd) >> s), t16(mul(fixedSin(int(A)), rd) >> s), zc, v[0]);
            world(t16(mul(fixedCos(int(B)), rd) >> s), t16(mul(fixedSin(int(B)), rd) >> s), zc, v[1]);
            world(0, 0, zc, v[2]);
            dc::otTriG3(actor.at, cg, 1, v, kNoOff3);
        }
    }
}
// exe 0x5c16d0: the timer 4, the boss, the bone / height / radius from its position's words, the box 0x8d0e60
void bladeStart(StaticActor& actor, BladeFields& blade) {
    blade.timer = 4;
    pointAtWord(blade.boss, blade.spawnBoss);
    blade.segments = 0;
    blade.bone = actor.pos.x.whole;
    blade.height = actor.pos.y.whole;
    blade.radius = actor.pos.z.whole;
    actor.collisionBox.pointAt(TableId::St5cFxBladeBox);
    actor.actorHit = 0;
    blade.hitBox.result = 0;
    ++actor.state;
}
// exe 0x5c1720: at the bone, the growing rings; 4 ticks -> 2 (timer 8)
void bladeGrow(StaticActor& actor, BladeFields& blade) {
    bladeAtBone(actor, blade);
    bladeGrowRings(actor, blade);
    blade.timer = int16_t(uint16_t(blade.timer) - 1u);
    if (blade.timer == 0) {
        blade.timer = 8;
        ++actor.state;
    }
}
// exe 0x5c1760: up to 7 steps of 0x80 along the rotation (exe 0x5e3970) from the blade's point until the move test (exe
// 0x40f5a0) fails (rings there); the segments = the steps + 1; drawn; a capsule (radius 8) from the blade's point to
// the last step while the boss's report is set, else the timer runs out and the blade is freed
void bladeSpin(const PlayerEnv& env, StaticActor& actor, BladeFields& blade) {
    bladeAtBone(actor, blade);
    const AnimatedActor& boss = *blade.boss;
    const int16_t rot[3] = {actor.rotation.x, t16(int(boss.rotation.y) - int(actor.rotation.y) - 0x400),
                            actor.rotation.z};
    int32_t d[3];
    rotateVector(rot, 0, 0, 0x800, d);
    const uint32_t mask = collisionLayerMask(actor.slot); // exe 0x413240
    int32_t tip[3];                                        // the exe's local 16.16 tip
    positionOf(actor, tip);
    const int16_t i1[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    uint32_t n = 0;
    for (;;) {
        for (uint32_t k = 0; k < 3; ++k) tip[k] = int32_t(uint32_t(tip[k]) + uint32_t(d[k]));
        const int16_t i0[3] = {wholeOf(tip[0]), wholeOf(tip[1]), wholeOf(tip[2])};
        if (int32_t(testMoveBlocked(env, i0, i1, actor.collisionBox, mask)) < 0) {
            bladeWallRings(actor, tip);
            break;
        }
        if (++n >= 7) break;
    }
    blade.segments = int16_t(n + 1);
    bladeDraw(actor, blade);
    if (blade.boss->stepCounter != 0) {
        const int16_t q[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
        const int16_t p[3] = {wholeOf(tip[0]), wholeOf(tip[1]), wholeOf(tip[2])};
        const AnimatedActorStats row = bossStats(*blade.boss);
        hitListSubmitCapsule(q, 8, p, 8, blade.hitBox, hitOwner(actor.slot) | kBladeEntry,
                             uint32_t(row.shotAttack) | kHitAttackStrong); // exe 0x5e4190
        return;
    }
    blade.timer = int16_t(uint16_t(blade.timer) - 1u);
    if (blade.timer == 0) freeActor(actor); // exe 0x5e1540
}
void bladeTick(const PlayerEnv& env, StaticActor& actor) {
    BladeFields& blade = classState<BladeFields>(actor);
    switch (BladeState(actor.state)) { // [0x8d0e7c]
    case BladeState::Start: bladeStart(actor, blade); return;
    case BladeState::Grow: bladeGrow(actor, blade); return;
    case BladeState::Spin: bladeSpin(env, actor, blade); return;
    default: notPortedAt(kStoryPort, "0x5c16b0 state past [0x8d0e7c]", actor.state);
    }
}

// ---- the stomp's shots (pool 0x40 type 0x32, exe 0x5d16d0) and the ball (type 0x33, exe 0x5d1a10) ------------------
enum class StompShotState : uint8_t { Start = 0, Run = 1 };
enum class BallState : uint8_t { Start = 0, Fly = 1, End = 2 };
constexpr uint32_t kStompShotEntry = hitGroup(3) | hitCapsule(3);
constexpr uint32_t kBallEntry = hitGroup(3) | hitRadius(0x20);
constexpr uint16_t kBallSound = 0x2d1, kBallBurstSound = 0x2d2;


// exe 0x5d1720: model variant 0 (none: 1 = freed), shown; at the boss's bone 3 (j != 0) / 6 on the floor (exe
// 0x5d19c0); the travel rotation (0, the player's angle + j * 0x100 + k * 0x70 - 0xf0, 0), the yaw random, the
// previous point 0xd00 ahead, speed -0x680, timer 0x80, blend (1, 1)
int stompShotStart(ProjectileActor& actor, StompShotFields& shot) {
    if (!bindActorModel(actor, 0)) return 1; // exe 0x5e0ba0
    actor.setDrawn(true);
    pointAtWord(shot.boss, shot.spawnBoss);
    const ShortVec4& bp = bossBone(*shot.boss, (actor.variant & 0xf0) ? 3u : 6u);
    actor.pos.x = int32_t(uint32_t(int32_t(bp.x)) << 16);
    actor.pos.y = 0;
    actor.pos.z = int32_t(uint32_t(int32_t(bp.z)) << 16);
    shot.travel.x = 0;
    const uint32_t v = actor.variant, j = v >> 4, k = v & 0xf;
    int32_t target[3];
    playerTarget(target);
    const int ang = angleOf(target[0], target[2], actor.pos.x, actor.pos.z); // exe 0x5e3bc0
    shot.travel.z = 0;
    shot.travel.y = int16_t(uint32_t(ang) + (j << 8) + k * 0x70 - 0xf0u);
    actor.rotation.x = 0;
    actor.rotation.y = int16_t(rngStep() & 0xfff);
    actor.rotation.z = 0;
    actor.velocity.x = 0;
    actor.velocity.y = 0;
    actor.prevPos = actor.pos;
    actor.velocity.z = int16_t(0xf980);
    moveAlongRotation(actor.prevPos, shot.travel, 0, 0, 0xd00); // exe 0x5e3810
    actor.collisionBox.pointAt(TableId::St5cFxStompShotBox);
    actor.actorHit = 0;
    shot.timer = 0x80;
    shot.word = 0;
    shot.hitBox.result = 0;
    actor.semiTransparency = semiTransparencyBits(1, 1); // exe 0x5e1a70(actor, 1, 1)
    ++actor.state;
    return 0;
}
// exe 0x5d1880: freed (1) on an actor hit, a bump, the timer or the player's shield; else spin, move -0x680 along the
// travel rotation, a capsule (radius 0x18) up 0x300
int stompShotRun(ProjectileActor& actor, StompShotFields& shot) {
    actor.savePreviousPose();
    if (actor.actorHit != 0 || (shot.hitBox.result & kHitAttackBump)) return 1;
    shot.timer -= 1u;
    if (shot.timer == 0) return 1;
    actor.rotation.y = int16_t(uint16_t(actor.rotation.y) + 0x80u);
    moveAlongRotation(actor.pos, shot.travel, 0, 0, actor.velocity.z); // exe 0x5e3810
    if (playerShieldBlocks(actor, 0x60)) return 1;                     // exe 0x5737b0
    const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const int16_t q[3] = {p[0], t16(int(p[1]) - 0x300), p[2]};
    shot.hitBox.result = 0;
    shot.hitBox.previousX = actor.prevPos.x.whole;
    shot.hitBox.previousZ = actor.prevPos.z.whole;
    const AnimatedActorStats row = bossStats(*shot.boss);
    hitListSubmitCapsule(p, 0x18, q, 0x18, shot.hitBox, hitOwner(actor.slot) | kStompShotEntry,
                         uint32_t(row.bodyAttack) | kHitAttackStrong); // exe 0x5e4190
    actor.airborne = kAirborneFalling;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    return 0;
}
void stompShot(const PlayerEnv& env, ProjectileActor& actor) {
    StompShotFields& shot = classState<StompShotFields>(actor);
    int r = 0;
    switch (StompShotState(actor.state)) { // [0x8d188c]
    case StompShotState::Start: r = stompShotStart(actor, shot); break;
    case StompShotState::Run: r = stompShotRun(actor, shot); break;
    default:
        notPortedAt(kStoryPort, "0x5d16d0 state (2..4 are the type-0x33 entries; past 4 [0x8d188c])", actor.state);
    }
    if (r == 1) {
        freeActor(actor); // exe 0x5e1500
        return;
    }
    if (actorGround(env, actor) > 0) freeActor(actor); // exe 0x40f190
}
// exe 0x5d1a30: three ball visuals (variants 0..2) on the ball; between the boss's bones 3 / 6, pitched / turned at the
// player (y - 0x80), speed 0x300, the previous point 0x600 ahead; timer 0x60; sound 0x2d1
void ballStart(ProjectileActor& actor, BallFields& ball) {
    for (uint32_t k = 0; k < 3; ++k) {
        const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (!slot) continue;
        StaticActor& fx = staticActorAt(slot);
        fx.flags = kActorLive;
        fx.type = kBallVisualsType;
        fx.subtype = 0;
        fx.variant = uint8_t(k);
        fx.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
        fx.setParamsWord(actor.at);
    }
    pointAtWord(ball.boss, ball.spawnBoss);
    int32_t target[3];
    playerTarget(target);
    const ShortVec4& p3 = bossBone(*ball.boss, 3);
    const ShortVec4& p6 = bossBone(*ball.boss, 6);
    actor.pos.x = int32_t(uint32_t(int32_t(p6.x) + int32_t(p3.x)) << 15);
    actor.pos.y = int32_t(uint32_t(int32_t(p6.y) + int32_t(p3.y)) << 15);
    actor.pos.z = int32_t(uint32_t(int32_t(p6.z) + int32_t(p3.z)) << 15);
    int32_t at[3];
    positionOf(actor, at);
    actor.rotation.x = int16_t(0x800u - uint32_t(pitchOf(target, at))); // exe 0x5e3ca0
    actor.rotation.y = int16_t(angleOf(target[0], target[2], at[0], at[2])); // exe 0x5e3bc0
    actor.rotation.z = 0;
    actor.velocity.x = 0;
    actor.velocity.y = 0;
    actor.prevPos = actor.pos;
    actor.velocity.z = 0x300;
    moveAlongRotation(actor.prevPos, actor.rotation, 0, 0, 0x600); // exe 0x5e3810
    ball.hitBox.result = 0;
    ball.timer = 0x60;
    ball.word = 0;
    soundPlayAtPos(kBallSound, actor.pos);
    ++actor.state;
}
// exe 0x5d1b70: along the rotation at -0x300; a point hit entry; the timer / below y 0 / a bump -> the explosion at the
// ball, the tint 0x4200ffff (render output), sound 0x2d2, 4 ticks -> 2; the boss dead -> freed
void ballFly(ProjectileActor& actor, BallFields& ball) {
    actor.savePreviousPose();
    moveAlongRotation(actor.pos, actor.rotation, 0, 0, -int(actor.velocity.z)); // exe 0x5e3810
    ball.timer -= 1u;
    if (ball.timer != 0 && actor.pos.y <= 0 && !(ball.hitBox.result & kHitAttackBump)) {
        const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
        ball.hitBox.result = 0;
        ball.hitBox.previousX = actor.prevPos.x.whole;
        ball.hitBox.previousZ = actor.prevPos.z.whole;
        hitListSubmit(p, ball.hitBox, hitOwner(actor.slot) | kBallEntry, kHitAttackStrong); // exe 0x5e4120
        actor.airborne = kAirborneFalling;
        actor.onPlatform = 0;
        actor.platform = nullptr;
        if (ball.boss->hitPoints < 0) freeActor(actor); // exe 0x5e1500
        return;
    }
    if (const uint32_t slot = allocActor40(kActorStageClass)) { // exe 0x5e1280
        ProjectileActor& blast = projectileActorAt(slot);
        blast.type = kBallExplosionType;
        blast.loopFlags |= kActorSweepGroup2;
        blast.flags = kActorLive;
        blast.subtype = 0;
        blast.variant = 0;
        blast.pos = actor.pos;
        blast.setParamsWord(slotIdentityWord(ball.boss));
    }
    dc::screenTint(0x4200ffff); // exe 0x5f3150: the full-screen tint
    soundPlayAtPos(kBallBurstSound, actor.pos);
    ball.timer = 4;
    ++actor.state;
}
// exe 0x5d1cc0: the subtractive tint exe 0x5f3150((v | 0x420000) << 8 | v), v = timer << 5, fading over the timer; at 0
// freed
void ballEnd(ProjectileActor& actor, BallFields& ball) {
    ball.timer -= 1u;
    const uint32_t v = uint32_t(int32_t(int16_t(ball.timer))) << 5;
    if (v != 0) dc::screenTint((v | 0x420000u) << 8 | v);
    else freeActor(actor); // exe 0x5e1500
}
void ballTick(ProjectileActor& actor) {
    BallFields& ball = classState<BallFields>(actor);
    switch (BallState(actor.state)) { // [0x8d1894] (= [0x8d188c] entries 2..4)
    case BallState::Start: ballStart(actor, ball); return;
    case BallState::Fly: ballFly(actor, ball); return;
    case BallState::End: ballEnd(actor, ball); return;
    default: notPortedAt(kStoryPort, "0x5d1a10 state past [0x8d1894]", actor.state);
    }
}

} // namespace

void bossFallingRockUpdate(const PlayerEnv&, uint32_t actor) { rockTick(animatedActorAt(actor)); }

void ballChargeUpdate(const PlayerEnv&, uint32_t actor) { ballCharge(staticActorAt(actor)); }

void beamChargeUpdate(const PlayerEnv&, uint32_t actor) { beamCharge(staticActorAt(actor)); }

void bossBladeUpdate(const PlayerEnv& env, uint32_t actor) { bladeTick(env, staticActorAt(actor)); }

void stompShotUpdate(const PlayerEnv& env, uint32_t actor) { stompShot(env, projectileActorAt(actor)); }

void chargeBallUpdate(const PlayerEnv&, uint32_t actor) { ballTick(projectileActorAt(actor)); }

} // namespace od::port
