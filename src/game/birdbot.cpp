// The Birdbots' shared body routines (see birdbot.hpp).
#include "game/actor_state.hpp"
#include "game/birdbot.hpp"

#include "game/actor_movement.hpp"

namespace od::port {

int birdbotArc(AnimatedActor& actor, BirdbotBody& body, int yaw) { // exe 0x492e70
    if (body.contact & (kBirdbotLanded | kBirdbotOnPlatform)) {
        actor.velocity.z = int16_t(actor.velocity.z >> 2);
        actor.velocity.y = int16_t(uint32_t(-int(actor.velocity.y)) >> 1);
        actor.rotation.x = 0;
        return 1;
    }
    moveAlongYaw(actor.pos, yaw, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e36f0
    actor.velocity.y = int16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y));
    return 0;
}

void birdbotContactHits(AnimatedActor& actor, BirdbotBody& body) { // exe 0x492ed0
    const uint32_t r = actor.actorHit;
    if (r != 0) {
        if (r & 0xf00) body.contact |= uint8_t(kBirdbotWall | kBirdbotActorSide);
        if ((body.bits & kBirdbotBounces) && reflectRotationOffWalls(actor.rotation, r) != 0)
            body.contact |= kBirdbotLanded;
    }
    if (actor.onPlatform != 0) body.contact |= kBirdbotOnPlatform;
}

int birdbotCollide(const PlayerEnv& env, AnimatedActor& actor, BirdbotBody& body, int16_t& groundHeight) {
    uint32_t r = 0; // exe 0x4958f0
    body.contact = 0;
    const uint32_t t = actorTestMove(env, actor); // exe 0x40f570
    if (t & 0x20000000u) body.contact |= kBirdbotInWater;
    if (int32_t(t) < 0) {
        r = actorPushOut(env, actor); // exe 0x410960
        actor.wallHit = r;
        if (int32_t(r) < 0) actor.pos = actor.prevPos;
        if (r & 0xf00) body.contact = kBirdbotWall;
        if ((body.bits & kBirdbotBounces) && reflectRotationOffWalls(actor.rotation, r) != 0)
            body.contact |= kBirdbotLanded;
    }
    const int g = actorGround(env, actor); // exe 0x40f190
    groundHeight = int16_t(g);
    if (actor.onPlatform == 0) {
        if (actor.airborne == 0) {
            if (0x20 < g - actor.pos.y.whole) {
                body.contact |= kBirdbotGroundLost;
                return 1;
            }
            actor.pos.y.whole = int16_t(g);
            return 0;
        }
        if (!(body.bits & kBirdbotBounces) && (r & 0x3000)) {
            actor.airborne = 0;
            actor.pos.y.whole = int16_t(g);
            body.contact |= kBirdbotLanded;
        }
    }
    return 0;
}

} // namespace od::port
