// The relic class 0x4dfc70 (see abandoned_mine_relic.hpp). States [0x89596c]: 0 init 0x4dfd50, 1 0x4dfe10 (sub-states
// [0x895978]: 0 0x4dfe30 wait for the examine, 1 0x4dfe80 the talk), 2 0x468fa0 (freed). The shadow (exe 0x5f20f0) is
// render output.
#include "content/yosyonke/abandoned_mine_relic.hpp"
#include "content/yosyonke/abandoned_mine_relic_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"

namespace od::port {

using namespace abandoned_mine_relic_state;

namespace {

constexpr const char* kRelicPort = "ruin relic port"; // the area of its port-gap errors


enum class RelicState : uint8_t {
    Init = 0,  // exe 0x4dfd50
    Main = 1,  // exe 0x4dfe10
    Freed = 2, // exe 0x468fa0
};
enum class RelicSub : uint8_t {
    Wait = 0, // exe 0x4dfe30
    Talk = 1, // exe 0x4dfe80
};
enum class RelicTalkStep : uint8_t { Start = 0, Talking = 1, Ending = 2 };

const DataTable<CollisionBoxRecord> relicCollisionBox{TableId::AbandonedMineRelicCollisionBox};
const DataTable<InteractionBoxRecord> relicTalkBox{TableId::AbandonedMineRelicTalkBox};
constexpr int16_t kRelicScale = 0x200;
constexpr uint32_t kTalkEvent = uint32_t(EventMode::Talk) << 16 | kEventWaitsWindow | kEventNoTurn;

void relicInit(AnimatedActor& actor, RuinRelicFields& f) { // exe 0x4dfd50
    if (!bindActorModel(actor, actor.variant)) {
        freeActor(actor);
        return;
    }
    actor.clearClassBlock();
    actor.anim = f.cueAnim;
    actor.collisionBox.pointAt(relicCollisionBox.stateAddress());
    actor.interactionBox.pointAt(relicTalkBox.stateAddress());
    actor.interaction = kInteractTalk;
    actor.prevAnim = 0xff;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.maxHitPoints = 1;
    actor.hitPoints = 1;
    actor.velocity.setXY(0, 0);
    actor.velocity.setZSpare(0, 0);
    actor.spin.setXY(0, 0);
    actor.spin.setZSpare(0, 0);
    actor.scale.z = kRelicScale;
    actor.scale.y = kRelicScale;
    actor.scale.x = kRelicScale;
    f.tracks.init(0, 0); // exe 0x5e33a0(block, 0, 0)
    f.clearTalkWord();
    actor.enterState(uint8_t(RelicState::Main));
}

void relicWait(AnimatedActor& actor, RuinRelicFields& f) { // exe 0x4dfe30
    if (f.talkBits & kRelicTalking) return;
    if (!player().talksTo(actor) || eventState().active != 0) return;
    eventStart(&actor.pos, &actor.rotation, kTalkEvent, 0, f.message); // exe 0x543af0
    actor.subState = uint8_t(RelicSub::Talk);
    actor.enterStep(0);
}

void relicTalk(AnimatedActor& actor, RuinRelicFields& f) { // exe 0x4dfe80
    EventState& ev = eventState();
    switch (RelicTalkStep(actor.step)) {
    case RelicTalkStep::Start:
        f.talkBits |= kRelicTalking;
        actor.anim = 0;
        ev.speakerAnimation = kNoSpeakerAnimation;
        actor.enterStep(uint8_t(RelicTalkStep::Talking));
        [[fallthrough]];
    case RelicTalkStep::Talking:
        if (ev.speakerAnimation != kNoSpeakerAnimation) {
            actor.anim = ev.speakerAnimation;
            ev.speakerAnimation = kNoSpeakerAnimation;
        } else if (actor.animKey.flags == kKeyHold) {
            actor.anim = 0;
        }
        if (ev.active == 0) {
            actor.anim = 0;
            actor.enterStep(uint8_t(RelicTalkStep::Ending));
        }
        return;
    case RelicTalkStep::Ending:
        if (!actor.animEnded()) return;
        f.talkBits &= uint8_t(~kRelicTalking);
        actor.subState = uint8_t(RelicSub::Wait);
        actor.enterStep(0);
        return;
    default: return;
    }
}

void relic(const PlayerEnv& env, AnimatedActor& actor) { // exe 0x4dfc70
    RuinRelicFields& f = classState<RuinRelicFields>(actor);
    actor.savePreviousPose();
    actor.prevAnim = actor.anim;
    f.frameCopy = uint8_t(actor.animKey.frame);
    switch (RelicState(actor.state)) {
    case RelicState::Init: relicInit(actor, f); break;
    case RelicState::Main: // exe 0x4dfe10
        switch (RelicSub(actor.subState)) {
        case RelicSub::Wait: relicWait(actor, f); break;
        case RelicSub::Talk: relicTalk(actor, f); break;
        default: notPortedAt(kRelicPort, "0x4dfc70 sub-state", actor.subState);
        }
        break;
    case RelicState::Freed: freeActor(actor); break; // exe 0x468fa0
    default: notPortedAt(kRelicPort, "0x4dfc70 state", actor.state);
    }
    actor.pos.y.whole = int16_t(actorGround(env, actor)); // exe 0x40f190 (also after a free, as the exe does)
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim != f.cueAnim) actor.anim = f.cueAnim;
    textureTracksTick(actor, f.tracks); // exe 0x5e34f0
    if (actor.anim == actor.prevAnim) animStepPose(actor);
    else animSetPose(actor, actor.anim, f.animKeyStart);
    actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0); // exe 0x414050(actor, 1, 0xffff, 0)
    // exe 0x5f20f0(actor, 0x30, 0x160, 0): the shadow (render output)
}

} // namespace

void abandonedMineRelicUpdate(const PlayerEnv& env, uint32_t actor) { relic(env, animatedActorAt(actor)); }

} // namespace od::port
