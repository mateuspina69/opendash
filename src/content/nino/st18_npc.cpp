// The ST18 standing NPC, class 0x443aa0 (pool 0x20 type 0; variant 6 on ST18 at story 9 / 0xa): a standing NPC that
// talks. The town NPC (game/town_npc, class 0x441950) without the route, the move tests and the partner, on the same
// field layout and head / look routines; states 0x88ddf4 (0 init 0x443bb0, 1 main 0x443c30), sub-states 0x88ddfc
// (0 / 1 stand 0x443cf0, 2 talk 0x443de0, 3 face the player 0x443ec0, 4 bumped 0x443f50); boxes 0x88ddd8 / 0x88dde8,
// the hit point 0x60 above its feet. Its bump test 0x444070 returns 0: sub-state 4 is never entered.
#include "content/nino/st18_npc.hpp"

#include "engine/data_source.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/distances.hpp"
#include "game/event_state.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/actors.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/message.hpp"
#include "game/story.hpp"
#include "game/town_npc.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

namespace {

constexpr const char* kSt18NpcPort = "ST18 NPC"; // the area of its port-gap errors

enum class NpcState : uint8_t {
    Init = 0, // exe 0x443bb0
    Main = 1, // exe 0x443c30
};
enum class NpcSub : uint8_t {
    StandUnused = 0, // exe 0x443cf0 (the table's first entry; the class enters 1)
    Stand = 1,       // exe 0x443cf0
    Talk = 2,       // exe 0x443de0
    FacePlayer = 3, // exe 0x443ec0
    Bumped = 4,     // exe 0x443f50
};

constexpr uint8_t kAnimStand = 0, kAnimTalk = 4, kAnimSettle = 5, kNoAnim = 0xff;
constexpr uint8_t kHeadHoldTicks = 0x3c;
constexpr int kPlayerNear = 0x100;
constexpr uint32_t kFrontWindow = 0x400;
constexpr int kBumperGone = 0x180;
constexpr int16_t kBumpedTicks = 0x1e;
constexpr int kHitHeight = 0x60; // the hit entry's centre above the feet

const DataTable<CollisionBoxRecord> st18NpcCollisionBox{TableId::St18NpcCollisionBox};
const DataTable<InteractionBoxRecord> st18NpcTalkBox{TableId::St18NpcTalkBox};

// exe 0x443bb0: the model, the boxes, the class block cleared; standing
void npcInit(AnimatedActor& actor) {
    actor.enterState(uint8_t(NpcState::Main));
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
        freeActor(actor);                                     // exe 0x5e1490
        return;
    }
    actor.prevAnim = kNoAnim;
    actor.anim = kAnimStand;
    actor.velocity.clearWords();
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.actorHit = 0;
    actor.lastMover = nullptr;
    actor.collisionBox.pointAt(st18NpcCollisionBox.stateAddress());
    actor.interactionBox.pointAt(st18NpcTalkBox.stateAddress());
    actor.interaction = kInteractTalk;
    actor.clearClassBlock();
    actor.enterState(uint8_t(NpcState::Main));
    actor.subState = uint8_t(NpcSub::Stand);
}
// exe 0x443cf0: stands, looking at the player while it is close in front
void npcStand(AnimatedActor& actor, TownNpcFields& npc) {
    switch (WatchStep(actor.step)) {
    case WatchStep::Start:
        actor.anim = kAnimStand;
        npc.bits = kNpcNoBump | kNpcSolid;
        actor.enterStep(uint8_t(WatchStep::LookAhead));
        [[fallthrough]];
    case WatchStep::LookAhead:
        npc.lookMode = uint8_t(NpcLook::Ahead);
        if (npcPlayerInFront(actor, kPlayerNear, kFrontWindow)) actor.enterStep(uint8_t(WatchStep::LookAtPlayer));
        break;
    case WatchStep::LookAtPlayer:
        npc.lookMode = uint8_t(NpcLook::Player);
        if (npcPlayerGone(actor)) {
            npc.headHold = 0;
            actor.enterStep(uint8_t(WatchStep::LookAhead));
        }
        break;
    default: break;
    }
    NpcHead head = npc.head();
    npcHoldHead(head);
}
// exe 0x443de0: the event runs (the speaker animations its message asks for), then the settle animation
void npcTalk(AnimatedActor& actor, TownNpcFields& npc) {
    EventState& event = eventState();
    switch (TalkStep(actor.step)) {
    case TalkStep::Start:
        npc.bits = kNpcNoTalk | kNpcNoBump | kNpcSolid;
        actor.anim = kAnimStand;
        event.speakerAnimation = kNoSpeakerAnimation;
        npc.lookMode = uint8_t((npc.behaviour & kNpcFacesSpeaker) ? NpcLook::Player : NpcLook::Ahead);
        npc.headPitchTarget = 0;
        actor.enterStep(uint8_t(TalkStep::Talking));
        [[fallthrough]];
    case TalkStep::Talking: {
        if (event.speakerAnimation < kSpeakerAnimationCount) {
            actor.anim = event.speakerAnimation;
            event.speakerAnimation = kNoSpeakerAnimation;
        } else if (actor.animKey.flags == kKeyHold) {
            actor.anim = kAnimTalk;
        }
        int16_t v[3];
        npcLookPoint(kPlayerClass, v);   // exe 0x4431d0(0, v): slot 0, the player
        npcLookYaw(actor, npc.headYawTarget, v); // exe 0x445170
        if (event.active == 0) {
            actor.anim = kAnimSettle;
            actor.enterStep(uint8_t(TalkStep::Settling));
        }
        return;
    }
    case TalkStep::Settling:
        if (!actor.animEnded()) return;
        npc.headHold = kHeadHoldTicks;
        actor.subState = uint8_t(NpcSub::FacePlayer);
        actor.enterStep(0);
        return;
    default: return;
    }
}
// exe 0x443ec0: faces the player until it is gone, then stands
void npcFacePlayer(AnimatedActor& actor, TownNpcFields& npc) {
    if (actor.step == 0) {
        actor.anim = kAnimStand;
        npc.lookMode = uint8_t(NpcLook::Player);
        npc.bits = kNpcSolid;
        actor.enterStep(1);
    }
    NpcHead head = npc.head();
    npcHoldHead(head);
    if (npcPlayerGone(actor)) {
        npc.headHold = 0;
        actor.subState = uint8_t(NpcSub::Stand);
        actor.enterStep(0);
    }
}
// exe 0x443f50: bumped: stands for a while, until the bumper is gone, then stands
void npcBumped(AnimatedActor& actor, TownNpcFields& npc) {
    auto bumper = [&]() -> const Actor& {
        if (npc.bumpedBy == nullptr) notPorted(kSt18NpcPort, "bumped by no actor (the exe reads address 0)");
        return *npc.bumpedBy;
    };
    if (actor.step == 0) {
        actor.anim = kAnimStand;
        if (bumper().airborne != 0 || bumper().slot == kPlayerClass) npc.lookMode = uint8_t(NpcLook::Player);
        npc.bits = kNpcNoBump | kNpcSolid;
        npc.timer = kBumpedTicks;
        actor.enterStep(1);
    }
    // the exe's re-bump test (+0x6c and exe 0x444070) always fails
    if (npc.timer != 0) --npc.timer;
    if (npc.timer == 0 && uint32_t(kBumperGone) < uint32_t(distance3(bumper().pos, actor.pos))) {
        npc.headHold = 0;
        actor.subState = uint8_t(NpcSub::Stand);
        actor.enterStep(0);
    }
}
// exe 0x443c30: the talk test, the sub-state, the head
void npcMain(AnimatedActor& actor, TownNpcFields& npc) {
    // the bump test (!(+0x15c & 2) && +0x6c && exe 0x444070) always fails
    if (!(npc.bits & kNpcNoTalk) && player().talksTo(actor) && eventState().active == 0) {
        npcStartTalk(actor, npc.behaviour, npc.message); // exe 0x543af0
        actor.subState = uint8_t(NpcSub::Talk);
        actor.enterStep(0);
    }
    switch (NpcSub(actor.subState)) {
    case NpcSub::StandUnused:
    case NpcSub::Stand: npcStand(actor, npc); break;
    case NpcSub::Talk: npcTalk(actor, npc); break;
    case NpcSub::FacePlayer: npcFacePlayer(actor, npc); break;
    case NpcSub::Bumped: npcBumped(actor, npc); break;
    default: notPortedAt(kSt18NpcPort, "sub-state", actor.subState);
    }
    NpcHead head = npc.head();
    switch (NpcLook(head.lookMode)) { // exe 0x444000: the head targets (modes 1 / 2 only)
    case NpcLook::Ahead:
        head.pitchTarget = 0;
        head.yawTarget = 0;
        break;
    case NpcLook::Player: {
        int16_t v[3];
        npcLookPoint(0, v);         // exe 0x4431d0
        npcLookYaw(actor, head.yawTarget, v);   // exe 0x445170
        npcLookPitch(actor, head, v); // exe 0x4451f0
        break;
    }
    default: break;
    }
    npcHeldHeadTick(head);
    npcTurnHead(head); // exe 0x445230 / 0x445280
}
// exe 0x443aa0's body: the state, the animation, the hit entry, a push-out solid
void st18Npc(const PlayerEnv& env, AnimatedActor& actor) {
    TownNpcFields& npc = classState<TownNpcFields>(actor);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (NpcState(actor.state)) {
    case NpcState::Init: npcInit(actor); break;
    case NpcState::Main: npcMain(actor, npc); break;
    default: notPortedAt(kSt18NpcPort, "state (table 0x88ddf4 has 2 entries)", actor.state);
    }
    npc.collisionBits = 0;
    actor.pos.y.whole = int16_t(actorGround(env, actor)); // exe 0x40f190
    NpcHead head = npc.head();
    npcAnimate(actor, head);                          // exe 0x444080
    npc.hitBox.result = 0;
    npc.hitBox.previousX = actor.prevPos.x.whole;
    npc.hitBox.previousZ = actor.prevPos.z.whole;
    const int16_t pos[3] = {actor.pos.x.whole, int16_t(uint16_t(actor.pos.y.whole - kHitHeight)), actor.pos.z.whole};
    hitListSubmit(pos, static_cast<HitBoxHead&>(npc.hitBox), hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | hitRadius(0x40),
                  kHitAttackHarmless); // exe 0x5e4120
    // exe 0x5f20f0(actor, 0x30, 0x160, 0): the shadow list entry (render output)
    if (npc.bits & kNpcSolid) {
        actorCollisionSubmitSolid(actor, SolidType::PushOut, kNoBone, 0); // exe 0x414050
    } else {
        actor.lastMover = nullptr;
    }
    actor.actorHit = 0;
}

} // namespace

void st18NpcUpdate(const PlayerEnv& env, uint32_t address) {
    st18Npc(env, animatedActorAt(address));
}

} // namespace od::port
