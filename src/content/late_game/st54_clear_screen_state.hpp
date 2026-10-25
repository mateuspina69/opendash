// The parts of the script / stage block that st54_clear_screen.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/script_state.hpp"

namespace od::port::st54_clear_screen_state {

// The script's own bytes: the clear time
struct ClearScreenFields : ScriptPart<ClearScreenFields> {
    uint8_t hours = 0; // +0xc
    uint8_t minutes = 0; // +0xd
    uint8_t seconds = 0; // +0xe
};

// The exe globals st54_clear_screen.cpp keeps (their exe addresses in the comments)
struct St54ClearScreenGlobals {
    StateArray<uint8_t, 8> clearedBytes{}; // 0xa59fe0
};
inline St54ClearScreenGlobals& st54ClearScreenGlobals() {
    static St54ClearScreenGlobals s;
    return s;
}

} // namespace od::port::st54_clear_screen_state
