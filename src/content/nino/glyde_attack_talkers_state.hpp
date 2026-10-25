// The class states of glyde_attack_talkers.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/field_alias.hpp"

namespace od::port::glyde_attack_talkers_state {

// The class block: the two texture tracks
struct TalkerFields : ActorClassState {
    explicit TalkerFields(AnimatedActor&) {}
    KeyframeTracks tracks;

    void clearBlock() override {
        tracks = {};
    }
};

} // namespace od::port::glyde_attack_talkers_state
