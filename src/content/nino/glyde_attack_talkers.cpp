// Nino island (ST17 / ST18), the cutscenes around Glyde's last attack: the pool-0x20 type-0x69 cast. Both classes are
// pure script puppets: their animation and their two texture tracks (the face cells of the texture-page words: eyes /
// mouth) keyed on the script's step tick. Models from the script's alternate type-1 bank. Who the model is was not
// confirmed from the exe alone (a talking cast member with a face; hypothesis only).
//   0x4f07b0  ST17 (table 0x8e2618) type 0x69: records 0x8a6a9c (variant 0) / 0x8a6ab0 (variant 1) of script 0x39's
//             camera script 0x8a6c1c. States [0x896960]: 0 init, 1 by the variant [0x896968]: 0 the cues, 1 none.
//   0x4f0a50  ST18 (table 0x8e3108) type 0x69: records of scripts 0x3a (camera 0x8a6efc) and 0x3b (camera 0x8a7208).
//             States [0x896a04]: 0 init, 1 by the variant [0x896a10]: 0 / 1 / 2 the cues of each.
// Render output left out: the shadow list entry of 0x4f0a50's variant 2 (exe 0x5f2220).
#include "content/nino/glyde_attack_talkers.hpp"
#include "content/nino/glyde_attack_talkers_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/cutscene_actor.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace glyde_attack_talkers_state;

namespace {


enum class TalkerState : uint8_t { Init = 0, Cue = 1 };

// the model parts the ST18 talker swaps on animation 3 (shown / hidden)
constexpr size_t kSwapShownPart = 4, kSwapHiddenPart = 17;

int32_t stepTick() { return scriptState().stepTick; }

// the copies at the top of both updates
void savePose(AnimatedActor& actor) {
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
}

// the common actor fields of both inits (after the model bound)
void initFields(AnimatedActor& actor, uint32_t box) {
    actor.prevAnim = kNoAnimShown;
    actor.maxHitPoints = 1;
    actor.hitPoints = 1;
    actor.velocity.clearWords();
    actor.spin.setXY(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.collisionBox.pointAt(box);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
}

// the common tail: live -> the texture tracks ticked, the animation stepped (the same) or started at key 0
void animateTail(AnimatedActor& actor, TalkerFields& fields) {
    if (!(actor.flags & kActorLive)) return;
    textureTracksTick(actor, fields.tracks); // exe 0x5e34f0
    if (actor.anim == actor.prevAnim) animStepPose(actor); // exe 0x5e1e80
    else animSetPose(actor, actor.anim, 0);               // exe 0x5e1c30
}

// ---- 0x4f07b0 (ST17, script 0x39) ----------------------------------------------------------------------------------
const DataTable<CollisionBoxRecord> st17TalkerBox{TableId::GlydeAttackTalkersSt17TalkerBox};

// exe 0x4f0850
void st17TalkerInit(AnimatedActor& actor, TalkerFields& fields) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);           // exe 0x5e1490
        return;
    }
    actor.flags |= kActorDrawn;
    actor.anim = 0;
    initFields(actor, st17TalkerBox.stateAddress());
    actor.clearClassBlock();                      // exe 0x40dc40
    fields.tracks.init(stateAddress(TableId::GlydeAttackTalkersSt17TalkerEyes), stateAddress(TableId::GlydeAttackTalkersSt17TalkerMouth));       // exe 0x5e33a0
    fields.tracks.first.select(0, 0);             // exe 0x5e33d0
    fields.tracks.second.select(0, 0);            // exe 0x5e33f0
    actor.enterState(uint8_t(TalkerState::Cue));
}
// exe 0x4f0920 (variant 0): the cues on the step tick
void st17TalkerCues(AnimatedActor& actor, TalkerFields& fields) {
    switch (stepTick()) {
    case 0xf:
        actor.anim = 1;
        fields.tracks.first.select(0, 0);
        fields.tracks.second.select(1, 0);
        break;
    case 0x70:
        actor.anim = 2;
        fields.tracks.first.select(1, 0);
        fields.tracks.second.select(2, 0);
        break;
    case 0x64:
    case 0xaf: fields.tracks.second.select(0, 0); break;
    default: break;
    }
}
// exe 0x4f07b0
void st17Talker(AnimatedActor& actor) {
    TalkerFields& fields = classState<TalkerFields>(actor);
    savePose(actor);
    switch (TalkerState(actor.state)) {
    case TalkerState::Init: st17TalkerInit(actor, fields); break;
    case TalkerState::Cue: // exe 0x4f0900
        switch (actor.variant) {
        case 0: st17TalkerCues(actor, fields); break;
        case 1: break; // exe 0x52ef00 (ret)
        default: notPortedAt(kStoryPort, "0x4f07b0 variant (table 0x896968 has 2 entries)", actor.variant);
        }
        break;
    default: notPortedAt(kStoryPort, "0x4f07b0 state (table 0x896960 has 2 entries)", actor.state);
    }
    animateTail(actor, fields);
}

// ---- 0x4f0a50 (ST18, scripts 0x3a / 0x3b) --------------------------------------------------------------------------
const DataTable<CollisionBoxRecord> st18TalkerBox{TableId::GlydeAttackTalkersSt18TalkerBox};
const DataTable<ScalarTable<uint8_t, 3>> st18TalkerAnims{TableId::GlydeAttackTalkersSt18TalkerAnims}; // {0, 0, 4}

// exe 0x4f0b30: the swapped part's hide bit kept, the other part hidden
void st18TalkerInit(AnimatedActor& actor, TalkerFields& fields) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);           // exe 0x5e1490
        return;
    }
    const uint8_t shownFlags = uint8_t(actor.parts[kSwapShownPart].flags & ~kPartHidden);
    actor.parts[kSwapHiddenPart].flags |= kPartHidden;
    actor.anim = st18TalkerAnims[actor.variant];
    initFields(actor, st18TalkerBox.stateAddress());
    actor.parts[kSwapShownPart].flags = shownFlags;
    actor.clearClassBlock();                      // exe 0x40dc40
    fields.tracks.init(stateAddress(TableId::GlydeAttackTalkersSt18TalkerEyes), stateAddress(TableId::GlydeAttackTalkersSt18TalkerMouth));       // exe 0x5e33a0
    fields.tracks.first.select(0, 0);             // exe 0x5e33d0
    fields.tracks.second.select(0, 0);            // exe 0x5e33f0
    if (actor.variant == 2) {
        fields.tracks.first.select(1, 0);
        fields.tracks.second.select(3, 0);
    }
    actor.enterState(uint8_t(TalkerState::Cue));
}
// exe 0x4f0c40 (variant 0)
void st18TalkerCues0(AnimatedActor& actor, TalkerFields& fields) {
    if (actor.step == 0) {
        actor.anim = 0;
        actor.step = 1;
    }
    const int32_t t = stepTick();
    if (t == 0x1e) {
        actor.anim = 1;
        fields.tracks.second.select(1, 0);
    } else if (t == 0xaf) {
        fields.tracks.second.select(0, 0);
    }
}
// exe 0x4f0ca0 (variant 1; switches 0x4f0d68 / 0x4f0d58 and 0x4f0df8 / 0x4f0de8): on animation 3's key 0x1c the part
// pair swaps (the first hidden, the second shown)
void st18TalkerCues1(AnimatedActor& actor, TalkerFields& fields) {
    const int32_t t = stepTick();
    if (t > 0xfa) {
        switch (t) {
        case 0x186:
            actor.anim = 3;
            fields.tracks.second.select(2, 0);
            break;
        case 0x1d6: fields.tracks.second.select(2, 0); break;
        case 0x1b8:
        case 0x23a: fields.tracks.second.select(0, 0); break;
        default: break;
        }
    } else if (t == 0xfa) {
        fields.tracks.second.select(0, 0);
    } else {
        switch (t) {
        case 0xf:
            actor.anim = 2;
            fields.tracks.first.select(0, 0);
            fields.tracks.second.select(2, 0);
            break;
        case 0x3c:
        case 0x82: fields.tracks.second.select(0, 0); break;
        case 0x46:
        case 0x8c: fields.tracks.second.select(2, 0); break;
        default: break;
        }
    }
    if (actor.anim == 3 && actor.keyIndex == 0x1c) {
        actor.parts[kSwapShownPart].flags |= kPartHidden;
        actor.parts[kSwapHiddenPart].flags &= uint8_t(~kPartHidden);
    }
}
// exe 0x4f0eb0 (variant 2)
void st18TalkerCues2(TalkerFields& fields) {
    if (stepTick() == 0x7d) fields.tracks.second.select(4, 0);
}
// exe 0x4f0a50
void st18Talker(AnimatedActor& actor) {
    TalkerFields& fields = classState<TalkerFields>(actor);
    savePose(actor);
    switch (TalkerState(actor.state)) {
    case TalkerState::Init: st18TalkerInit(actor, fields); break;
    case TalkerState::Cue: // exe 0x4f0c20
        switch (actor.variant) {
        case 0: st18TalkerCues0(actor, fields); break;
        case 1: st18TalkerCues1(actor, fields); break;
        case 2: st18TalkerCues2(fields); break;
        default: notPortedAt(kStoryPort, "0x4f0a50 variant (table 0x896a10 has 3 entries)", actor.variant);
        }
        break;
    default: notPortedAt(kStoryPort, "0x4f0a50 state (table 0x896a04 has 2 entries)", actor.state);
    }
    animateTail(actor, fields);
}

} // namespace

void st17TalkerUpdate(const PlayerEnv&, uint32_t actor) { st17Talker(animatedActorAt(actor)); }
void st18TalkerUpdate(const PlayerEnv&, uint32_t actor) { st18Talker(animatedActorAt(actor)); }

} // namespace od::port
