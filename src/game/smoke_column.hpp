// The smoke column (static type 0x14 of the global table, exe 0x569870): a trail of up to 16 rings rising and swirling
// from the actor (variants 0 / 1: a fire put out, content/intro/flutter_fire) or from one of the player's bone points
// (variant 2: the trails of the player's orbit status, four at a time, game/player/player_status), drawn as gouraud
// quads that are bright along the middle and fade to black at the edges and the tail (OT packets, render output). The
// status trails tint the player darker while the status lasts and die out once it ends.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port {

// The column's fields (its static slot from +0x28: the trail reuses the bytes past the actor's position)
struct SmokeColumnFields : ActorClassState {
    explicit SmokeColumnFields(StaticActor& actor) : ended(actor.stepCounter), bone(actor.params[0]), length(actor.rotation.x), head(actor.rotation.y), rise(actor.rotation.z), phase(actor.rotation.spare), segmentWidth(actor.velocity.x) {}
    uint8_t& ended; // variants 0 / 1: set by the fire when it is gone (the column dies out)
    FieldAlias<int8_t, uint8_t> bone; // variant 2: the player's bone point it trails (the spawn's parameter 0)
    int16_t& length; // the rings in the trail (up to 16)
    int16_t& head; // the newest ring's index
    int16_t& rise; // the rings' rise per step
    FieldAlias<uint16_t, int16_t> phase; // the swirl's angle
    FieldAlias<uint16_t, int16_t> segmentWidth; // the quads' half width step
    uint32_t drawMode = 0; // the DR_MODE word linked before the quads (its abr: (>> 5) & 3)
    uint32_t colourStep = 0; // the brightness lost per ring
    StateArray<int16_t, 16> ringX{};
    StateArray<int16_t, 16> ringY{};
    StateArray<int16_t, 16> ringZ{};
};

// exe 0x569870 (static type 0x14)
void smokeColumnUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
