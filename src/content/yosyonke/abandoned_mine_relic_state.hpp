// The class states of abandoned_mine_relic.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/field_alias.hpp"

namespace od::port::abandoned_mine_relic_state {

constexpr uint8_t kRelicTalking = 0x01;

// The relic's own fields: its spawn parameters and its class block (its texture tracks first)
struct RuinRelicFields : ActorClassState {
    explicit RuinRelicFields(AnimatedActor& actor) : cueAnim(actor.params[1]), message(actor.params[3]) {}
    uint8_t& cueAnim; // the animation the scripts ask for (outside a talk)
    uint8_t& message; // the examine's message
    KeyframeTracks tracks; // the two texture tracks
    uint32_t talkWord_ = 0;
    PartField<uint8_t, uint32_t> talkBits{talkWord_, 0}; // kRelicTalking
    PartField<uint8_t, uint32_t> animKeyStart{talkWord_, 2}; // the key a new animation starts at
    PartField<uint8_t, uint32_t> frameCopy{talkWord_, 3}; // the current key's frame (copied every tick)
    void clearTalkWord() {
        talkWord_ = 0;
    }

    void clearBlock() override {
        tracks = {};
        talkWord_ = 0;
    }
};

} // namespace od::port::abandoned_mine_relic_state
