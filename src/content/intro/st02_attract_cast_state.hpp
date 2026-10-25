// The class states of st02_attract_cast.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/field_alias.hpp"

namespace od::port::st02_attract_cast_state {

// The hold block of a class (after its tracks): its flags (kHoldFrame), the frame it shows (copied each tick, signed)
// and the key a new animation starts at
struct HoldBlock {
    uint8_t flags = 0;
    int8_t frame = 0;
    uint8_t startKey = 0;
};

// The two texture tracks (the face) / the part track at the class block's start, the hold block after them; the hold
// block alone
struct FaceTracksFields : ActorClassState {
    explicit FaceTracksFields(AnimatedActor&) {}
    KeyframeTracks tracks;
    HoldBlock hold;

    void clearBlock() override {
        tracks = {};
        hold = {};
    }
};

struct PartTrackFields : ActorClassState {
    explicit PartTrackFields(AnimatedActor&) {}
    KeyframeTrack track;
    HoldBlock hold;

    void clearBlock() override {
        track = {};
        hold = {};
    }
};

struct HoldOnlyFields : ActorClassState {
    explicit HoldOnlyFields(AnimatedActor&) {}
    HoldBlock hold;

    void clearBlock() override { hold = {}; }
};

} // namespace od::port::st02_attract_cast_state
