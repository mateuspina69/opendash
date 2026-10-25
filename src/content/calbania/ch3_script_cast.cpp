// The cast of the Calbania chapter's scripts 0x69 / 0x20 (ST1F) and 0x3c (ST46; content/calbania/
// calbania_departure_script, square_defence_scripts, island_blown_script),
// transcribed from dash2.exe one function per exe function. Shadows (exe 0x5f20f0) are render output and left out.
// The cast keys on the script block's step sub and step tick (game/script_state).
#include "content/calbania/ch3_script_cast.hpp"
#include "content/calbania/ch3_script_cast_state.hpp"

#include "content/calbania/appo_dah_face.hpp"
#include "content/calbania/ch3_story.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/address_forms.hpp"
#include "game/angles.hpp"
#include "game/cutscene_actor.hpp"
#include "game/distances.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace ch3_script_cast_state;

namespace {

using address_forms::E;

int32_t stepTick() { return scriptState().stepTick; }
uint8_t stepSub() { return scriptState().stepSub; }

// =====================================================================================================================
// ---- exe 0x49bea0: ST1F pool 0x20 type 0x2f sub 0 (script 0x69 record 0x8acd70, slot 0): the variant [0x893750 + +6 *
// 4] = 0x49bf70 (variant 0 only). States: 0 bind (box 0x893758, texture tracks 0x89377c / 0x8937a4 stopped on cell 0), 1
// (step sub 0) tick 0x15 tracks 0 / 0x5b cell 0, 2 (step sub 1) track cues by the tick (jump tables 0x49c128 /
// 0x49c1f0), 3 (step sub 2) waits; a change of the step sub moves to the next state. Tail: on the ground, texture
// tracks, animation (kScript69Held: decode + pose only).
enum class Script69CastState : uint8_t { Bind = 0, Sub0 = 1, Sub1 = 2, Sub2 = 3, Done = 4 };
constexpr uint8_t kScript69Held = kCastHoldPose | kCastShadow; // the cast flags that hold its pose (the exe's & 3)

// exe 0x49bf70: the cues by the step sub and the tick
void script69Cues(AnimatedActor& actor, CastFields& c) {
    switch (Script69CastState(actor.state)) {
    case Script69CastState::Bind:
        if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
            freeActor(actor);
            return;
        }
        actor.collisionBox.pointAt(TableId::Ch3ScriptCastScript69CastBox);
        actor.clearClassBlock();
        actor.enterState(uint8_t(Script69CastState::Sub0));
        actor.anim = 0;
        actor.prevAnim = kNoAnimShown;
        clearVelocity(actor);
        c.animKey = 0;
        c.faceTracks.init(stateAddress(TableId::Ch3ScriptCastScript69Eyes), stateAddress(TableId::Ch3ScriptCastScript69Mouth));
        textureTrackStop(actor, c.faceTracks, FaceTrack::Eyes, 0);
        textureTrackStop(actor, c.faceTracks, FaceTrack::Mouth, 0);
        [[fallthrough]];
    case Script69CastState::Sub0:
        if (stepSub() != 0) {
            actor.enterState(uint8_t(Script69CastState::Sub1));
            return;
        }
        if (stepTick() == 0x15) {
            c.faceTracks.first.select(0, 0);
            c.faceTracks.second.select(0, 0);
            return;
        }
        if (stepTick() == 0x5b) textureTrackStop(actor, c.faceTracks, FaceTrack::Mouth, 0);
        return;
    case Script69CastState::Sub1:
        if (stepSub() != 1) {
            actor.enterState(uint8_t(Script69CastState::Sub2));
            return;
        }
        switch (stepTick()) {
        case 1: case 0x97: case 0xb5: case 0xfb: case 0x141: c.faceTracks.second.select(0, 0); return;
        case 0x6f: case 0xab: case 0xe2: case 0x123: case 0x178:
            textureTrackStop(actor, c.faceTracks, FaceTrack::Mouth, 0);
            return;
        case 0x8d: c.faceTracks.first.select(0, 0); return;
        case 0x12d: c.faceTracks.first.select(1, 0); return;
        default: return;
        }
    case Script69CastState::Sub2:
        if (stepSub() != 2) actor.enterState(uint8_t(Script69CastState::Done));
        return;
    default: return;
    }
}
// exe 0x49bea0's body
void script69Cast(AnimatedActor& actor) {
    CastFields& c = classState<CastFields>(actor);
    actor.savePreviousPose();
    actor.prevAnim = actor.anim;
    if (actor.variant != 0) notPortedAt(kStoryPort, "0x49bea0 variant (table 0x893750)", actor.variant);
    script69Cues(actor, c);
    if (!(actor.flags & kActorLive)) return;
    actor.pos.y.whole = int16_t(actorGround(*E, actor)); // exe 0x40f190
    textureTracksTick(actor, c.faceTracks);               // exe 0x5e34f0
    if (actor.anim != actor.prevAnim) {
        animSetPose(actor, actor.anim, c.animKey); // exe 0x5e1c30
    } else if (c.castFlags & kScript69Held) {
        animDecodeCurrent(actor); // exe 0x5e1f30
        animPose(actor);          // exe 0x5e2d90
    } else {
        animStepPose(actor);
    }
    // exe 0x5f20f0(actor, 0x30, 0x100, 0): the shadow (render output)
}

constexpr uint8_t kAppoDahRestart = 0x01; // the animation restarted at its key every tick (the pose held)
enum class Script20AppoDahState : uint8_t { Bind = 0, Released = 1, Act2 = 2, Act3 = 3, Scene = 4 };
enum class BindSub : uint8_t { Bind = 0, WaitScript = 1 };
enum class SceneStep : uint8_t { Place = 0, WalkIn = 1, Stop = 2, Wait = 3, Talk = 4, Done = 5 };
constexpr uint32_t kFlagAppoDahHurt = 0x680; // + the variant: hurt in the fight (grey)
constexpr uint32_t kFlagDahAtGate = 0x580;
constexpr uint16_t kHurtTint = 0x2108;
constexpr uint8_t kAnimSit = 0x1c, kAnimSitOther = 0x1d, kAnimWalkIn = 0x1e, kAnimStand = 0x1f;

// exe 0x4c70c0
void script20AppoDahRoutine(AnimatedActor& actor, Script20AppoDahFields& f) {
    switch (Script20AppoDahState(actor.state)) {
    case Script20AppoDahState::Bind:
        if (BindSub(actor.subState) == BindSub::Bind) {
            if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
                freeActor(actor);
                return;
            }
            actor.collisionBox.pointAt(TableId::Ch3ScriptCastScript20AppoDahBox);
            actor.clearClassBlock();
            actor.prevAnim = kNoAnimShown;
            f.castFlags |= kAppoDahRestart;
            appoDahFaceReset(actor, f.mouth);
            actor.anim = f.seated != 0 ? kAnimSitOther : kAnimSit;
            if (storyFlag(uint32_t(actor.variant) + kFlagAppoDahHurt)) actor.tint = kHurtTint;
            actor.subState = uint8_t(BindSub::WaitScript);
            return;
        }
        if (scriptState().state != 0) actor.enterState(uint8_t(Script20AppoDahState::Released));
        return;
    case Script20AppoDahState::Released:
        if (actor.step == 0) {
            f.castFlags &= uint8_t(~kAppoDahRestart);
            actor.step = 1;
        }
        if (stepSub() == 1) actor.enterState(uint8_t(Script20AppoDahState::Act2));
        return;
    case Script20AppoDahState::Act2:
        if (stepSub() == 2) actor.enterState(uint8_t(Script20AppoDahState::Act3));
        return;
    case Script20AppoDahState::Act3:
        if (stepSub() == 3) actor.enterState(uint8_t(Script20AppoDahState::Scene));
        return;
    case Script20AppoDahState::Scene:
        switch (SceneStep(actor.step)) {
        case SceneStep::Place:
            if (actor.variant != uint8_t(storyFlag(kFlagDahAtGate) ? 1 : 0)) {
                actor.pos.x.whole = 0x139b;
                actor.pos.z.whole = int16_t(0xffda);
                actor.rotation.y = 0xe00;
                actor.anim = kAnimWalkIn;
                actor.step = uint8_t(SceneStep::WalkIn);
            } else {
                actor.pos.x.whole = 0x1560;
                actor.pos.z.whole = int16_t(0xff42);
                actor.rotation.y = 0x238;
                actor.anim = kAnimStand;
                actor.step = uint8_t(SceneStep::Wait);
            }
            return;
        case SceneStep::WalkIn:
            integrateVelocity(actor, 0, 0, -0x40); // exe 0x5e3690
            if (stepTick() == 0x35) actor.step = uint8_t(SceneStep::Stop);
            return;
        case SceneStep::Stop:
            if (stepTick() != 0x37) return;
            f.animKey = 0xf;
            f.castFlags |= kAppoDahRestart;
            actor.step = uint8_t(SceneStep::Wait);
            return;
        case SceneStep::Wait:
            if (stepTick() == 0x3c) actor.step = uint8_t(SceneStep::Talk);
            return;
        case SceneStep::Talk:
            appoDahTalk(actor, f.mouth);
            if (stepTick() != 0x8c) return;
            appoDahFaceReset(actor, f.mouth);
            actor.step = uint8_t(SceneStep::Done);
            return;
        default: return;
        }
    default: return;
    }
}
// exe 0x4c6fe0's body
void script20AppoDah(AnimatedActor& actor) {
    Script20AppoDahFields& f = classState<Script20AppoDahFields>(actor);
    actor.savePreviousPose();
    actor.prevAnim = actor.anim;
    const int32_t t = stepTick();
    if (t == 1 || t != int32_t(f.lastTick)) {
        f.lastTick = int8_t(t);
        if (f.routine != 0) notPortedAt(kStoryPort, "0x4c6fe0 variant (table 0x894ed0)", uint8_t(f.routine));
        script20AppoDahRoutine(actor, f);
    }
    if (!(actor.flags & kActorLive)) return;
    if (f.noGround == 0) actor.pos.y.whole = int16_t(actorGround(*E, actor)); // exe 0x40f190
    if (actor.anim == actor.prevAnim && !(f.castFlags & kAppoDahRestart)) animStepPose(actor);
    else animSetPose(actor, actor.anim, f.animKey);
    // exe 0x5f20f0(actor, 0x30, 0x160, 0): the shadow (render output)
}

enum class Script3cCastState : uint8_t { Bind = 0, Act = 1 };
enum class Script3cAction : uint8_t { Wait = 0, Walk = 1, Placed = 2 };
enum class WaitStep : uint8_t { Hold = 0, Play = 1, WaitSub = 2 };
enum class WalkStep : uint8_t { Turn = 0, Walk = 1, WaitSub = 2 };
constexpr size_t kHiddenParts[2] = {4, 7}, kShownParts[2] = {0xe, 0xf};
constexpr size_t kPlacedHiddenPart = 0xe, kPlacedShownPart = 0xd;
constexpr uint32_t kScript3cPage = 4;

// exe 0x4e2e80
void script3cBind(AnimatedActor& actor) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    actor.velocity.clearWords();
    actor.spin.setXY(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.parts[kHiddenParts[0]].flags |= kPartHidden;
    actor.parts[kHiddenParts[1]].flags |= kPartHidden;
    actor.anim = 3;
    actor.prevAnim = kNoAnimShown;
    actor.maxHitPoints = 1;
    actor.hitPoints = 1;
    actor.collisionBox.pointAt(TableId::Ch3ScriptCastScript3cCastBox);
    actor.parts[kShownParts[0]].flags &= uint8_t(~kPartHidden);
    actor.parts[kShownParts[1]].flags &= uint8_t(~kPartHidden);
    setTexturePageCell(actor.texturePage, kScript3cPage); // exe 0x5e3280(actor, 4)
    actor.clearClassBlock();
    actor.enterState(uint8_t(Script3cCastState::Act));
}
// exe 0x4e2f60
void script3cWait(AnimatedActor& actor, Script3cCastFields& f) {
    switch (WaitStep(actor.step)) {
    case WaitStep::Hold:
        actor.anim = 3;
        f.unread153 = 0;
        if (scriptState().part<IslandBlownBytes>().castStarted == 0) {
            f.holdPose = 1;
            return;
        }
        f.holdPose = 0;
        actor.step = uint8_t(WaitStep::Play);
        [[fallthrough]];
    case WaitStep::Play:
        if (!actor.animEnded()) return;
        actor.anim = 4;
        actor.enterStep(uint8_t(WaitStep::WaitSub));
        return;
    case WaitStep::WaitSub:
        if (stepSub() != 1) return;
        actor.subState = uint8_t(Script3cAction::Placed);
        actor.enterStep(0);
        return;
    default: return;
    }
}
constexpr int32_t kOrigin[3] = {0, 0, 0}; // the local point the walk aims at
// exe 0x4e2fe0
void script3cWalk(AnimatedActor& actor) {
    switch (WalkStep(actor.step)) {
    case WalkStep::Turn:
        actor.anim = 1;
        actor.rotation.y = int16_t(angleOf(kOrigin[0], kOrigin[2], actor.pos.x, actor.pos.z)); // exe 0x5e3bc0
        ++actor.step;
        [[fallthrough]];
    case WalkStep::Walk:
        integrateVelocity(actor, 0, 0, -0x80);
        if (planarDistance(actor.pos, kOrigin) >= 0x41) return; // exe 0x5e3b00
        actor.anim = 2;
        actor.step = uint8_t(WalkStep::WaitSub);
        return;
    case WalkStep::WaitSub:
        if (stepSub() != 2) return;
        actor.enterStep(0);
        actor.subState = uint8_t(Script3cAction::Placed);
        return;
    default: return;
    }
}
// exe 0x4e3080
void script3cPlaced(AnimatedActor& actor) {
    if (actor.step != 0) return;
    actor.anim = 5;
    actor.rotation.y = int16_t(angleOf(kOrigin[0], kOrigin[2], actor.pos.x, actor.pos.z));
    actor.pos.x.whole = int16_t(0xffb8);
    actor.pos.z.whole = 0x18;
    actor.parts[kPlacedHiddenPart].flags |= kPartHidden;
    actor.parts[kPlacedShownPart].flags &= uint8_t(~kPartHidden);
    ++actor.step;
}
// exe 0x4e2da0's body
void script3cCast(AnimatedActor& actor) {
    Script3cCastFields& f = classState<Script3cCastFields>(actor);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (Script3cCastState(actor.state)) {
    case Script3cCastState::Bind: script3cBind(actor); break;
    case Script3cCastState::Act:
        switch (Script3cAction(actor.subState)) { // [0x895e6c + +9 * 4]
        case Script3cAction::Wait: script3cWait(actor, f); break;
        case Script3cAction::Walk: script3cWalk(actor); break;
        case Script3cAction::Placed: script3cPlaced(actor); break;
        default: notPortedAt(kStoryPort, "0x4e2da0 action (table 0x895e6c)", actor.subState);
        }
        break;
    default: notPortedAt(kStoryPort, "0x4e2da0 state (table 0x895e64)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim != actor.prevAnim) animSetPose(actor, actor.anim, 0);
    else if (f.holdPose != 0) animDecodeCurrent(actor); // exe 0x5e1f30
    else animStepPose(actor);
    // exe 0x5f20f0(actor, 0x30, 0x160, 0): the shadow (render output)
}

} // namespace

void script69CastUpdate(const PlayerEnv& env, uint32_t actor) {
    E = &env;
    script69Cast(animatedActorAt(actor));
}
void script20AppoDahUpdate(const PlayerEnv& env, uint32_t actor) {
    E = &env;
    script20AppoDah(animatedActorAt(actor));
}
void script3cCastUpdate(const PlayerEnv& env, uint32_t actor) {
    E = &env;
    script3cCast(animatedActorAt(actor));
}

} // namespace od::port
