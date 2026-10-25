// The class states of st11_script2a.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::st11_script2a_state {

// ---- exe 0x4b57b0: ST11 pool 0x20 type 0x3b sub 1 (script 0x2a record 0x8a4a40) ------------------------------------
// States [0x89464c]: exe 0x4b5870 (bind: model variant 0, animation 1, drawn, box 0x894640, animating, no shadow) /
// 0x4b58e0 (run) / 0x468fa0 (free). The run's phases (sub-state): it waits for step 3 tick 0xa0 (animation 0); the
// tick table 0x4b5a58 over ticks 0xbe..0x104 (0xbe animation 4, 0xd1 / 0xe5 the animation stopped / resumed, 0x104
// next); from 0x10e the shadow, at 0x11d a dust puff at (0xb50, -0x480, 0x2750), the jump; landed, animation 0x20 and
// a walk off along its yaw (sound 0x16b on the animation's cue). Every tick of the run the move / ground of 0x4b5aa0.
struct ArrivalCastFields : ActorClassState {
    explicit ArrivalCastFields(AnimatedActor&) {}
    uint8_t animating = 0; // the animation steps (0: the pose held)
    uint8_t shadow = 0; // its shadow drawn (render output)
    uint8_t jumpPhase = 0; // JumpPhase

    void clearBlock() override {
        animating = 0;
        shadow = 0;
        jumpPhase = 0;
    }
};

} // namespace od::port::st11_script2a_state
