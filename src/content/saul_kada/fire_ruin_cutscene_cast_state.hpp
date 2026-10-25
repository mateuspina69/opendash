// The class states of fire_ruin_cutscene_cast.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::fire_ruin_cutscene_cast_state {

// The prop holder's own bytes: its prop and whether it holds one
struct HolderFields : ActorClassState {
    explicit HolderFields(AnimatedActor&) {}
    ActorRef<StaticActor> heldProp;
    uint8_t holding = 0;
};
// The entrance's and the depths' casts' own bytes: a tick counter and whether its animation runs (the depths' cast
// steps it only then)
struct TickCastFields : ActorClassState {
    explicit TickCastFields(AnimatedActor&) {}
    uint32_t ticks = 0;
    uint8_t animRuns = 0;
};
// The prop dropper's own bytes: its ground height
struct DropperFields : ActorClassState {
    explicit DropperFields(AnimatedActor&) {}
    int16_t groundY = 0;
};

} // namespace od::port::fire_ruin_cutscene_cast_state
