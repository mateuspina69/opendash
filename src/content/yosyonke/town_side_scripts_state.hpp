// The class states of town_side_scripts.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/script_state.hpp"

namespace od::port::town_side_scripts_state {

struct FlyerFields : ActorClassState {
    explicit FlyerFields(StaticActor& actor) : go(actor.params[1]) {}
    uint8_t& go;
    uint16_t glideTicks = 0;

    void clearBlock() override {
        glideTicks = 0;
    }
};

// the script's own bytes (after the cast's copy of the timeline)
struct ExamDoorsBytes : ScriptPart<ExamDoorsBytes> {
    uint8_t destinationRoom = 0; // +0x12
    uint16_t doorSegmentA = 0; // +0x14
    uint16_t doorSegmentB = 0; // +0x16
};

// the script's own bytes
struct VisitorBytes : ScriptPart<VisitorBytes> {
    uint16_t timer = 0; // +0xc
    uint16_t doorSegment = 0; // +0xe
    uint16_t soundTimer = 0; // +0x10
};

} // namespace od::port::town_side_scripts_state
