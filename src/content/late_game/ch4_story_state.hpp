// The class states of ch4_story.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/script_state.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"

namespace od::port::ch4_story_state {

// One puff: its live bit and frame, its rise step, its size, its point and its spin
struct SmokePuff {
    uint8_t state = 0;
    int8_t rise = 0;
    uint16_t size = 0;
    ShortVec3 point;
    int16_t spin = 0;
};

// The column's bytes: its parent's slot / stop / parent's type (its parameter bytes), the countdown (the velocity's
// spare lane), the size's growth (the acceleration's x), the puffs' spin (the spin's x); the puffs, the next puff, its
// tick, the glow's texel, the live puffs, the size, the glow on
struct SmokeColumnFields : ActorClassState {
    explicit SmokeColumnFields(StaticActor& actor)
        : parentSlot(actor.params[0]), stop(actor.params[1]), parentType(actor.params[2]), countdown(actor.velocity.spare),
          growth(actor.acceleration.x), puffSpin(actor.spin.x) {}
    FieldAlias<int8_t, uint8_t> parentSlot;
    uint8_t& stop;
    FieldAlias<int8_t, uint8_t> parentType;
    FieldAlias<uint16_t, int16_t> countdown;
    FieldAlias<uint16_t, int16_t> growth;
    int16_t& puffSpin;
    StateArray<SmokePuff, 8> puffs{};
    uint8_t nextPuff = 0;
    uint8_t tick = 0;
    uint8_t glowTexel = 0;
    int8_t livePuffs = 0;
    int16_t size = 0;
    uint8_t glow = 0;
    uint8_t afterGlow = 0; // the byte after it (cleared with the puffs)

    // the puffs to the byte after the glow cleared (exe 0x40dc40)
    void clearPuffWords() {
        puffs = {};
        nextPuff = 0;
        tick = 0;
        glowTexel = 0;
        livePuffs = 0;
        size = 0;
        glow = 0;
        afterGlow = 0;
    }
};

// The script's own bytes: the cue the cast sets (0 / 2..5), a counter
struct Script62Fields : ScriptPart<Script62Fields> {
    uint8_t cue = 0; // +0x12
    int32_t counter = 0; // +0x1c
    // exe 0x40dc40 over +0xc..+0x1b: the members there zeroed (past it: kept)
    void clearSetUpRun() override {
        cue = {};
    }
};

} // namespace od::port::ch4_story_state
