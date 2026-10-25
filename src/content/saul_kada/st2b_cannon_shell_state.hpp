// The class states of st2b_cannon_shell.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st2b_cannon_shell_state {

// Its own bytes (0x20 cleared at the start): its hit box, its target (copied), the bounce's blast size and ticks, its
// warning given
struct ShellFields : ActorClassState {
    explicit ShellFields(ProjectileActor& actor) : hitBox(actor.hitBox) {}
    HitBoxHead& hitBox;
    int16_t targetX = 0;
    int16_t targetZ = 0;
    uint16_t blastSize = 0;
    uint8_t bounceTicks = 0;
    StateArray<uint8_t, 1> clearedPad0{}; // bytes the block clear covers (no reader)
    uint8_t warned = 0;
    StateArray<uint8_t, 15> clearedPad1{}; // bytes the block clear covers (no reader)
    void clearBytes() {
        targetX = 0;
        targetZ = 0;
        blastSize = 0;
        bounceTicks = 0;
        clearedPad0 = {};
        warned = 0;
        clearedPad1 = {};
        hitBox = {};
    }
};

} // namespace od::port::st2b_cannon_shell_state
