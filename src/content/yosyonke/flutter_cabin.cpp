// The classes of the Flutter's cabin (ST04; see arrival.hpp): the actor that plays the animation of its spawn bytes,
// a placed model, an NPC that plays an animation while its flag is set and turns its head toward a point, and Roll at
// her seat (the way to the travel map).
#include "content/yosyonke/arrival.hpp"
#include "content/yosyonke/flutter_cabin_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/collision.hpp"
#include "game/cutscene_actor.hpp"
#include "game/distances.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/field_alias.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace flutter_cabin_state;

namespace {

// ---- the animated actor (exe 0x4cd290, pool 0x20 type 0x47)
// -----------------------------------------------------------
// its spawn bytes: the animation to play and its start key
struct CabinAnimatedFields {
    explicit CabinAnimatedFields(const AnimatedActor& actor) : CabinAnimatedFields(const_cast<AnimatedActor&>(actor)) {}
    explicit CabinAnimatedFields(AnimatedActor& actor) : anim(actor.params[0]), key(actor.params[1]) {}
    FieldAlias<int8_t, uint8_t> anim;
    FieldAlias<int8_t, uint8_t> key;
};

// exe 0x4cd290
void cabinAnimated(AnimatedActor& a) {
    const CabinAnimatedFields f(a);
    castSavePose(a);
    if (a.state == 0) {
        if (!bindActorModel(a, 0)) {
            freeActor(a);
            return;
        }
        a.collisionBox.pointAt(TableId::FlutterCabinAnimatedBox);
        a.clearClassBlock();
        a.enterState(1);
        a.anim = uint8_t(f.anim);
        a.prevAnim = kNoAnimShown;
        a.airborne = kAirborneFalling;
    }
    if (!(a.flags & kActorLive)) return;
    if (uint32_t(a.anim) != uint32_t(int32_t(f.anim))) a.anim = uint8_t(f.anim);
    if (a.anim != a.prevAnim) animSetPose(a, a.anim, int(f.key));
    else animStepPose(a);
}

// ---- the placed model (exe 0x58dba0, static type 1: its state 0 0x5915d0 binds the model of its variant)
// -------------
void cabinModel(StaticActor& a) {
    if (a.state != 0) return;
    if (!bindActorModel(a, a.variant)) {
        freeActor(a); // exe 0x5e1540
        return;
    }
    a.state = 1;
}

// ---- the NPC (exe 0x4e7290, pool 0x20 type 0x60, variants 0x896348 by its signed +0xc: 0x4e74c0)
// ----------------------
constexpr uint8_t kRestartAnim = 0x01; // the animation restarted (set again) every tick
const DataTable<RoutineTable<1>> npcVariants{TableId::FlutterCabinNpcVariants};
constexpr uint32_t kCabinNpcAnimFlag = 0x5e3; // the NPC plays its animation while set (it clears it at the end)
constexpr uint8_t kNpcIdleAnim = 0, kNpcFlagAnim = 3;
enum class NpcSub : uint8_t { Idle = 0, Playing = 1 };
// the parts its state 0 hides / shows
constexpr size_t kNpcHidden[2] = {11, 12}, kNpcShown = 3;
constexpr int16_t kMaxHeadYaw = 0x300, kMaxHeadPitch = 0x200;
// the head's turn (decoded frame entry 2: the head's angles, offset before the pose)
constexpr size_t kHeadEntry = 2;

// exe 0x4e7290: the state (its variant checked), the ground, the animation, the head's turn, the pose
void cabinNpc(const PlayerEnv& env, AnimatedActor& a) {
    CabinNpcFields& f = classState<CabinNpcFields>(a);
    castSavePose(a);
    if (npcVariants->idAt(size_t(int(f.variantIndex))) != RoutineId::FlutterCabinCabinNpc)
        notPortedAt(kStoryPort, "0x4e7290 variant", uint8_t(f.variantIndex));
    if (a.state == 0) { // exe 0x4e74c0
        if (!bindActorModel(a, a.variant)) {
            freeActor(a);
            return;
        }
        a.collisionBox.pointAt(TableId::FlutterCabinNpcBox);
        a.clearClassBlock();
        a.enterState(1);
        a.anim = kNpcIdleAnim;
        a.prevAnim = kNoAnimShown;
        f.npcFlags &= uint8_t(~kRestartAnim);
        a.parts[kNpcHidden[0]].flags |= kPartHidden;
        a.parts[kNpcHidden[1]].flags |= kPartHidden;
        a.parts[kNpcShown].flags &= uint8_t(~kPartHidden);
        f.headYaw = 0;
        f.headPitch = 0;
    } else if (a.state == 1) {
        if (NpcSub(a.subState) == NpcSub::Idle) {
            if (storyFlag(kCabinNpcAnimFlag)) {
                a.anim = kNpcFlagAnim;
                a.subState = uint8_t(NpcSub::Playing);
            }
        } else if (NpcSub(a.subState) == NpcSub::Playing && a.animEnded()) {
            clearStoryFlag(kCabinNpcAnimFlag);
            a.anim = kNpcIdleAnim;
            a.subState = uint8_t(NpcSub::Idle);
        }
    }
    if (!(a.flags & kActorLive)) return;
    if (f.floating == 0) a.pos.y.whole = int16_t(actorGround(env, a));
    if (a.anim == a.prevAnim && !(f.npcFlags & kRestartAnim)) animStep(a);
    else animSet(a, a.anim, f.animKey);
    const int16_t step = f.turnStep;
    if (f.looking != 0) {
        const uint16_t h = uint16_t(f.headYaw);
        int d = int((uint32_t(uint16_t(a.rotation.y)) + h) & kAngleMask) - int(f.lookYaw & kAngleMask);
        if (uint32_t(d + kHalfTurn) > uint32_t(kFullTurn)) d = int((uint32_t(-d) ^ uint32_t(kAngleMask)) + 1u);
        if (d < -(step >> 1)) {
            f.headYaw = int16_t(uint16_t(step + h));
            if (int16_t(step + h) > kMaxHeadYaw) f.headYaw = kMaxHeadYaw;
        } else if ((step >> 1) < d) {
            f.headYaw = int16_t(uint16_t(h - step));
            if (int16_t(h - step) < -kMaxHeadYaw) f.headYaw = -kMaxHeadYaw;
        }
        const int16_t p = f.headPitch;
        const int e = ((int(a.pos.y.whole) - int(f.lookHeight)) >> 1) + p;
        if (e < -(step >> 1)) {
            f.headPitch = int16_t(uint16_t(p + step));
            if (int16_t(p + step) > kMaxHeadPitch) f.headPitch = kMaxHeadPitch;
        } else if ((step >> 1) < e) {
            f.headPitch = int16_t(uint16_t(p - step));
            if (int16_t(p - step) < -kMaxHeadPitch) f.headPitch = -kMaxHeadPitch;
        }
    } else { // back to straight ahead
        for (int16_t* lane : {&f.headYaw, &f.headPitch}) {
            const int16_t v = *lane;
            if (v < 0) {
                *lane = int16_t(uint16_t(step + v));
                if (int16_t(step + v) > 0) *lane = 0;
            } else {
                *lane = int16_t(uint16_t(v - step));
                if (int16_t(v - step) < 0) *lane = 0;
            }
        }
    }
    DecodedFrame& frame = decodedFrame();
    frame.entries[kHeadEntry].x += uint16_t(f.headPitch);
    frame.entries[kHeadEntry].y += uint16_t(f.headYaw);
    animPose(a); // (the shadow, exe 0x5f20f0: render output)
}

constexpr uint8_t kPoseHeld = 0x80;      // the pose decoded, not stepped
constexpr uint8_t kPoseTalkFlag = 0x40;  // the talk event's flags (>> 2: kEventKeepsInput's bit)
constexpr uint8_t kPoseAside = 0x20;     // she looks aside while talking
constexpr uint8_t kFlutterRollNoTalk = 0x01, kFlutterRollWatching = 0x02, kFlutterRollSolid = 0x04,
                  kFlutterRollMover = 0x08;
enum class FlutterRollSub : uint8_t {
    Settle = 0, // exe 0x4e7070
    Watch = 1,  // exe 0x4e70b0: step 2 while the player is within 0x100 and in front of her (+-0x200)
    Talk = 2,   // exe 0x4e7190
};
const DataTable<CollisionBoxRecord> flutterRollCollisionBox{TableId::FlutterCabinRollCollisionBox};
const DataTable<InteractionBoxRecord> flutterRollTalkBox{TableId::FlutterCabinRollTalkBox};
constexpr uint8_t kSeatedAnim = 6, kTalkIdleAnim = 4, kTalkEndAnim = 5;
constexpr uint8_t kWatchTicks = 0x3c;
constexpr size_t kFlutterRollParts[3] = {3, 11, 12}; // the parts one of which shows

// the yaw against the player's direction toward her (exe 0x5e3d80(+0x2a, exe 0x5e3bc0(the player, actor)))
int facingDiff(const AnimatedActor& a) {
    return angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(angleOf(player().pos, a.pos)));
}
void flutterRollSub(AnimatedActor& a, FlutterRollFields& f) {
    switch (FlutterRollSub(a.subState)) {
    case FlutterRollSub::Settle:
        if (a.step == 0) {
            a.anim = kSeatedAnim;
            f.gaze = 2;
            f.mode = kFlutterRollSolid;
            a.enterStep(1);
        }
        if (f.watchTimer != 0) f.watchTimer = kWatchTicks;
        a.subState = uint8_t(FlutterRollSub::Watch);
        a.restartStep();
        return;
    case FlutterRollSub::Watch: {
        const uint8_t st = a.step;
        if (st == 0) {
            a.anim = kSeatedAnim;
            f.mode = kFlutterRollSolid | kFlutterRollWatching;
            a.enterStep(1);
        }
        if (st <= 1) {
            f.gaze = 1;
            if (distance3(a.pos, player().pos) < 0x100 && uint32_t(facingDiff(a) + 0x200) < 0x400u) a.enterStep(2);
        } else if (st == 2) {
            f.gaze = 2;
            const int dist = distance3(a.pos, player().pos);
            const int d = facingDiff(a);
            if (dist > 0x180 || uint32_t(d + 0x280) > 0x500u) {
                f.watchTimer = 0;
                a.enterStep(1);
            }
        }
        if (f.watchTimer != 0) f.watchTimer = kWatchTicks;
        return;
    }
    case FlutterRollSub::Talk: {
        const uint8_t st = a.step;
        if (st == 0) {
            f.mode = kFlutterRollSolid | kFlutterRollWatching | kFlutterRollNoTalk;
            a.anim = kSeatedAnim;
            eventState().speakerAnimation = kNoSpeakerAnimation;
            f.gaze = (f.pose & kPoseAside) ? 2u : 1u;
            f.look = 0;
            a.enterStep(1);
        } else if (st == 2) {
            if (!a.animEnded()) return;
            f.watchTimer = kWatchTicks;
            a.subState = uint8_t(FlutterRollSub::Settle);
            a.restartStep();
            return;
        } else if (st != 1) {
            return;
        }
        if (eventState().speakerAnimation == kNoSpeakerAnimation) {
            if (a.animKey.flags == kKeyHold) a.anim = kTalkIdleAnim;
        } else {
            a.anim = eventState().speakerAnimation;
            eventState().speakerAnimation = kNoSpeakerAnimation;
        }
        if (eventState().active == 0) {
            a.anim = kTalkEndAnim;
            a.enterStep(2);
        }
        return;
    }
    default: notPortedAt(kStoryPort, "0x4e7010 sub-state", a.subState);
    }
}
// exe 0x4e6dc0
void flutterRoll(const PlayerEnv& env, AnimatedActor& a) {
    FlutterRollFields& f = classState<FlutterRollFields>(a);
    a.prevAnim = a.anim;
    a.savePreviousPose();
    a.savePreviousContact();
    if ((a.actorHit & kSolidOnSides) || a.lastMover) f.hitBits |= 0x21u;
    switch (a.state) {
    case 0: // exe 0x4e6f80
        if (!bindActorModel(a, a.variant)) {
            freeActor(a); // exe 0x5e1490
            return;
        }
        a.anim = kSeatedAnim;
        a.prevAnim = kNoAnimShown;
        a.maxHitPoints = 1;
        a.hitPoints = 1;
        a.velocity.setXY(0, 0);
        a.velocity.setZSpare(0, 0);
        a.spin.setXY(0, 0);
        a.rotation.z = 0;
        a.rotation.x = 0;
        a.collisionBox.pointAt(flutterRollCollisionBox.stateAddress());
        a.interactionBox.pointAt(flutterRollTalkBox.stateAddress());
        a.interaction = kInteractTalk;
        a.airborne = 0;
        a.onPlatform = 0;
        a.actorHit = 0;
        a.lastMover = nullptr;
        a.clearClassBlock(); // exe 0x40dc40
        resetBoneScales(a);
        a.enterState(1);
        break;
    case 1: // exe 0x4e7010
        if (!(f.mode & kFlutterRollNoTalk) && player().talksTo(a) && eventState().active == 0) {
            eventStartAtActor(a, uint32_t((int32_t(int8_t(f.pose)) & kPoseTalkFlag) >> 2), 0, f.message);
            a.subState = uint8_t(FlutterRollSub::Talk);
            a.restartStep();
        }
        flutterRollSub(a, f);
        break;
    default: notPortedAt(kStoryPort, "0x4e6dc0 state", a.state);
    }
    f.hitBits = 0;
    const uint32_t r = actorTestMove(env, a);
    if (r & 0x2600000) f.hitBits |= 8u;
    if (int32_t(r) < 0) {
        const uint32_t r2 = actorPushOut(env, a); // exe 0x410960
        a.wallHit = r2;
        if (int32_t(r2) < 0) a.pos = a.prevPos;
        if (r2 & kWallOnSides) f.hitBits |= 1u;
    }
    a.pos.y.whole = int16_t(actorGround(env, a));
    uint8_t shown[3];
    for (size_t k = 0; k < 3; ++k) shown[k] = uint8_t(a.parts[kFlutterRollParts[k]].flags | kPartHidden);
    if (f.shownPart == 1) shown[1] &= uint8_t(~kPartHidden);
    else if (f.shownPart == 2) shown[2] &= uint8_t(~kPartHidden);
    else shown[0] &= uint8_t(~kPartHidden);
    for (size_t k = 0; k < 3; ++k) a.parts[kFlutterRollParts[k]].flags = shown[k];
    // exe 0x4e7240: the animation
    if (f.pose & kPoseHeld) {
        animDecodeCurrent(a); // exe 0x5e1f30
    } else if (a.anim != a.prevAnim) {
        animSet(a, a.anim, 0); // exe 0x5e1c30: the set and the pose
        animPose(a);
    } else {
        animStep(a);
        animPose(a);
    }
    f.box.result = 0;
    f.box.previousX = a.prevPos.x.whole;
    f.box.previousZ = a.prevPos.z.whole;
    const int16_t pos[3] = {a.pos.x.whole, int16_t(a.pos.y.whole - 100), a.pos.z.whole};
    hitListSubmit(pos, f.box, hitOwner(a.slot) | hitGroup(kHitGroupBodies) | hitRadius(0x40), kHitAttackHarmless);
    const uint8_t bits = f.mode;
    if (bits & kFlutterRollSolid) actorCollisionSubmitSolid(a, SolidType::PushOut, kNoBone, 0);
    else a.lastMover = nullptr;
    if (bits & kFlutterRollMover) actorCollisionSubmitMover(a);
    else a.actorHit = 0;
}

} // namespace

void cabinAnimatedUpdate(const PlayerEnv&, uint32_t actor) { cabinAnimated(animatedActorAt(actor)); }
void cabinModelUpdate(const PlayerEnv&, uint32_t actor) { cabinModel(staticActorAt(actor)); }
void cabinNpcUpdate(const PlayerEnv& env, uint32_t actor) { cabinNpc(env, animatedActorAt(actor)); }
void flutterRollUpdate(const PlayerEnv& env, uint32_t actor) { flutterRoll(env, animatedActorAt(actor)); }

} // namespace od::port
