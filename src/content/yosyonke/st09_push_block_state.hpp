// The class states of st09_push_block.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st09_push_block_state {

// the block's bytes
struct BlockFields : ActorClassState {
    explicit BlockFields(StaticActor& actor) : box(actor.hitBox), hits(actor.gunshipTimer()), solid(actor.gunshipCooldown()), coolDown(actor.gunshipAttackCooldown()) {}
    HitBoxHead& box; // its hit-list result box
    uint8_t& hits; // kHit*: what the collisions found this tick
    uint8_t& solid; // 1: queued as a solid to stand on
    uint8_t& coolDown; // the re-hit's cool-down
    ActorRef<StaticActor> partner; // the block's copy (variant 1) / the copy's block
    uint16_t pushYaw = 0; // the yaw it was hit along / the push's timer
    int16_t travelYaw = 0;
    uint8_t mode = 0; // kMode*

    void clearBlock() override {
        pushYaw = 0;
        travelYaw = 0;
        mode = 0;
    }
};

} // namespace od::port::st09_push_block_state
