// The room collision step of the walking NPC classes (see walker_collision.hpp).
#include "game/walker_collision.hpp"

#include "game/actor_collision.hpp"
#include "game/collision.hpp"
#include "game/territory.hpp"

namespace od::port {

int walkerCollide(const PlayerEnv& env, Actor& actor, uint8_t& hits, const uint8_t& behaviour,
                  bool queueMover, int maxDrop) {
    hits = 0;
    uint32_t r = 0;
    if (int32_t(actorTestMove(env, actor)) < 0) { // exe 0x40f570
        r = actorPushOut(env, actor);              // exe 0x410960
        if (int32_t(r) < 0) actor.pos = actor.prevPos;
        if (r & (kWallOnXPlus | kWallOnXMinus)) hits |= kWalkHitSidesLow;
        if (r & (kWallOnZPlus | kWallOnZMinus)) hits |= kWalkHitSidesHigh;
        if ((behaviour & kWalkerBounces) && reflectRotationOffWalls(actor.rotation, r) != 0) hits |= kWalkLanded;
    } else {
        const int z = territoryViolated(actor.pos);
        if (z != 0) {
            r = territoryPushBack(actor, z);
            if (r & (kWallOnXPlus | kWallOnXMinus)) hits |= kWalkHitSidesLow;
            if (r & (kWallOnZPlus | kWallOnZMinus)) hits |= kWalkHitSidesHigh;
        }
    }
    if (queueMover) actorCollisionSubmitMover(actor); // exe 0x4140b0
    const int g = actorGround(env, actor);            // exe 0x40f190
    if (actor.airborne == 0) {
        if (g - int(actor.pos.y.whole) > maxDrop) {
            hits |= kWalkGroundLost;
            return 1;
        }
        actor.pos.y.whole = int16_t(g);
        return 0;
    }
    if (!(behaviour & kWalkerBounces) && (r & kWallOnY)) {
        actor.airborne = 0;
        actor.pos.y.whole = int16_t(g);
        hits |= kWalkLanded;
    }
    return 0;
}

int walkerFallStep(Actor& actor, const uint8_t& hits, int yaw) {
    if (hits & kWalkLanded) {
        actor.velocity.y = int16_t(uint32_t(-int(actor.velocity.y) >> 1));
        actor.velocity.z = int16_t(actor.velocity.z >> 1);
        actor.rotation.x = 0;
        actor.airborne = kAirborneFalling;
        return 1;
    }
    moveAlongYaw(actor.pos, yaw, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e36f0
    actor.velocity.y += actor.acceleration.y;
    return 0;
}

} // namespace od::port
