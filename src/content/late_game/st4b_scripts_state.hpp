// The class states of st4b_scripts.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/script_state.hpp"

namespace od::port::st4b_scripts_state {

struct StillFigureFields : ActorClassState {
    explicit StillFigureFields(AnimatedActor&) {}
    uint32_t classWord = 0;

    void clearBlock() override {
        classWord = 0;
    }
};

// The scripts' own bytes: the RNG state saved for the scene
struct St4bScriptFields : ScriptPart<St4bScriptFields> {
    uint32_t savedRng = 0; // +0xc
};

} // namespace od::port::st4b_scripts_state
