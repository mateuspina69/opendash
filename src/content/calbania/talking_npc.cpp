// The standing NPCs you talk to, 0x449620 (ST24) and its ST17 twin 0x4499a0 (see talking_npc.hpp), moved here from
// game/actors. States [0x88e36c + state * 4] (the twin: 0x88e39c): 0 = 0x449780 init (model variant 0, the class block
// cleared, animation +0xc, scale 0x200, the boxes, hit points 1; the twin: 0x449b00 with its boxes), 1 = 0x449830 ->
// [0x88e378 + sub * 4] (the twin: 0x88e3a8): 0x449850 waits for the player's talk (the event of message +0xf,
// param ((+0xd & 0x40) | 8) >> 2), 0x4498b0 talks (the animation the message script asks for; the event over ->
// animation 5; the twin's 0x449bd0 opens its hints from the stage's message bank). Every tick: wall test / push-out /
// ground, animation + pose, the hit-list entry (y - 0x30), a stand-on solid of type 1.
#include "content/calbania/talking_npc.hpp"
#include "content/calbania/talking_npc_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/event_state.hpp"
#include "game/hit_list.hpp"
#include "game/message.hpp"
#include "game/event.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/save_menus.hpp"
#include "game/actors.hpp"
#include "game/address_forms.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

using namespace talking_npc_state;

namespace {

using address_forms::E;

constexpr const char* kTalkingNpcPort = "talking NPC"; // the area of its port-gap errors

constexpr uint8_t kTalkerFrozen = 0x80;    // frozen on its current frame (decoded only: no step, no pose)
constexpr uint8_t kTalkerEventFlag = 0x40; // its talk event gets flag 0x10
constexpr uint8_t kTalking = 0x01;

enum class TalkerState : uint8_t {
    Init = 0, // exe 0x449780 (the twin: 0x449b00)
    Main = 1, // exe 0x449830 (the twin: 0x449bb0)
};
enum class TalkerSub : uint8_t {
    Wait = 0, // exe 0x449850
    Talk = 1, // exe 0x4498b0 (the twin: 0x449bd0)
};

constexpr uint8_t kAnimTalk = 4, kAnimSettle = 5, kNoAnim = 0xff;
constexpr uint32_t kTalkEventParam = 8; // ((behaviour & 0x40) | 8) >> 2: the event's flags
constexpr uint32_t kHintsFlag = 0x6f9;  // the message chose "hints"
constexpr int8_t kHintsWindow = 1;
constexpr int16_t kScaleOne = 0x200;
constexpr int kHitHeight = 0x30;

const DataTable<CollisionBoxRecord> talkerCollisionBox{TableId::TalkingNpcTalkerCollisionBox};
const DataTable<InteractionBoxRecord> talkerTalkBox{TableId::TalkingNpcTalkerTalkBox};
const DataTable<CollisionBoxRecord> twinCollisionBox{TableId::TalkingNpcTwinCollisionBox};
const DataTable<InteractionBoxRecord> twinTalkBox{TableId::TalkingNpcTwinTalkBox};

// exe 0x449850: waits for the player's talk, then starts its event
void talkerWait(AnimatedActor& actor, TalkingNpcFields& npc) {
    if (npc.talkFlags & kTalking) return;
    if (!player().talksTo(actor) || eventState().active != 0) return;
    const uint32_t param = ((npc.behaviour & kTalkerEventFlag) | kTalkEventParam) >> 2;
    eventStart(&actor.pos, &actor.rotation, param, 0, npc.message); // exe 0x543af0
    actor.subState = uint8_t(TalkerSub::Talk);
    actor.enterStep(0);
}
// stageHints: the twin's 0x449bd0 (the hints window on the stage's message bank, message +0xe) instead of 0x4498b0's
// exe bank
void talkerTalk(AnimatedActor& actor, TalkingNpcFields& npc, bool stageHints) {
    EventState& event = eventState();
    switch (actor.step) {
    case 0:
        npc.talkFlags |= kTalking;
        npc.anim = 0;
        event.speakerAnimation = kNoSpeakerAnimation;
        actor.enterStep(1);
        [[fallthrough]];
    case 1:
        if (storyFlag(kFlagSaveMenuAsked)) {
            saveMenuTick(npc.saveMenu); // exe 0x548f60
        } else if (storyFlag(kHintsFlag)) {
            windowClose(kHintsWindow, WindowClose::Force);
            if (stageHints) windowOpen(kHintsWindow, kMessageBank, npc.hintsMessage);
            else windowOpen(kHintsWindow, stateAddress(TableId::TalkingNpcHintsBank), 0);
            clearStoryFlag(kHintsFlag); // exe 0x510e30
        }
        if (event.speakerAnimation != kNoSpeakerAnimation) {
            npc.anim = event.speakerAnimation;
            event.speakerAnimation = kNoSpeakerAnimation;
        } else if (actor.animKey.flags == kKeyHold) {
            npc.anim = kAnimTalk;
        }
        if (event.active == 0) {
            npc.anim = kAnimSettle;
            actor.enterStep(2);
        }
        return;
    case 2:
        if (actor.animEnded()) {
            npc.talkFlags &= uint8_t(~kTalking);
            actor.subState = uint8_t(TalkerSub::Wait);
            npc.anim = 0;
            actor.enterStep(0);
        }
        return;
    default: return;
    }
}
// the body of both classes (exe 0x449620 / 0x4499a0): the state, the animation, the hit entry, a solid
void talkingNpc(AnimatedActor& actor, const DataTable<CollisionBoxRecord>& collisionBox, const DataTable<InteractionBoxRecord>& talkBox, bool stageHints) {
    TalkingNpcFields& npc = classState<TalkingNpcFields>(actor);
    actor.savePreviousPose();
    actor.prevAnim = actor.anim;
    npc.frameCopy = uint8_t(actor.animKey.frame);
    switch (TalkerState(actor.state)) {
    case TalkerState::Init:
        if (!bindActorModel(actor, 0)) {
            freeActor(actor);
            return;
        }
        actor.clearClassBlock();
        actor.anim = npc.anim;
        actor.scale.z = kScaleOne;
        actor.scale.y = kScaleOne;
        actor.scale.x = kScaleOne;
        actor.collisionBox.pointAt(collisionBox.stateAddress());
        actor.interactionBox.pointAt(talkBox.stateAddress());
        actor.interaction = kInteractTalk;
        actor.prevAnim = kNoAnim;
        actor.airborne = 0;
        actor.onPlatform = 0;
        actor.maxHitPoints = 1;
        actor.hitPoints = 1;
        actor.velocity.clearWords();
        actor.spin.clearWords();
        npc.clearTalkState();
        actor.flags |= kActorDrawn;
        actor.enterState(uint8_t(TalkerState::Main));
        break;
    case TalkerState::Main:
        switch (TalkerSub(actor.subState)) {
        case TalkerSub::Wait: talkerWait(actor, npc); break;
        case TalkerSub::Talk: talkerTalk(actor, npc, stageHints); break;
        default: notPortedAt(kTalkingNpcPort, "sub-state", actor.subState);
        }
        break;
    default: notPortedAt(kTalkingNpcPort, "state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actorTestMove(*E, actor) != 0) actorPushOut(*E, actor);
    actor.pos.y.whole = int16_t(actorGround(*E, actor));
    if (actor.anim != npc.anim) actor.anim = npc.anim;
    if (actor.anim == actor.prevAnim) {
        if (npc.behaviour & kTalkerFrozen) {
            animDecodeCurrent(actor); // exe 0x5e1f30
        } else {
            animStepPose(actor);
        }
    } else {
        animSetPose(actor, actor.anim, npc.startKey);
    }
    npc.hitBox.result = 0;
    npc.hitBox.previousX = actor.prevPos.x.whole;
    npc.hitBox.previousZ = actor.prevPos.z.whole;
    const int16_t pos[3] = {actor.pos.x.whole, int16_t(uint16_t(actor.pos.y.whole - kHitHeight)), actor.pos.z.whole};
    hitListSubmit(pos, static_cast<HitBoxHead&>(npc.hitBox), hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | hitRadius(0x40),
                  kHitAttackHarmless);
    // exe 0x5f20f0(actor, 0x28, 0x140, 0): the shadow list entry (render output)
    actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0);
}

} // namespace

void talkingNpcUpdate(const PlayerEnv& env, uint32_t address) {
    E = &env;
    talkingNpc(animatedActorAt(address), talkerCollisionBox, talkerTalkBox, false);
}

void talkingNpcSt17Update(const PlayerEnv& env, uint32_t address) {
    E = &env;
    talkingNpc(animatedActorAt(address), twinCollisionBox, twinTalkBox, true);
}

} // namespace od::port
