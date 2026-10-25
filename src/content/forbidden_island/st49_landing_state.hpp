// The parts of the script / stage block that st49_landing.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/script_state.hpp"
#include "game/transition_state.hpp"
#include "game/vectors.hpp"

namespace od::port::st49_landing_state {

// Script 0x18's own bytes: the destination of its end (written as three dwords)
struct Script18Bytes : ScriptPart<Script18Bytes> {
    uint8_t destStage = 0; // +0xc
    uint8_t destRoom = 0; // +0xe
    RoomPlace destination; // +0x10
    // stage / room / place as the exe stores them (three dwords)
    void setDestinationWords(uint8_t stage, uint8_t room, int16_t x, int16_t y, int16_t z, int16_t yaw) {
        destStage = stage;
        destRoom = room;
        destination = {{}, x, y, z, yaw};
    }
};

} // namespace od::port::st49_landing_state
