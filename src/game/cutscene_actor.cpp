// The cutscene actors' shared steps (see cutscene_actor.hpp).
#include "game/actor_state.hpp"
#include "game/cutscene_actor.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"

namespace od::port {

namespace {

// the cast classes' state 0 ends in state 1 (the classes' own first state)
constexpr uint8_t kCastFirstState = 1;
// the model variant the cast classes bind
constexpr int kCastModelVariant = 0;

} // namespace

void castSavePose(AnimatedActor& actor) {
    actor.savePreviousPose();
    actor.prevAnim = actor.anim;
}

bool castBindModel(AnimatedActor& actor, uint32_t box) {
    if (!bindActorModel(actor, kCastModelVariant)) {
        freeActor(actor); // exe 0x5e1490
        return false;
    }
    actor.collisionBox.pointAt(box);
    actor.clearClassBlock();
    actor.enterState(kCastFirstState);
    return true;
}

bool castBind(AnimatedActor& actor, uint32_t box, uint8_t anim, bool clearMotion) {
    if (!castBindModel(actor, box)) return false;
    actor.anim = anim;
    actor.prevAnim = kNoAnimShown;
    if (clearMotion) clearVelocity(actor);
    return true;
}

void clearVelocity(Actor& actor) {
    actor.velocity.z = 0;
    actor.velocity.y = 0;
    actor.velocity.x = 0;
    actor.acceleration.z = 0;
    actor.acceleration.y = 0;
    actor.acceleration.x = 0;
}

void castMove(Actor& actor) {
    actor.velocity.x = int16_t(actor.velocity.x + actor.acceleration.x);
    actor.velocity.z = int16_t(actor.velocity.z + actor.acceleration.z);
    actor.velocity.y = int16_t(actor.velocity.y + actor.acceleration.y);
    integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z);
}

void castDrift(Actor& actor) {
    if (actor.acceleration.x != 0) actor.velocity.x += uint16_t(actor.acceleration.x);
    if (actor.acceleration.z != 0) actor.velocity.z += uint16_t(actor.acceleration.z);
    if (actor.velocity.x != 0 || actor.velocity.z != 0) integrateVelocity(actor, actor.velocity.x, 0, actor.velocity.z);
}

void castAnimate(AnimatedActor& actor, uint8_t key, uint8_t flags, uint8_t holdMask) {
    if (actor.anim != actor.prevAnim) {
        animSetPose(actor, actor.anim, key);
    } else if (flags & holdMask) {
        animDecodeCurrent(actor); // exe 0x5e1f30
        animPose(actor);
    } else {
        animStepPose(actor);
    }
}

void castAnimateFromKey(AnimatedActor& actor, const CastFields& cast) {
    const uint8_t bits = cast.castFlags;
    if (actor.anim == actor.prevAnim && (bits & kCastHeld) == 0) {
        animStepPose(actor);
    } else {
        const uint8_t key = (bits & kCastHoldPose) ? uint8_t(cast.heldKey) : uint8_t(cast.animKey);
        animSetPose(actor, actor.anim, key);
    }
}

} // namespace od::port
