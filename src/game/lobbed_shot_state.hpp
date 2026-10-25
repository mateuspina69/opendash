// The class states of lobbed_shot.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/lobbed_shot.hpp"
#include "game/field_alias.hpp"

namespace od::port::lobbed_shot_state {

// The shot's fields: its spawn parameters, its hit box and its sprite's frame
struct LobbedShotFields : ActorClassState, LobbedShotParams {
    explicit LobbedShotFields(ProjectileActor& actor) : LobbedShotParams(actor), hitBox(actor.hitBox), slot_(actor) {}
    HitBoxHead& hitBox;
    int16_t spriteU = 0; // the sprite's u (steps 0x20 a tick through 6 frames)

    // its class block cleared at the set-up, one byte at a time (the hit box, the sprite's u and the bytes after)
    void clearBlockBytes() {
        hitBox = {};
        slot_.debrisFlags = 0;
        spriteU = 0;
    }

private:
    ProjectileActor& slot_;
};

} // namespace od::port::lobbed_shot_state
