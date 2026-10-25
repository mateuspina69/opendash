// The class states of st39_script68.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/script_state.hpp"

namespace od::port::st39_script68_state {

// ---- the crew's variant 2 (exe 0x4ea120; the camera script's record 0x8acce4)
// ----------------------------------------------
// state 0 only: bound (box 0x896490, animation 0, the motion cleared), its shadow (kCastShadow) and the shadow's
// sizes, its texture track 0x8964b4 on sequence 0
// the script's own bytes: its clock (counted every tick of state 1)
struct Script68Bytes : ScriptPart<Script68Bytes> {
    int32_t clock = 0; // +0x1c
    // exe 0x40dc40 over +0xc..+0x1b: none of its members there (past it: kept)
    void clearSetUpRun() override {}
};

} // namespace od::port::st39_script68_state
