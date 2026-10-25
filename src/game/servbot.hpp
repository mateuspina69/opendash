// The servbots, class 0x4628b0 (pool 0x20 type 0xb of the ST24 / ST3B tables): a sibling of the town NPC
// (game/town_npc: the same route / stationary / talk / face-the-player logic on its own tables and a shifted block)
// plus the reactions to being hit (knocked down front / back, dazed, get up) and to the ground falling away (fall,
// bounce).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/actor_movement.hpp"
#include "game/town_npc.hpp"

namespace od::port {

// The servbot's own fields (spawn parameters, the velocity / acceleration spare lanes and the class block)
struct ServbotFields : ActorClassState {
    explicit ServbotFields(AnimatedActor& actor) : colour(actor.spawnByte), route(actor.params[0]), behaviour(actor.params[1]), partnerRoute(actor.params[2]), message(actor.params[3]), timer(actor.velocity.spare), hitYaw(actor.acceleration.spare), fidgetPattern(actor.acceleration.spare), bits(actor.hopperBytes[2]), partnerSlot(actor.hopperBytes[3]), lookMode(actor.hopperBytes[4]), previousWaypoint(actor.townBirdHits), headHold(actor.townBirdHitsHigh), collisionBits(actor.hopperTimer) {}
    uint8_t& colour; // the texture cell of its model (its colour)
    FieldAlias<int8_t, uint8_t> route;
    uint8_t& behaviour; // the town NPC's kNpc* bits | kind
    uint8_t& partnerRoute;
    uint8_t& message;
    int16_t& timer; // pause / wait ticks, the fidget's repeats (the velocity's spare lane)
    int16_t& hitYaw;
    int16_t& fidgetPattern;
    int8_t headYawTarget = 0;
    uint8_t& bits; // kServbot* bits
    uint8_t& partnerSlot;
    uint8_t& lookMode; // NpcLook
    int8_t waypoint = 0;
    uint8_t& previousWaypoint;
    uint8_t& headHold;
    uint8_t& collisionBits; // kServbotBlocked | kServbotLanded | kServbotGroundLost | ...
    HitBox hitBox; // contact.x: the hit's yaw
    int8_t headPitch = 0;
    int8_t headYaw = 0;
    int8_t headPitchTarget = 0;
    union {
        int16_t targetX = 0;
        int16_t fidgetLookTicks;
    };
    int16_t targetZ = 0; // the waypoint z
    uint8_t groundLossReaction = 0; // what losing the ground leads to (1 fall, 2 bounce)
    NpcHead head() {
        return {headPitch, headYaw, headPitchTarget, headYawTarget, targetX, targetZ, partnerSlot, lookMode, headHold};
    }

    void clearBlock() override {
        headYawTarget = 0;
        waypoint = 0;
        hitBox = {};
        headPitch = 0;
        headYaw = 0;
        headPitchTarget = 0;
        targetX = 0;
        targetZ = 0;
        groundLossReaction = 0;
    }
};

// exe 0x4628b0: the servbot's update (ActorUpdateFn)
void servbotUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
