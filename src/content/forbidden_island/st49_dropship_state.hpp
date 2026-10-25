// The class states of st49_dropship.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::st49_dropship_state {

// ---- the views --------------------------------------------------------------------------------------------------------
// The dropship's class block: the counter of its phases
struct DropshipFields : ActorClassState {
    explicit DropshipFields(AnimatedActor&) {}
    uint16_t counter = 0;

    void clearBlock() override {
        counter = 0;
    }
};

// The cargo's class block: the dropship it hangs from (set by the dropship) and how far below it hangs
struct CargoFields : ActorClassState {
    explicit CargoFields(AnimatedActor&) {}
    ActorRef<AnimatedActor> dropship;
    int16_t hangDepth = 0; // whole units below the dropship's position

    void clearBlock() override {
        dropship = nullptr;
        hangDepth = 0;
    }
};

// The cable's fields (its effect slot): the two actors it hangs between (set by the dropship)
struct CableFields : ActorClassState {
    explicit CableFields(EffectActor&) {}
    ActorRef<AnimatedActor> dropship;
    ActorRef<AnimatedActor> cargo;
};

} // namespace od::port::st49_dropship_state
