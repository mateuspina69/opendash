// The class states of st14_enemy.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"

namespace od::port::st14_enemy_state {

// The class's own fields: the cruise speed in its velocity's spare lane, the spin's acceleration in its spin's, and
// its class block
struct HoverFields : ActorClassState {
    explicit HoverFields(AnimatedActor& actor) : cruiseSpeed(actor.velocity.spare), spinAccel(actor.spin.spare), noticeCooldown(actor.townBirdHits), hitResult(actor.townBirdHitsHigh) {}
    int16_t& cruiseSpeed; // the velocity's spare lane
    int16_t& spinAccel; // the spin's spare lane (the knock-back's spin slowing)
    uint8_t& noticeCooldown; // ticks before the player is noticed again
    uint8_t& hitResult; // this tick's HitResult
    HitBoxHead hitBox;
    uint8_t mode = 0; // kHover*
    uint8_t contact = 0; // kHoverContact* of this tick's collision
    ShortVec3 home;
    ShortVec3 target; // the dive's target
    int16_t movePitch = 0; // the rotation it moves along
    int16_t moveYaw = 0;
    int16_t timer = 0;

    void clearBlock() override {
        hitBox = {};
        mode = 0;
        contact = 0;
        home = {};
        target = {};
        movePitch = 0;
        moveYaw = 0;
        timer = 0;
    }
};

} // namespace od::port::st14_enemy_state
