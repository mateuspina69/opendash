// The parts of the script / stage block that st03_attract_script.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/script_state.hpp"

namespace od::port::st03_attract_script_state {

// The script's own bytes: the controller table's cursor and the rings' radius it hands variant 0, the engine sound
// played (the wrap-up plays its stop), the loop sound's request (LoopRequest), handle, pan and dip level (set a byte at
// a time), the sparkles' clock
struct Script4Bytes : ScriptPart<Script4Bytes> {
    uint32_t ringCursor = 0; // +0xc
    uint8_t ringRadius = 0; // +0x10
    uint8_t engineSound = 0; // +0x11
    uint8_t loop = 0; // +0x12
    uint8_t loopHandle = 0; // +0x13
    int8_t loopPan = 0; // +0x14
    int16_t loopDip = 0; // +0x16
    int32_t sparkleClock = 0; // +0x1c
    // exe 0x40dc40 over +0xc..+0x1b: the members there zeroed (past it: kept)
    void clearSetUpRun() override {
        ringCursor = {};
        ringRadius = {};
        engineSound = {};
        loop = {};
        loopHandle = {};
        loopPan = {};
        loopDip = {};
    }
    // the dip level's two bytes (the exe's byte stores)
    void setLoopDipBytes(uint8_t low, uint8_t high) {
        loopDip = int16_t(uint16_t(low | high << 8));
    }
};

} // namespace od::port::st03_attract_script_state
