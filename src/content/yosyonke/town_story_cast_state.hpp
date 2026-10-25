// The class states of town_story_cast.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::town_story_cast_state {

// ---- Roll running off to Joe's lab (exe 0x448320, ST09 pool 0x20 type 0 sub 2: the variant 0x4483f0) -----------------
// its own bytes: the flag that sets the animation again every tick and its start key
struct RunningRollFields : ActorClassState {
    explicit RunningRollFields(AnimatedActor& actor) : variant(actor.params[0]), floating(actor.params[3]) {}
    FieldAlias<int8_t, uint8_t> variant;
    uint8_t& floating;
    uint8_t runFlags = 0; // kPuppetResetAnim
    uint8_t animKey = 0;

    void clearBlock() override {
        runFlags = 0;
        animKey = 0;
    }
};

} // namespace od::port::town_story_cast_state
