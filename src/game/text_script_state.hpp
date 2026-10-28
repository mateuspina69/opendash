// The state game/text_script keeps: the texts its codes insert (fb 21's number, fb 23's time), built at the exe's
// addresses of them (a window's read pointer names them: game/loaded_data BuiltData).
#pragma once

#include <cstdint>
#include <span>

#include "game/loaded_data.hpp"
#include "game/state_array.hpp"

namespace od::port::text_script_state {

struct InsertedTexts {
    StateArray<uint8_t, 0xc> number{}; // 0x8b172c
    StateArray<uint8_t, 0xa> time{};   // 0x8b1738
    StateArray<uint8_t, 12> warpOperands{}; // 0x8b171e: fb 3c's operands, kept
    StateArray<uint8_t, 0x28> recordName{}; // 0x8b1744 (PS1 SLUS 0x8006b890): fb 49's
};
// the texts (registered as the game's built data at their addresses on their first use)
inline InsertedTexts& insertedTexts() {
    static InsertedTexts s;
    static const bool registered = [] {
        registerBuiltData(stateAddress(TableId::TextScriptNumberText), std::span<uint8_t>(s.number.data(), s.number.size()));
        registerBuiltData(stateAddress(TableId::TextScriptTimeText), std::span<uint8_t>(s.time.data(), s.time.size()));
        registerBuiltData(stateAddress(TableId::TextScriptRecordNameText),
                          std::span<uint8_t>(s.recordName.data(), s.recordName.size()));
        return true;
    }();
    (void)registered;
    return s;
}

} // namespace od::port::text_script_state
