// The class states of glyde_base_scene_birdbots.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::glyde_base_scene_birdbots_state {

// the script's cue (+0xc: a new value -> the next animation) and its copy, the parameters both classes share
struct SceneCue {
    explicit SceneCue(const AnimatedActor& actor) : SceneCue(const_cast<AnimatedActor&>(actor)) {}
    explicit SceneCue(AnimatedActor& actor) : cue(actor.params[0]), cueSeen(actor.params[1]) {}
    uint8_t& cue;
    uint8_t& cueSeen;
    bool take() {
        if (cue == cueSeen) return false;
        cueSeen = cue;
        return true;
    }
};

// the fields of exe 0x497590
struct SceneBirdbotFields : ActorClassState, SceneCue {
    explicit SceneBirdbotFields(AnimatedActor& actor) : SceneCue(actor), steady(actor.params[3]) {}
    uint8_t& steady;          // the sinking one is not shaken (0: shaken)
    int16_t baseY = 0;        // the sinking one's height before its shake
    uint16_t smokeTicks = 0;  // a smoke every second tick
    uint16_t shakePhase = 0;
    uint16_t animKey = 0;     // the key the next animation starts at
    uint8_t blink = 0;        // the blinking pair's phase (bit 0)
    uint8_t blinkStarted = 0; // set when a blink run starts (its reader is not traced)

    void clearBlock() override {
        baseY = 0;
        smokeTicks = 0;
        shakePhase = 0;
        animKey = 0;
        blink = 0;
        blinkStarted = 0;
    }
};

} // namespace od::port::glyde_base_scene_birdbots_state
