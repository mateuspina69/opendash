// The ST3B cast of script 0x5c (content/saul_kada/pirate_base_scripts): the figure on the gate (pool 0x20 type 0 sub 1, exe
// 0x448ae0) and the gate's debris (pool 0xa0 stage type 0x1c, exe 0x43aa30); hypothesis names. Shadows (exe 0x5f20f0)
// are render output and left out.
#include "content/saul_kada/st3b_script5c_cast.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/camera_state.hpp"
#include "game/cutscene_actor.hpp"
#include "game/draw_commands.hpp"
#include "game/matrix.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/projection_state.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/table_records.hpp"

namespace od::port {

namespace {

// ---- exe 0x448ae0: ST3B pool 0x20 type 0 sub 1 (script 0x5c record 0x8ab634, slot 1) -------------------------------
// States [0x88e30c]: 0 bind 0x448b80 (model variant 0x2d, box 0x88e300), 1 run 0x448c00: variant 0 plays the actions
// [0x88e314] (0 animations by the step tick 0x448c40, 1 hidden until the script's step sub-id 5 0x448d90, 2 follows
// the script's first actor once its sub-state is 6, then flies off 0x448d90), another variant 0x448e20 (freed once flag
// 0x5a0). Action 3 (0x449070) reads the state as its own (a whole class body): no path sets it; not ported.
enum class FigureState : uint8_t { Bind = 0, Run = 1 };
enum class FigureAction : uint8_t { Animate = 0, Hidden = 1, Follow = 2 };
enum class FollowStep : uint8_t { Wait = 0, Fly = 1 };
constexpr int kFigureModel = 0x2d;
constexpr uint32_t kFlagGateOpen = 0x5a0;
constexpr uint8_t kLeaderJumpSub = 6, kFigureShowCue = 5, kFigureHideCue = 1;

// exe 0x448b80
void bindFigure(AnimatedActor& actor) {
    if (!bindActorModel(actor, kFigureModel)) { // exe 0x5e0bc0(actor, &0x2d)
        freeActor(actor);                       // exe 0x5e1490
        return;
    }
    actor.anim = 0;
    actor.prevAnim = kNoAnimShown;
    actor.maxHitPoints = 1;
    actor.hitPoints = 1;
    actor.velocity.clearWords();
    actor.collisionBox.pointAt(TableId::St3bScript5cCastFigureBox);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock();
    actor.enterState(uint8_t(FigureState::Run));
}
// exe 0x448c40: the animations 1..4 at the step ticks 0x3c / 0x78 / 0xdc / 0xfa (jump table 0x448cb0); the step
// sub-id 1 hides it
void animateByTick(AnimatedActor& actor) {
    switch (scriptState().stepTick) {
    case 0x3c: actor.anim = 1; break;
    case 0x78: actor.anim = 2; break;
    case 0xdc: actor.anim = 3; break;
    case 0xfa: actor.anim = 4; break;
    default: break;
    }
    if (scriptState().stepSub == kFigureHideCue) {
        actor.subState = uint8_t(FigureAction::Hidden);
        actor.restartStep();
    }
}
// exe 0x448d70: hidden until the step sub-id 5
void hidden(AnimatedActor& actor) {
    actor.setDrawn(false);
    if (scriptState().stepSub != kFigureShowCue) return;
    actor.subState = uint8_t(FigureAction::Follow);
    actor.step = 0;
    actor.stepCounter = 0;
}
// exe 0x448d90: at the script's first actor once its sub-state is 6 (animation 5), then flies off under gravity
void followLeader(AnimatedActor& actor) {
    const Actor& leader = actorLayoutOf(*scriptState().actors[0]);
    if (FollowStep(actor.step) == FollowStep::Wait) {
        if (int8_t(leader.subState) != kLeaderJumpSub) return;
        actor.flags |= kActorDrawn;
        actor.anim = 5;
        actor.pos.x = leader.pos.x;
        actor.pos.y = leader.pos.y;
        const int32_t z = leader.pos.z;
        actor.rotation.y = 0xfb8;
        actor.velocity.y = int16_t(0xfe70);
        actor.pos.z = z;
        actor.pos.y.whole -= 0x100u;
        actor.velocity.z = int16_t(0xf800);
        actor.rotation.x = 0x100;
        actor.step = uint8_t(FollowStep::Fly);
        return;
    }
    if (FollowStep(actor.step) != FollowStep::Fly) return;
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e3690
    actor.velocity.y += 0x28u;
}
// exe 0x448ae0
void gateFigure(AnimatedActor& actor) {
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (FigureState(actor.state)) {
    case FigureState::Bind: bindFigure(actor); break;
    case FigureState::Run: // 0x448c00
        if (actor.variant != 0) {
            if (storyFlag(kFlagGateOpen)) freeActor(actor); // 0x448e20
            break;
        }
        switch (FigureAction(actor.subState)) { // 0x448c20: [0x88e314]
        case FigureAction::Animate: animateByTick(actor); break;
        case FigureAction::Hidden: hidden(actor); break;
        case FigureAction::Follow: followLeader(actor); break;
        default: notPortedAt(kStoryPort, "0x448ae0 action (table 0x88e314)", actor.subState);
        }
        break;
    default: notPortedAt(kStoryPort, "0x448ae0 state (table 0x88e30c)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    animateAsked(actor, 0); // exe 0x5e1c30 / 0x5e1e80
    // exe 0x5f20f0(actor, 0x40, 0x160, 0) while drawn: the shadow (render output)
}

// ---- exe 0x43aa30: pool 0xa0 stage type 0x1c (ST3B), the gate's debris of script 0x5c (0x536630) -------------------
// Tumbles (its spin added to its angles) under gravity 0x40 (its velocity in 1/16), freed once below y 0. Drawn as one
// textured POLY_FT4 0x2c808080 (CLUT 0x3f08, tpage 0x36): the variant's corners (exe 0x88d7a8) rotated by Ry Rx Rz
// of its angles (built in the projections' scratch transform) around its position (not shifted), the variant's texels
// (exe 0x88d828 {u, v, w, h}); only when its view depth is in 0x30 .. the effects' far * 4 (linked at its average
// depth, no range test).
enum class DebrisState : uint8_t { Start = 0, Fly = 1 };
// A corner of a debris piece (exe 0x88d7a8: 4 per variant, 8 bytes each)
const DataTable<RecordTable<ShortPoint, 0x20, 8>> debrisCorners{TableId::St3bScript5cCastDebrisCorners};
// The texels of a debris piece (exe 0x88d828, by variant)
struct DebrisTexels : DataRecord {
    uint16_t u = 0;
    int16_t v = 0;
    int16_t w = 0;
    int16_t h = 0;
    static DebrisTexels load(const TableReader& r) {
        DebrisTexels rec;
        rec.u = r.u16(0);
        rec.v = r.s16(2);
        rec.w = r.s16(4);
        rec.h = r.s16(6);
        return rec;
    }
};
const DataTable<RecordTable<DebrisTexels, 8, 8>> debrisTexels{TableId::St3bScript5cCastDebrisTexels};
constexpr int16_t kDebrisGravity = 0x40;

void gateDebris(EffectActor& actor) {
    GateDebrisFields& debris = classState<GateDebrisFields>(actor);
    if (DebrisState(actor.state) == DebrisState::Start) actor.state = uint8_t(DebrisState::Fly);
    debris.angles.x += debris.spinX;
    debris.angles.y += debris.spinY;
    actor.pos.x += uint16_t(debris.velocity.x >> 4);
    actor.pos.y += uint16_t(debris.velocity.y >> 4);
    actor.pos.z += uint16_t(debris.velocity.z >> 4);
    debris.velocity.y += uint16_t(kDebrisGravity);
    if (actor.pos.y >= 1) {
        freeActor(actor); // exe 0x5e1580
        return;
    }
    ViewTransform& turn = projectionScratch().transform;
    turn.setIdentityWords();
    turn.setRotationYXZ(debris.angles); // exe 0x40bfd0
    const Matrix3 rotation = turn.matrix;
    int16_t corners[4][3];
    for (uint32_t k = 0; k < 4; ++k) {
        const ShortPoint corner = debrisCorners[size_t(actor.variant) * 4 + k];
        const int16_t point[3] = {corner.x, corner.y, corner.z};
        int16_t turned[3];
        transformShortVector(rotation, point, turned); // exe 0x40bb30
        corners[k][0] = int16_t(turned[0] + actor.pos.x);
        corners[k][1] = int16_t(turned[1] + actor.pos.y);
        corners[k][2] = int16_t(turned[2] + actor.pos.z);
    }
    const int16_t pos[3] = {actor.pos.x, actor.pos.y, actor.pos.z};
    const CameraPoint view = cameraPoint(pos); // exe 0x40c3c0 / 0x40c3e0 / 0x40c6a0 / 0x40c800 / 0x40caa0
    if (!effectDepthVisible(view)) return;
    const DebrisTexels texels = debrisTexels[actor.variant];
    const uint32_t u0 = texels.u, u1 = uint16_t(texels.w + texels.u);
    const uint32_t v0 = uint32_t(uint16_t(texels.v)) << 8, v1 = uint32_t(uint16_t(texels.h + texels.v)) << 8;
    const uint32_t words[10] = {0x9000000u, 0x2c808080u, 0, 0x3f080000u | ((v0 | u0) & 0xffff), 0,
                                0x360000u | ((v0 | u1) & 0xffff), 0, (v1 | u0) & 0xffff, 0, (v1 | u1) & 0xffff};
    dc::otQuadFT4(actor.at, words, corners);
}

} // namespace

void gateFigureUpdate(const PlayerEnv&, uint32_t actor) { gateFigure(animatedActorAt(actor)); }
void gateDebrisUpdate(const PlayerEnv&, uint32_t actor) { gateDebris(effectActorAt(actor)); }

} // namespace od::port
