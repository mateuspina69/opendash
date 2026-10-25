// ST4B: the effect / helper classes of the boss (exe 0x4b8d90, content/late_game/st4b). Names are hypotheses from the
// observed behaviour.
//
// The eye trail (static ST4B type 0x52 sub 0, exe 0x5b8000; two spawned by the boss's exe 0x4bad40 with the loop flags
// |= 6, its eye point = the boss's block +8 / +0x10, the points the boss's exe 0x4ba270 writes from bones 8 / 0xb):
// states [0x8d0b48](actor, actor + 0x28): 0 0x5b8030, 1 0x5b8080; then freed (exe 0x5e1540) once the eye point's word
// +6 is non-zero (the boss sets its block +0xe / +0x16 when its eye timer runs out). The ring at +0x28: s16 length (up
// to 0x1a), s16 head, then 0x1a s16 x / y / z of the point's past positions. Drawn as an additive ribbon (OT: DR_MODE +
// 4 POLY_G4 per segment, as the missile trail 0x563730 of content/nino/glyde_attack with head width 0x40 and widths
// (0x10 - n / 2) * 8). States 2..7 of the table (0x5b84a0 model bind, 0x5b8540 a falling bouncing piece, 0x5b8690 /
// 0x5b86e0 / 0x5b8700) are not reached from the eye spawner (it leaves the state 0 and 0x5b84a0 goes back to state 1,
// whose eye pointer is a point): notPortedAt.
//
// The volley shot (pool 0x40 ST4B type 0x30, exe 0x5d0cb0; the boss's volley exe 0x4ba950: its damage = the row's byte
// 2, speed 0x1400, its rotation): states [0x8d1820]: 0 0x5d0cd0 (the damage kept, timer 0x64, box 0x8d1830), 1 0x5d0d10
// (flies at its speed along the rotation; timer out, a hit (a player's attack or a bump) or a wall (exe 0x40f570 < 0) ->
// 2; a hit capsule from the current to the previous position, radius 0x10), 2 0x5d0e10 (variant 0: a hit spark at the
// shot, size 0x100; freed exe 0x5e1500). Drawn by exe 0x5d0e70 in states 0 / 1: two billboards (tpage 0x2f / 0xf, CLUT
// 0x7c92) and a ring.
//
// The falling piece (static ST4B type 6 sub 2, exe 0x595e80): states [0x8ce090] = [0x8ce084 + 3 * 4], i.e. states 3 /
// 4 / 5 of the class 0x595d80 (content/late_game/st4b): 0 0x595ea0 bind, 1 0x595f00 the script-driven falling piece (by
// the script step's sub and the step tick; rocking exe 0x59e580, effect statics 0x5960b0 / 0x596110, a hit-list entry),
// 2 0x5afdd0 freed.
//
// Spawned and ported elsewhere: global effect type 0 0x55d210 (game/hit_effects hit spark), static type 7 / static type
// 3 (game/global_fx / game/explosions). The model of the falling piece goes through the standard +0xa4 path; the eye
// trail and the volley shot have no model.
#include "content/late_game/st4b_fx.hpp"
#include "content/late_game/st4b_fx_state.hpp"

#include <algorithm>

#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/audio_logic.hpp"
#include "game/draw_commands.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"
#include "game/global_class_types.hpp"
#include "game/gunship_flight.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/script_state.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st4b_fx_state;

namespace {

// ---- the eye trail (static type 0x52, exe 0x5b8000) -----------------------------------------------------------------
enum class EyeTrailState : uint8_t { Start = 0, Trail = 1 };


// exe 0x5b8030: kept through warps, its own position cleared, the ring started at the eye point
void eyeStart(StaticActor& actor, EyeTrailFields& trail) {
    const ShortVec4& eye = *trail.eye;
    actor.runFlags |= kActorWarpKept;
    actor.pos.x = 0;
    actor.pos.y = 0;
    actor.pos.z = 0;
    trail.head = 0;
    trail.length = 0;
    trail.xs[0] = eye.x;
    trail.ys[0] = eye.y;
    trail.zs[0] = eye.z;
    ++actor.state;
}
// The rest of exe 0x5b8080: the ribbon from the head back along the ring. Each point (<< shift) is projected (exe
// 0x5f2dd0 -> S) and given a screen half width w = exe 0x5f2f20(point, width, &depth) (head: 0x40; then (0x10 - (n >>
// 1)) << 3 for the remaining count n); its corners C0 (sx - w, sy - w), C1 (sx + w + 1, sy - w), C2 (sx + w + 1, sy + w
// + 1), C3 (sx - w, sy + w + 1). Between a point t and the newer one o, unless w(t) == 0, min(depth(o), depth(t)) < 0x30
// or S(t) == 0x80008000: a DR_MODE 0xe1000220 (abr 1, additive) then 4 POLY_G4 0x3a (C_t.0 S_t C_o.0 S_o), (S_t C_t.2
// S_o C_o.2), (C_t.1 S_t C_o.1 S_o), (S_t C_t.3 S_o C_o.3), S coloured k (t) / k + 0x020202 (o), the corners 0x3a000000,
// k = 0x3a343434 - 0x020202 per step; linked at OT [0x6d0048] + depth(t), packet cursor 0x6d004c += 0x9c. The depth
// locals ([esp+0x1c] / [esp+0x18]) keep their last written value (exe 0x5f2f20 writes them only in range; 0 here before
// the first write, the exe's stack holds garbage). Port: the corners kept as the point + a camera-plane offset
// (dc::otQuadG4): p pixels at the point = p * (width << shift) / w camera-input units.
void eyeRibbon(const StaticActor& actor, const EyeTrailFields& trail, int h) {
    const int sh = stage().coordinateShift & 0x1f;
    struct Set {
        int16_t pt[3]; // camera-input point
        int16_t s[2];  // screen point
        int w;         // half width (pixels, << shift)
        float scale;   // camera-input units per pixel at the point (0: w == 0)
    } set[2]{};
    auto load = [&](Set& e, int i, int width, int16_t& depth) {
        const int16_t p[3] = {trail.xs[size_t(i)], trail.ys[size_t(i)], trail.zs[size_t(i)]};
        projectOnScreen(p, e.s);                         // exe 0x5f2dd0
        e.w = projectedWidth(p, int16_t(width), &depth); // exe 0x5f2f20
        for (int k = 0; k < 3; ++k) e.pt[k] = int16_t(uint16_t(p[k] << sh));
        e.scale = e.w != 0 ? float(width << sh) / float(e.w) : 0.0f;
    };
    auto corner = [](const Set& e, int k, float off[2]) { // corner k's pixel offset from S (C0..C3)
        static constexpr int kx[4] = {0, 1, 1, 0}, ky[4] = {0, 0, 1, 1};
        off[0] = float(kx[k] ? e.w + 1 : -e.w) * e.scale;
        off[1] = float(ky[k] ? e.w + 1 : -e.w) * e.scale;
    };
    int16_t depthPrev = 0, depthCur = 0; // stack [0x1c] / [0x18]
    int t = 0;                           // the toggle (the exe reuses its argument slot [esp+0x6c])
    load(set[0], h, 0x40, depthPrev);
    uint32_t k = 0x3a343434u;
    int idx = h;
    for (int n = trail.length; n > 1; --n) {
        idx = (idx + 0x19) % kTrailPoints;
        t ^= 1;
        Set& cur = set[t];
        const Set& old = set[t ^ 1];
        load(cur, idx, (0x10 - (n >> 1)) << 3, depthCur);
        const bool clipped = uint16_t(cur.s[0]) == 0x8000 && uint16_t(cur.s[1]) == 0x8000;
        if (cur.w != 0 && std::min(depthPrev, depthCur) >= 0x30 && !clipped) {
            const uint32_t kn = k + 0x20202u;
            struct V {
                bool newer;
                int c; // c < 0: S
            };
            static constexpr V kQuads[4][4] = {{{false, 0}, {false, -1}, {true, 0}, {true, -1}},
                                               {{false, -1}, {false, 2}, {true, -1}, {true, 2}},
                                               {{false, 1}, {false, -1}, {true, 1}, {true, -1}},
                                               {{false, -1}, {false, 3}, {true, -1}, {true, 3}}};
            for (int q = 0; q < 4; ++q) {
                uint32_t c[4];
                int16_t v[4][3];
                float off[4][2];
                for (int j = 0; j < 4; ++j) {
                    const V& vv = kQuads[q][j];
                    const Set& e = vv.newer ? old : cur;
                    std::copy(e.pt, e.pt + 3, v[j]);
                    if (vv.c < 0) {
                        off[j][0] = off[j][1] = 0.0f;
                        c[j] = vv.newer ? kn : k;
                    } else {
                        corner(e, vv.c, off[j]);
                        c[j] = 0x3a000000u;
                    }
                }
                dc::otQuadG4(actor.at, c, 1, v, off); // DR_MODE 0xe1000220 (abr 1)
            }
        }
        k -= 0x20202u;
        depthPrev = depthCur;
    }
}
// exe 0x5b8080: the ring grown by one (to 0x1a), moved by a move of its own (an in-stage warp's offset), the eye point
// pushed at its head; the ribbon
void eyeTrail(StaticActor& actor, EyeTrailFields& trail) {
    trail.length = int16_t(uint16_t(trail.length) + 1u);
    if (trail.length > kTrailPoints) trail.length = kTrailPoints;
    if (actor.pos.x != 0 || actor.pos.z != 0) { // a move of its own (the stage shift) moves the whole ring
        for (int i = 0; i < trail.length; ++i) {
            trail.xs[size_t(i)] = int16_t(uint16_t(trail.xs[size_t(i)]) + uint16_t(actor.pos.x.whole));
            trail.zs[size_t(i)] = int16_t(uint16_t(trail.zs[size_t(i)]) + uint16_t(actor.pos.z.whole));
        }
        actor.pos.x = 0;
        actor.pos.z = 0;
    }
    const int h = (int(trail.head) + 1) % kTrailPoints;
    trail.head = int16_t(h);
    const ShortVec4& eye = *trail.eye;
    trail.xs[size_t(h)] = eye.x;
    trail.ys[size_t(h)] = eye.y;
    trail.zs[size_t(h)] = eye.z;
    eyeRibbon(actor, trail, h);
}
// exe 0x5b8000
void eyeTrailTick(StaticActor& actor) {
    EyeTrailFields& trail = classState<EyeTrailFields>(actor);
    switch (EyeTrailState(actor.state)) { // [0x8d0b48 + state * 4](actor, actor + 0x28)
    case EyeTrailState::Start: eyeStart(actor, trail); break;
    case EyeTrailState::Trail: eyeTrail(actor, trail); break;
    default:
        notPortedAt(kStoryPort,
                    "0x5b8000 state (table 0x8d0b48; states 2..7 not reached from the eye spawner 0x4bad40)",
                    actor.state);
    }
    if (trail.eye->spare != 0) freeActor(actor); // exe 0x5e1540 (the point's 4th lane: released)
}

// ---- the volley shot (pool 0x40 type 0x30, exe 0x5d0cb0) -----------------------------------------------------------
enum class VolleyState : uint8_t { Start = 0, Fly = 1, End = 2 };
constexpr uint32_t kVolleyTicks = 0x64;
// group 3, no lock-on target, a capsule sampled every 2 steps
constexpr uint32_t kVolleyEntry = hitGroup(3) | kHitNoLockOn | hitCapsule(2);


// exe 0x5d0e70: two billboards (colour 0xe02040 / 0x404040, CLUT 0x7c92, tpage word 0x2f / 0xf with u 0x20..0x3f, v
// 0..0x3f, size word ((timer & 0xf) << 23) | 0x80) and a ring (0x4000, colour 0xe02040 / 0x22000000, z | 0x20000,
// 0x800000) at the shot; word 0 of the billboards is not written
void drawVolleyShot(const ProjectileActor& actor, const VolleyFields& shot) {
    const uint32_t xy = uint32_t(int32_t(actor.pos.y.whole)) << 16 | uint16_t(actor.pos.x.whole);
    const uint32_t zs = uint32_t(int32_t(actor.pos.z.whole));
    const uint32_t tw = ((shot.timer & 0xf) << 23) | 0x80u;
    dc::emit({0, dc::billboardColour(0xe02040), xy, 0x7c922000u, zs, 0x2f201fu, tw, 0x3f00u, 0, 0x3f1fu}, actor.at,
             false);
    dc::emit({0, dc::billboardColour(0x404040), xy, 0x7c922000u, zs, 0xf201fu, tw, 0x3f00u, 0, 0x3f1fu}, actor.at,
             false);
    dc::emit({0x4000u, dc::ringColour(0xe02040), xy, dc::kSemiAdditive, uint32_t(uint16_t(actor.pos.z.whole)) | 0x20000u,
              0x800000u},
             actor.at);
}
// exe 0x5d0cd0
void volleyStart(ProjectileActor& actor, VolleyFields& shot) {
    shot.hitBox.result = 0;
    shot.damage = uint32_t(int32_t(shot.spawnDamage));
    drawVolleyShot(actor, shot);
    shot.timer = kVolleyTicks;
    actor.collisionBox.pointAt(TableId::St4bFxVolleyBox);
    actor.state = uint8_t(VolleyState::Fly);
}
// exe 0x5d0d10
void volleyFly(const PlayerEnv& env, ProjectileActor& actor, VolleyFields& shot) {
    actor.savePreviousPose();
    shot.timer -= 1u;
    if (shot.timer == 0) actor.state = uint8_t(VolleyState::End);
    moveAlongRotation(actor.pos, actor.rotation, 0, 0, -int(actor.velocity.z)); // exe 0x5e37b0
    if (shot.hitBox.result & (kHitAttackByPlayer | kHitAttackBump)) actor.state = uint8_t(VolleyState::End);
    if (int32_t(actorTestMove(env, actor)) < 0) actor.state = uint8_t(VolleyState::End); // exe 0x40f570
    shot.hitBox.result = 0;
    shot.hitBox.previousX = actor.prevPos.x.whole;
    shot.hitBox.previousZ = actor.prevPos.z.whole;
    const int16_t prev[3] = {actor.prevPos.x.whole, actor.prevPos.y.whole, actor.prevPos.z.whole};
    const int16_t cur[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    hitListSubmitCapsule(cur, 0x10, prev, 0x10, shot.hitBox, hitOwner(actor.slot) | kVolleyEntry,
                         shot.damage | kHitAttackThrown | kHitAttackStrong); // exe 0x5e4190
    drawVolleyShot(actor, shot);
}
// exe 0x5d0e10
void volleyEnd(ProjectileActor& actor) {
    if (actor.variant == 0) {
        if (const uint32_t slot = allocActorA0(0)) { // exe 0x5e13b0
            EffectActor& spark = effectActorAt(slot);
            spark.flags = kActorLive;
            spark.loopFlags |= kActorSweepGroup2;
            spark.type = typeByte(GlobalEffectType::HitSpark);
            spark.variant = 0;
            spark.pos.x = actor.pos.x.whole;
            spark.pos.y = actor.pos.y.whole;
            spark.pos.z = actor.pos.z.whole;
            classState<HitSparkFields>(effectActorAt(slot)).size = 0x100;
        }
    }
    freeActor(actor); // exe 0x5e1500
}
// exe 0x5d0cb0
void volleyShot(const PlayerEnv& env, ProjectileActor& actor) {
    VolleyFields& shot = classState<VolleyFields>(actor);
    switch (VolleyState(actor.state)) { // [0x8d1820 + state * 4]
    case VolleyState::Start: volleyStart(actor, shot); return;
    case VolleyState::Fly: volleyFly(env, actor, shot); return;
    case VolleyState::End: volleyEnd(actor); return;
    default: notPortedAt(kStoryPort, "0x5d0cb0 state (table 0x8d1820 has 3 entries)", actor.state);
    }
}

// ---- the falling piece (static type 6 sub 2, exe 0x595e80) = states 3 / 4 / 5 of exe 0x595d80 -----------------------
enum class PieceState : uint8_t { Bind = 0, Run = 1, Free = 2 };
enum class PieceStep : uint8_t { Fall = 2, Tumble = 3 }; // the script step's sub
constexpr uint8_t kStageEffectType = 3, kDustVariant = 0x1b, kBlastVariant = 0x21;
constexpr uint16_t kLandSound = 0x8b;
// group 5, no lock-on target, radius 0x80
constexpr uint32_t kPieceEntry = hitGroup(kHitGroupLiftable) | kHitNoLockOn | hitRadius(0x80);
constexpr uint32_t kPieceAttack = kHitAttackHarmless | kHitAttackBump | 2;

// the spawn parameters of the stage's type-3 effects (their class is not ported here): two bytes, a word, the owner
struct StageEffectParams {
    explicit StageEffectParams(const StaticActor& actor) : StageEffectParams(const_cast<StaticActor&>(actor)) {}
    explicit StageEffectParams(StaticActor& actor)
        : paramA(actor.params[0]), paramB(actor.params[1]), paramWord(actor.params, 2), owner(actor.hitBox.result) {}
    uint8_t& paramA;
    uint8_t& paramB;
    BytesField<uint16_t, 4> paramWord;
    uint32_t& owner; // the owner's identity word (the hit box's first word)
};

// exe 0x5960b0: a blast (static global type 7, variant 0x21) at the piece, size 0x200, sound 0x8b
void landingBlast(const StaticActor& actor) {
    const uint32_t slot = allocActor60(0); // exe 0x5e1300
    if (!slot) return;
    StaticActor& blast = staticActorAt(slot);
    BlastParams params(actorHeaderAt(slot));
    blast.flags = kActorLive;
    blast.type = typeByte(GlobalStaticType::Blast);
    blast.loopFlags |= kActorSweepGroup2;
    blast.variant = kBlastVariant;
    blast.pos.x.whole = actor.pos.x.whole;
    blast.pos.y.whole = actor.pos.y.whole;
    blast.pos.z.whole = actor.pos.z.whole;
    params.size = 0x200;
    params.attack = 0;
    soundPlayAtPos(kLandSound, kSoundAtListener); // exe 0x5d5380(id, 0)
}
// exe 0x596110: a stage static type 3 variant 0x1b at the piece, following it
void followingDust(StaticActor& actor) {
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!slot) return;
    StaticActor& e = staticActorAt(slot);
    StageEffectParams params(staticActorAt(slot));
    e.flags = kActorLive;
    e.loopFlags |= kActorSweepGroup2;
    e.type = kStageEffectType;
    e.variant = kDustVariant;
    e.spawnByte = 0;
    e.pos.x.whole = actor.pos.x.whole;
    e.pos.y.whole = actor.pos.y.whole;
    e.pos.z.whole = actor.pos.z.whole;
    params.paramA = 0;
    params.paramB = 0;
    params.paramWord = 0x3c;
    params.owner = slotIdentityWord(actor);
}
// exe 0x595ea0
void pieceBind(StaticActor& actor, PieceFields& piece) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1540
        return;
    }
    actor.clearMotion();
    actor.clearClassBlock();
    piece.travelYaw = actor.rotation.y;
    actor.velocity.z = 0x800;
    actor.enterState(uint8_t(PieceState::Run));
}
// exe 0x595f00: by the script step's sub and its tick -- step 2: placed high, falls, at tick 0x1e lands (rocking, a
// blast, dust) and rocks; step 3: placed and tumbles away (dust every 16 ticks); its hit entry
void pieceRun(StaticActor& actor, PieceFields& piece) {
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    ScriptState& script = scriptState();
    const uint8_t step = scriptStepAt(script.steps.value()).sub;
    const int32_t tk = script.stepTick;
    if (PieceStep(step) == PieceStep::Fall) {
        if (tk != 0) {
            if (tk == 1) {
                actor.pos.x.whole = 0;
                actor.pos.y.whole = int16_t(0xf800);
                actor.pos.z.whole = 0;
                actor.velocity.z = 0x600;
            }
            if (script.stepTick == 0x1e) {
                rockInit(actor); // exe 0x5a06e0
                landingBlast(actor);
                followingDust(actor);
            }
            if (script.stepTick > 0x1e) rock(actor); // exe 0x59e580 (result unused)
        }
    } else if (PieceStep(step) == PieceStep::Tumble) {
        if (tk != 0) {
            if (tk == 1) {
                actor.pos.x.whole = 0x780;
                actor.pos.y.whole = int16_t(0xf6a0);
                actor.pos.z.whole = 0x2c0;
                actor.rotation.y = 0x6a0;
                actor.velocity.z = 0x380;
                actor.velocity.y = int16_t(0xff40);
                actor.acceleration.y = 6;
                actor.acceleration.z = 0x10;
                piece.travelYaw = 0x6a0;
                followingDust(actor);
            }
            const uint16_t v = uint16_t(actor.acceleration.z);
            actor.rotation.x = int16_t(uint16_t(actor.rotation.x) + 8u);
            actor.rotation.z = int16_t(uint16_t(actor.rotation.z) - v);
            actor.rotation.y = int16_t(uint16_t(actor.rotation.y) - 0xcu);
            actor.acceleration.z = int16_t(v + 2);
            if (!(uint8_t(script.stepTick) & 0xf)) followingDust(actor);
        }
    }
    actor.velocity.y = int16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y));
    moveAlongYaw(actor.pos, piece.travelYaw, 0, actor.velocity.y, -int(actor.velocity.z)); // exe 0x5e36f0
    piece.hitBox.result = 0;
    piece.hitBox.previousX = actor.prevPos.x.whole;
    piece.hitBox.previousZ = actor.prevPos.z.whole;
    const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    hitListSubmit(p, piece.hitBox, hitOwner(actor.slot) | kPieceEntry, kPieceAttack); // exe 0x5e4120
}
// exe 0x595e80
void fallingPiece(StaticActor& actor) {
    PieceFields& piece = classState<PieceFields>(actor);
    switch (PieceState(actor.state)) { // [0x8ce090 + state * 4]
    case PieceState::Bind: pieceBind(actor, piece); return;
    case PieceState::Run: pieceRun(actor, piece); return;
    case PieceState::Free: freeActor(actor); return; // 0x5afdd0 -> exe 0x5e1540
    default: notPortedAt(kStoryPort, "0x595e80 state (table 0x8ce090)", actor.state);
    }
}

} // namespace

void eyeTrailUpdate(const PlayerEnv&, uint32_t actor) { eyeTrailTick(staticActorAt(actor)); }
void volleyShotUpdate(const PlayerEnv& env, uint32_t actor) { volleyShot(env, projectileActorAt(actor)); }
void scriptFallingPieceUpdate(const PlayerEnv&, uint32_t actor) { fallingPiece(staticActorAt(actor)); }

} // namespace od::port
