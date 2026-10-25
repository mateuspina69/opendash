// The class states of walker_shot.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::walker_shot_state {

// The shot's class block: its hit box, its life and its sprites' frame
struct WalkerShotFields : ActorClassState {
    explicit WalkerShotFields(ProjectileActor& actor) : hitBox(actor.hitBox), slot_(actor) {}
    HitBoxHead& hitBox;
    uint8_t life = 0;        // ticks left
    uint8_t spriteFrame = 0; // 0..3

    // its class block cleared at the set-up, one byte at a time
    void clearBlockBytes() { slot_.clearClassBlock(); }

    void clearBlock() override {
        life = 0;
        spriteFrame = 0;
    }

private:
    ProjectileActor& slot_;
};

} // namespace od::port::walker_shot_state
