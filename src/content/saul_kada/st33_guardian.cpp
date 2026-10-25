// The guardian figures of the ST33 ruin (pool 0x20 type 0x45, ST33 sub 0, story 13, exe 0x4ccb70; hypothesis names),
// one routine per exe routine.
//
// Two kinds by the bind they run: the stomper (bind 0x4ccc30, run 0x4ccce0: animation 0x27 then a stomp (a shake
// 0x2000, sound 0x149), a jump (y -0x1fe0), the animation chains 0x0b / 0x20 (sound 0x150, two eye glows: stage
// effect type 6), its eye parts hidden / shown by the animation key's events) and the glower (bind 0x4cd010, run
// 0x4cd140: animation 0x0b with two eye glows kept, then 8 / 9 / 0x0a, the glows lit once its signal is set, a shake
// 0x6000 + sound 0x14d / 0x14c at the keys 0xf / 0x15).
// States [0x895178]: 0 bind the stomper, 1 by sub-state [0x895184], 2 freed, 3 nothing, 4 the stomper, 5 bind the
// glower, 6 the glower by sub-state [0x895198], 7 freed, 8 the glower. Sub-states [0x895184]: 0 nothing, 1 the
// stomper, 2 bind the glower, 3 the glower by sub-state, 4 freed, 5 the glower.
#include "content/saul_kada/st33_guardian.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/cutscene_actor.hpp"
#include "game/port_gap.hpp"
#include "game/state_array.hpp"
#include "game/story.hpp"

namespace od::port {

namespace {

// the exe routines the state / sub-state tables dispatch to
enum class GuardianRoutine : uint8_t {
    Idle,        // exe 0x52ef00
    Free,        // exe 0x468fa0
    BindStomper, // exe 0x4ccc30
    Stomper,     // exe 0x4ccce0
    BindGlower,  // exe 0x4cd010
    GlowerBySub, // exe 0x4cd120
    Glower,      // exe 0x4cd140
};
// [0x895178]
constexpr GuardianRoutine kStateRoutines[9] = {
    GuardianRoutine::BindStomper, GuardianRoutine::Idle /* 1: by sub-state */, GuardianRoutine::Free,
    GuardianRoutine::Idle,        GuardianRoutine::Stomper,                    GuardianRoutine::BindGlower,
    GuardianRoutine::GlowerBySub, GuardianRoutine::Free,                       GuardianRoutine::Glower,
};
constexpr uint8_t kBySubState = 1;
// [0x895184]
constexpr GuardianRoutine kSubRoutines[6] = {
    GuardianRoutine::Idle,        GuardianRoutine::Stomper, GuardianRoutine::BindGlower,
    GuardianRoutine::GlowerBySub, GuardianRoutine::Free,    GuardianRoutine::Glower,
};
enum class StomperStep : uint8_t { Start = 0, Stomp = 1, Jump = 2, Land = 3, Roar = 4, Glare = 5, Close = 6 };
constexpr uint8_t kGuardianHoldPose = 0x01;
constexpr uint8_t kGuardianShaking = 0x02;
// the animation key's events the stomper keys on
constexpr uint8_t kEventStomp = 0x20, kEventCloseEyes = 0x40, kEventOpenEyes = 0x80;
constexpr size_t kLeftEyePart = 4, kRightEyePart = 5;
constexpr int16_t kGuardianScale = 0x480;
constexpr uint8_t kGlowType = 6;
constexpr int16_t kGlowSpare = 3;
constexpr uint8_t kGlowLit = 1; // a glow's sub-state once lit
constexpr uint16_t kStompSound = 0x149, kRoarSound = 0x150, kQuakeSoundA = 0x14d, kQuakeSoundB = 0x14c;
constexpr int16_t kStomperEyes[2][3] = {{0x40, -0xc, -0xd}, {-0x40, -0xc, -0xd}};
constexpr int16_t kGlowerEyes[2][3] = {{0x18, -0x3c, -0x36}, {-0x18, -0x3c, -0x36}};

// two eye glows (stage effect type 6) riding the guardian; kept where `keep` says (the glower's)
void spawnGlows(const AnimatedActor& actor, uint8_t variant, uint8_t life, const int16_t (&points)[2][3],
                StateArray<uint32_t, 2>* keep) {
    for (int k = 0; k < 2; ++k) {
        const uint32_t slot = allocActorA0(kActorStageClass); // exe 0x5e13a0
        if (!slot) {
            if (k == 0) continue;
            return;
        }
        EffectActor& glow = effectActorAt(slot);
        glow.flags = kActorLive;
        glow.type = kGlowType;
        glow.variant = variant;
        glow.setParamsWord(actor.at); // its owner
        glow.step = life;
        glow.stepCounter = 0;
        glow.pos.x = points[k][0];
        glow.pos.y = points[k][1];
        glow.pos.z = points[k][2];
        glow.setRecordYaw(kGlowSpare);
        if (keep) (*keep)[size_t(k)] = slotIdentityWord(glow);
    }
}
// the bind both kinds share
void bindCommon(AnimatedActor& actor) {
    actor.flags |= kActorScenery | kActorDrawn;
    actor.prevAnim = kNoAnimShown;
    actor.scale.x = kGuardianScale;
    actor.scale.y = kGuardianScale;
    actor.scale.z = kGuardianScale;
    actor.collisionBox = nullptr;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.velocity.clearWords();
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
}
// exe 0x4ccc30
void bindStomper(AnimatedActor& actor, GuardianFields& guardian) {
    if (!bindActorModel(actor, 0)) {
        freeActor(actor);
        return;
    }
    bindCommon(actor);
    actor.anim = 0;
    guardian.animKey = 0;
    guardian.flags = kGuardianHoldPose;
    guardian.timer = 0;
    actor.state = 1;
    actor.subState = 0;
}
// exe 0x4cd010
void bindGlower(AnimatedActor& actor, GuardianFields& guardian) {
    if (!bindActorModel(actor, 0)) {
        freeActor(actor);
        return;
    }
    bindCommon(actor);
    actor.anim = 0xb;
    guardian.glowerAnimKey = 0;
    guardian.glowerHoldPose = 0;
    guardian.glowByteB = 0;
    actor.state = 1;
    actor.subState = 0;
    spawnGlows(actor, 2, 0x1e, kGlowerEyes, &guardian.glows);
}
void startAnim(AnimatedActor& actor, GuardianFields& guardian, uint8_t anim) {
    actor.anim = anim;
    guardian.animKey = 0;
}
// exe 0x4ccce0
void stomper(AnimatedActor& actor, GuardianFields& guardian) {
    switch (StomperStep(actor.step)) {
    case StomperStep::Start:
        startAnim(actor, guardian, 0x27);
        guardian.flags &= uint8_t(~kGuardianHoldPose);
        actor.step = uint8_t(StomperStep::Stomp);
        break;
    case StomperStep::Stomp:
        if (actor.animKey.events & kEventStomp) {
            guardian.flags |= kGuardianShaking;
            guardian.timer = 0x1e;
            cameraShake(true, 0x2000, 0);          // exe 0x40ea80
            soundPlayAtPos(kStompSound, actor.pos); // exe 0x5d5380
        }
        break;
    case StomperStep::Jump:
        startAnim(actor, guardian, 0x1a);
        integrateVelocity(actor, 0, 0, -0x1fe0); // exe 0x5e3690
        actor.step = uint8_t(StomperStep::Land);
        break;
    case StomperStep::Land:
        if (actor.animEnded()) {
            startAnim(actor, guardian, 0xb);
            actor.step = uint8_t(StomperStep::Stomp);
        }
        break;
    case StomperStep::Roar:
        if (actor.animEnded()) {
            startAnim(actor, guardian, 0x20);
            soundPlayAtPos(kRoarSound, actor.pos);
            actor.step = uint8_t(StomperStep::Glare);
            guardian.timer = 0x5a;
            GuardianParams(actor).signal = 1;
            spawnGlows(actor, 0, 0x28, kStomperEyes, nullptr);
        }
        break;
    case StomperStep::Glare:
        if (actor.animEnded()) {
            if (guardian.timer != 0) {
                --guardian.timer;
            } else {
                startAnim(actor, guardian, 0x28);
                actor.step = uint8_t(StomperStep::Close);
                actor.stepCounter = 0;
            }
        } else if (actor.animKey.events & kEventCloseEyes) {
            actor.parts[kLeftEyePart].flags |= kPartHidden;
            actor.parts[kRightEyePart].flags |= kPartHidden;
        }
        break;
    case StomperStep::Close:
        if (actor.animEnded()) {
            guardian.flags &= uint8_t(~kGuardianHoldPose);
            actor.step = uint8_t(StomperStep::Stomp);
        } else if (actor.animKey.events & kEventOpenEyes) {
            if (actor.stepCounter == 0) {
                actor.stepCounter = 1;
                actor.parts[kLeftEyePart].flags &= uint8_t(~kPartHidden);
            } else {
                actor.parts[kRightEyePart].flags &= uint8_t(~kPartHidden);
            }
        }
        break;
    default: break;
    }
    if (guardian.flags & kGuardianShaking) {
        if (guardian.timer != 0) --guardian.timer;
        else cameraShake(true, 0, 0);
    }
}
void startGlowerAnim(AnimatedActor& actor, GuardianFields& guardian, uint8_t anim) {
    actor.anim = anim;
    guardian.glowerAnimKey = 0;
}
// exe 0x4cd140
void glower(AnimatedActor& actor, GuardianFields& guardian) {
    switch (GlowerStep(actor.step)) {
    case GlowerStep::Wake:
        if (actor.animEnded()) {
            startGlowerAnim(actor, guardian, 8);
            actor.step = uint8_t(GlowerStep::Light);
            GuardianParams(actor).signal = 0;
        }
        return;
    case GlowerStep::Light:
        if (actor.animEnded() && GuardianParams(actor).signal != 0) {
            slotByIdentityWord<EffectActor>(guardian.glows[0])->subState = kGlowLit;
            slotByIdentityWord<EffectActor>(guardian.glows[1])->subState = kGlowLit;
            actor.step = uint8_t(GlowerStep::Done);
        }
        return;
    case GlowerStep::Turn:
        if (actor.animEnded()) {
            startGlowerAnim(actor, guardian, 9);
            actor.step = uint8_t(GlowerStep::ShakeA);
        }
        return;
    case GlowerStep::Turn2:
        if (actor.animEnded()) {
            startGlowerAnim(actor, guardian, 0xa);
            actor.step = uint8_t(GlowerStep::ShakeB);
        }
        return;
    case GlowerStep::ShakeA:
    case GlowerStep::ShakeB: {
        const bool first = GlowerStep(actor.step) == GlowerStep::ShakeA;
        if (actor.keyIndex != (first ? 0xf : 0x15)) return;
        cameraShakeAt(0x6000, 0x400, 0x100, 0x1000, actor.pos); // exe 0x40eab0
        soundPlayAtPos(first ? kQuakeSoundA : kQuakeSoundB, actor.pos);
        actor.step = uint8_t(GlowerStep::Done);
        return;
    }
    default: return;
    }
}
void runRoutine(AnimatedActor& actor, GuardianFields& guardian, GuardianRoutine routine) {
    switch (routine) {
    case GuardianRoutine::Idle: return;
    case GuardianRoutine::Free: freeActor(actor); return;
    case GuardianRoutine::BindStomper: bindStomper(actor, guardian); return;
    case GuardianRoutine::Stomper: stomper(actor, guardian); return;
    case GuardianRoutine::BindGlower: bindGlower(actor, guardian); return;
    case GuardianRoutine::GlowerBySub: // [0x895198]
        if (actor.subState != 0) {
            notPortedAt(kStoryPort, "0x4cd120 sub-state (table 0x895198)", actor.subState);
            return;
        }
        glower(actor, guardian);
        return;
    case GuardianRoutine::Glower: glower(actor, guardian); return;
    default: notPortedAt(kStoryPort, "0x4ccb70 routine", uint32_t(routine));
    }
}

// exe 0x4ccb70
void guardian(AnimatedActor& actor) {
    GuardianFields& guardian = classState<GuardianFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.tint = kTintNeutral;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    const uint8_t state = actor.state;
    if (state >= 9) {
        notPortedAt(kStoryPort, "0x4ccb70 state (table 0x895178)", state);
        return;
    }
    if (state == kBySubState) { // 0x4cccc0 [0x895184]
        if (actor.subState >= 6) {
            notPortedAt(kStoryPort, "0x4cccc0 sub-state (table 0x895184)", actor.subState);
            return;
        }
        runRoutine(actor, guardian, kSubRoutines[actor.subState]);
    } else {
        runRoutine(actor, guardian, kStateRoutines[state]);
    }
    if (actor.anim == actor.prevAnim) {
        if (guardian.flags & kGuardianHoldPose) animDecodeCurrent(actor); // exe 0x5e1f30
        else animStep(actor);                                             // exe 0x5e1ea0
    } else {
        animSet(actor, actor.anim, guardian.animKey); // exe 0x5e1c50
    }
    animPose(actor); // exe 0x5e2d90
}

} // namespace

void guardianUpdate(const PlayerEnv&, uint32_t actor) { guardian(animatedActorAt(actor)); }

void bindGlowerGuardian(AnimatedActor& actor) {
    GuardianFields& fields = classState<GuardianFields>(actor);
    bindGlower(actor, fields);
}
void glowerGuardianStep(AnimatedActor& actor) {
    GuardianFields& fields = classState<GuardianFields>(actor);
    glower(actor, fields);
}

} // namespace od::port
