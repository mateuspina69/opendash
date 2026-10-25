// The class states of abandoned_mine_roll.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/field_alias.hpp"

namespace od::port::abandoned_mine_roll_state {

struct RuinRollFields : ActorClassState {
    explicit RuinRollFields(AnimatedActor& actor) : kind(actor.params[0]), animParam(actor.params[1]), lookParam(actor.params[2]), message(actor.params[3]), headPitch(actor.velocity.spare), headYaw(actor.acceleration.spare), pathIndex(actor.shownParts) {}
    uint8_t& kind; // 0 stands, 1 the first path, else the second
    uint8_t& animParam; // the animation she plays
    uint8_t& lookParam; // & 0x60: the head's target (kLook*), & 0x1f the script actor; the talk's
    uint8_t& message;
    int16_t& headPitch; // the velocity's spare lane
    int16_t& headYaw; // the acceleration's spare lane
    uint8_t& pathIndex;
    KeyframeTracks tracks; // her texture tracks (her face)
    uint32_t talkWord_ = 0;
    PartField<uint8_t, uint32_t> talking{talkWord_, 0}; // bit 0: she talks (her head turns faster)
    PartField<uint8_t, uint32_t> animKey{talkWord_, 2}; // the key her animations start at
    PartField<uint8_t, uint32_t> frameCopy{talkWord_, 3}; // the animation key's frame of the last tick (its reader is not traced)
    void clearTalkWord() {
        talkWord_ = 0;
    }

    void clearBlock() override {
        tracks = {};
        talkWord_ = 0;
    }
};

} // namespace od::port::abandoned_mine_roll_state
