// The class states of opening_cast.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/cutscene_actor.hpp"

namespace od::port::opening_cast_state {

// ---- the Flutter (exe 0x49dab0, type 0x30)
// ----------------------------------------------------------------------------
// Its class block: the clock of its cloud streaks (a streak every 32 ticks at one of the 8 points)
struct OpeningFlutterFields : ActorClassState {
    explicit OpeningFlutterFields(AnimatedActor&) {}
    uint32_t streakClock = 0;

    void clearBlock() override {
        streakClock = 0;
    }
};

// ---- the crew (exe 0x4e9ae0, type 0x63)
// (the animation numbers its timeline asks for are the class's model-bank animations; the ticks are the timeline)
// -------------------------------------------------------------------------------
// The crew's class block past the cast's: the ticks counted on frame 8 (variant 1)
// (the cast's block, then its frame ticks; the ST39 script 0x68 variant's shadow sizes)
struct CrewFields : CastFields {
    explicit CrewFields(AnimatedActor& actor) : CastFields(actor) {}
    int16_t frameTicks = 0;
    int16_t shadowWidth = 0;
    int16_t shadowLength = 0;

    void clearBlock() override {
        CastFields::clearBlock();
        frameTicks = 0;
        shadowWidth = 0;
        shadowLength = 0;
    }
};

// ---- the talking character (exe 0x4af7e0, type 0x39)
// (the animation numbers its timeline asks for are the class's model-bank animations; the ticks are the timeline)
// ------------------------------------------------------------------
// Its class block past the cast's: the explanation acted out (the choice) and its tick
struct TalkerFields : CastFields {
    explicit TalkerFields(AnimatedActor& actor) : CastFields(actor) {}
    uint8_t explanation = 0;
    uint32_t ticks = 0;

    void clearBlock() override {
        CastFields::clearBlock();
        explanation = 0;
        ticks = 0;
    }
};

} // namespace od::port::opening_cast_state
