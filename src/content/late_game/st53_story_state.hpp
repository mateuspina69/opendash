// The class states of st53_story.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/field_alias.hpp"
#include "game/script_state.hpp"
#include "game/state_array.hpp"

namespace od::port::st53_story_state {

// ---- the cast ------------------------------------------------------------------------------------------------------
// The cast's own bytes: its face tracks (the textured ones), its animation flags / key; the fading figure (exe
// 0x488a90) keeps its fade counter and bursts there instead, its animation flags / key after them
struct St53CastFields : ActorClassState {
    explicit St53CastFields(AnimatedActor& actor) : faderFlags(actor.shownParts) {}
    uint8_t& faderFlags;
    KeyframeTracks faces;
    int16_t fadeCounter = 0;
    int16_t bursts = 0;
    uint8_t faderKey = 0;
    PartField<uint8_t, int16_t> castFlags{fadeCounter, 0};
    PartField<uint8_t, int16_t> fadeCounterLow{fadeCounter, 0};
    PartField<uint8_t, int16_t> animKey{fadeCounter, 1};

    void clearBlock() override {
        faces = {};
        fadeCounter = 0;
        bursts = 0;
        faderKey = 0;
    }
};

// The scripts' own bytes: the cast's copy of the timeline (the tick, the step id / sub), the talking cue the cast sets,
// the next scene's DAT loaded, a wait counter
struct St53ScriptFields : ScriptPart<St53ScriptFields> {
    uint8_t talkCue = 0; // +0x12
    uint8_t datLoaded = 0; // +0x13
    int32_t waitTicks = 0; // +0x1c
    // exe 0x40dc40 over +0xc..+0x1b: the members there zeroed (past it: kept)
    void clearSetUpRun() override {
        talkCue = {};
        datLoaded = {};
    }
};

} // namespace od::port::st53_story_state
