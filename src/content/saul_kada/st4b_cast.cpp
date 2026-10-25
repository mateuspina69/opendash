// The ST4B cast of script 0x59 (hypothesis names), one routine per exe routine.
//
// exe 0x47dd10 (pool 0x20 type 0x1a): a talking cast member. Its animation by variant (1 / 5 / 6); the script's step
// ticks start / stop its talking and switch its animations; its pose steps once let (else it holds); exe 0x47dfe0 flaps
// its mouth (parts 1 / 10 / 11) every 4 ticks while it talks. States [0x8912e0]: 0 bind 0x47ddd0, 1 by variant
// 0x47de80 [0x8912ec], 2 freed 0x468fa0, 3..5 the variants' routines.
// exe 0x5b23d0 (static type 0x40): the item Megaman holds -- variant 0 placed at his hand's point + R(his rotation) *
// (-0x200, -0x40, -0x100), facing back (his yaw + 0x800), rolled 0xe00 (0xd00 with its parameter); states 3 / 4: the
// cutscene cue table 0x8d0788. States [0x8d076c]: 0 bind 0x5b23f0, 1 follow 0x5b2500, 2 / 5 freed 0x5afdd0, 3 the
// cutscene bind 0x5b2630, 4 the cutscene cue 0x5b26b0, 6 nothing.
#include "content/saul_kada/st4b_cast.hpp"
#include "content/saul_kada/st4b_cast_state.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/cutscene_actor.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st4b_cast_state;

namespace {

// ---- exe 0x47dd10: the talking cast member --------------------------------------------------------------------------
// [0x8912e0]
enum class TalkerState : uint8_t { Bind = 0, ByVariant = 1, Free = 2, FirstRoutine = 3, LastRoutine = 5 };
// [0x8912ec]: the variants' routines
enum class TalkerRoutine : uint8_t { Main = 0, Short = 1, Brief = 2, Count = 3 };
// the talking (its step): the mouth flaps while Talking, closes at Stop
enum class TalkStep : uint8_t { Silent = 0, Talking = 1, Stop = 2 };
constexpr size_t kMouthClosedPart = 1, kMouthOpenPartA = 10, kMouthOpenPartB = 11;
constexpr uint8_t kVariantAnims[3] = {1, 5, 6};

void talk(AnimatedActor& actor, TalkStep step) { actor.step = uint8_t(step); }

// exe 0x47dea0 (variant 0)
void mainRoutine(AnimatedActor& actor, TalkerFields& talker) {
    const int32_t tick = scriptState().stepTick;
    if (actor.subState == 0) {
        if (tick == 0x94) talker.stepping = 1;
        else if (tick == 0xb2) talk(actor, TalkStep::Talking);
        else if (tick == 0x130) {
            talk(actor, TalkStep::Stop);
            actor.subState = 1;
        }
    } else if (actor.subState == 1) {
        if (scriptState().stepSub == 1 && tick == 1) {
            actor.pos.x.whole = 0x100;
            actor.pos.z.whole = int16_t(0xffa0);
            actor.rotation.y = 0x800;
            actor.subState = 2;
        }
    } else if (actor.subState == 2) {
        if (tick == 0x14) {
            actor.anim = 2;
            talk(actor, TalkStep::Talking);
        } else if (tick == 0x1a2) {
            actor.anim = 3;
            talk(actor, TalkStep::Talking);
        } else if (tick == 0x3b4) {
            actor.anim = 4;
            talk(actor, TalkStep::Talking);
        } else if (tick == 0xad || tick == 0x224 || tick == 0x404) {
            talk(actor, TalkStep::Stop);
        }
    }
}
// exe 0x47df70 (variant 1)
void shortRoutine(AnimatedActor& actor, TalkerFields& talker) {
    const int32_t tick = scriptState().stepTick;
    if (tick == 0x14) {
        talker.stepping = 1;
        talk(actor, TalkStep::Talking);
    } else if (tick == 0x3e) {
        talk(actor, TalkStep::Stop);
    }
}
// exe 0x47dfa0 (variant 2)
void briefRoutine(AnimatedActor& actor, TalkerFields& talker) {
    const int32_t tick = scriptState().stepTick;
    if (tick == 0x14) talker.stepping = 1;
    else if (tick == 0x46) talk(actor, TalkStep::Talking);
    else if (tick == 0x68) talk(actor, TalkStep::Stop);
}
void runRoutine(AnimatedActor& actor, TalkerFields& talker, TalkerRoutine routine) {
    switch (routine) {
    case TalkerRoutine::Main: mainRoutine(actor, talker); return;
    case TalkerRoutine::Short: shortRoutine(actor, talker); return;
    case TalkerRoutine::Brief: briefRoutine(actor, talker); return;
    default: notPortedAt(kStoryPort, "0x47dd10 cue fn", uint32_t(routine));
    }
}
// exe 0x47dfe0: the mouth flaps every 4 ticks while talking; closed at the stop
void flapMouth(AnimatedActor& actor) {
    uint8_t& closed = actor.parts[kMouthClosedPart].flags;
    uint8_t& openA = actor.parts[kMouthOpenPartA].flags;
    uint8_t& openB = actor.parts[kMouthOpenPartB].flags;
    if (TalkStep(actor.step) == TalkStep::Talking) {
        if (actor.stepCounter & 4) {
            closed |= kPartHidden;
            openA |= kPartHidden;
            openB &= uint8_t(~kPartHidden);
        } else {
            closed &= uint8_t(~kPartHidden);
            openA |= kPartHidden;
            openB |= kPartHidden;
        }
        ++actor.stepCounter;
    } else if (TalkStep(actor.step) == TalkStep::Stop) {
        closed &= uint8_t(~kPartHidden);
        openA |= kPartHidden;
        openB |= kPartHidden;
        actor.step = uint8_t(TalkStep::Silent);
        actor.stepCounter = 0;
    }
}
// exe 0x47ddd0
void bindTalker(AnimatedActor& actor, TalkerFields& talker) {
    if (!bindActorModel(actor, 0)) {
        freeActor(actor);
        return;
    }
    actor.flags |= kActorDrawn;
    if (actor.variant <= 2) actor.anim = kVariantAnims[actor.variant];
    actor.prevAnim = kNoAnimShown;
    animSetPose(actor, actor.anim, 0); // exe 0x5e1c30
    actor.velocity.x = 0;
    actor.velocity.y = 0;
    actor.velocity.z = int16_t(0xff00);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    actor.collisionBox = nullptr;
    talker.word = 0;
    talker.stepping = 0;
    actor.subState = 0;
    actor.restartStep();
    actor.state = uint8_t(TalkerState::ByVariant);
}
void talkerCast(AnimatedActor& actor) {
    TalkerFields& talker = classState<TalkerFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.tint = kTintNeutral;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    const uint8_t state = actor.state;
    if (TalkerState(state) == TalkerState::Bind) {
        bindTalker(actor, talker);
    } else if (TalkerState(state) == TalkerState::ByVariant) { // 0x47de80 [0x8912ec]
        if (actor.variant >= uint8_t(TalkerRoutine::Count)) {
            notPortedAt(kStoryPort, "0x47de80 variant (table 0x8912ec)", actor.variant);
            return;
        }
        runRoutine(actor, talker, TalkerRoutine(actor.variant));
    } else if (TalkerState(state) == TalkerState::Free) {
        freeActor(actor); // 0x468fa0
    } else if (state <= uint8_t(TalkerState::LastRoutine)) {
        runRoutine(actor, talker, TalkerRoutine(state - uint8_t(TalkerState::FirstRoutine)));
    } else {
        notPortedAt(kStoryPort, "0x47dd10 state (table 0x8912e0)", state);
        return;
    }
    if (actor.flags & kActorLive) {
        if (actor.anim != actor.prevAnim) animSetPose(actor, actor.anim, 0);
        else if (talker.stepping) animStepPose(actor); // exe 0x5e1e80
    }
    flapMouth(actor);
}

// ---- exe 0x5b23d0: the item Megaman holds -------------------------------------------------------------------------------
// [0x8d076c]
enum class ItemState : uint8_t {
    Bind = 0, Follow = 1, Free = 2, CutsceneBind = 3, CutsceneCue = 4, FreeToo = 5, Idle = 6,
};
enum class ItemVariant : uint8_t { InHand = 0 };
// The item's parameter: rolled less
struct HeldItemParams {
    explicit HeldItemParams(const StaticActor& actor) : HeldItemParams(const_cast<StaticActor&>(actor)) {}
    explicit HeldItemParams(StaticActor& actor) : lessRoll(actor.params[0]) {}
    uint8_t& lessRoll;
};
// Its own bytes: a word cleared at the bind; the spin's spare lane cleared by the cutscene bind
struct HeldItemFields {
    explicit HeldItemFields(const StaticActor& actor) : HeldItemFields(const_cast<StaticActor&>(actor)) {}
    explicit HeldItemFields(StaticActor& actor) : spinSpare(actor.spin.spare), word(actor.hitBox.previousX) {}
    int16_t& spinSpare;
    FieldAlias<uint16_t, int16_t> word;
};
constexpr uint32_t kHandBone = kRightHandBone;

// the item at Megaman's hand: the hand's point + R(his rotation) * (-0x200, -0x40, -0x100) (exe 0x5e3970), his yaw +
// 0x800, the roll
void placeInHand(StaticActor& actor) {
    Player& p = player();
    int32_t offset[3];
    rotateVector(p.rotation, -0x200, -0x40, -0x100, offset); // exe 0x5e3970
    const ShortVec3& hand = p.bonePoints[kHandBone];
    actor.pos.x = int32_t((uint32_t(int32_t(hand.x)) << 16) + uint32_t(offset[0]));
    actor.pos.y = int32_t((uint32_t(int32_t(hand.y)) << 16) + uint32_t(offset[1]));
    actor.pos.z = int32_t((uint32_t(int32_t(hand.z)) << 16) + uint32_t(offset[2]));
    actor.rotation.y = int16_t(uint16_t(p.rotation.y) + 0x800u);
    actor.rotation.z = HeldItemParams(actor).lessRoll ? int16_t(0xd00) : int16_t(0xe00);
}
void heldItem(StaticActor& actor) {
    HeldItemFields item(actor);
    switch (ItemState(actor.state)) {
    case ItemState::Bind: // 0x5b23f0
        if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
            freeActor(actor);
            return;
        }
        actor.flags |= kActorDrawn;
        item.word = 0;
        actor.velocity.clearWords();
        if (ItemVariant(actor.variant) == ItemVariant::InHand) placeInHand(actor);
        actor.subState = 0;
        actor.state = uint8_t(ItemState::Follow);
        return;
    case ItemState::Follow: // 0x5b2500
        if (ItemVariant(actor.variant) == ItemVariant::InHand) placeInHand(actor);
        return;
    case ItemState::Free:
    case ItemState::FreeToo: freeActor(actor); return; // 0x5afdd0
    case ItemState::CutsceneBind: // 0x5b2630
        if (!bindActorModel(actor, 0)) {
            freeActor(actor);
            return;
        }
        actor.flags |= kActorDrawn;
        actor.clearClassBlock();
        actor.velocity.z = 0;
        actor.velocity.y = 0;
        actor.velocity.x = 0;
        actor.acceleration.z = 0;
        actor.acceleration.y = 0;
        actor.acceleration.x = 0;
        actor.spin.y = 0;
        actor.spin.x = 0;
        item.spinSpare = 0;
        actor.spin.z = 0;
        [[fallthrough]];
    case ItemState::CutsceneCue: { // 0x5b26b0: [[0x8d0788 + variant * 4] + the cue * 4] (variant 0, cue 0: nothing)
        const uint8_t cue = scriptState().part<CastTimelineStep>().stepSub;
        if (actor.variant != 0 || int8_t(cue) != 0) notPortedAt(kStoryPort, "0x5b26b0 cue (table 0x8d0788)", cue);
        if (ItemState(actor.state) == ItemState::CutsceneBind) actor.enterState(uint8_t(ItemState::Follow));
        return;
    }
    case ItemState::Idle: return; // 0x52ef00
    default: notPortedAt(kStoryPort, "0x5b23d0 state (table 0x8d076c)", actor.state);
    }
}

} // namespace

void talkerUpdate(const PlayerEnv&, uint32_t actor) { talkerCast(animatedActorAt(actor)); }
void heldItemUpdate(const PlayerEnv&, uint32_t actor) { heldItem(staticActorAt(actor)); }

} // namespace od::port
