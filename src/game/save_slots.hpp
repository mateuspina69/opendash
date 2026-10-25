// The save slots' summaries (0xa7b040: 5 x 8 bytes): what the load / save menus (game/save_menus) copy out of each
// slot's image (game/save_image) and the messages print (fb 45: the place and the play time of a slot).
#pragma once

#include <cstddef>
#include <cstdint>

#include "game/state_array.hpp"

namespace od::port {

constexpr size_t kSaveSlots = 5;

struct SaveSlotSummary {
    uint8_t stage = 0;      // the image's stage / room / difficulty (their reader is not traced)
    uint8_t room = 0;
    uint8_t difficulty = 0;
    uint8_t place = 0;      // the image's place name's index (the bank of fb 44)
    int32_t time = 0;       // the image's play time
};
struct SaveSlotSummaries {
    StateArray<SaveSlotSummary, kSaveSlots> slots{};
};
inline SaveSlotSummaries& saveSlotSummaries() {
    static SaveSlotSummaries s;
    return s;
}

// The slots that hold a save (bit n = slot n: the save scan exe 0x548910 sets it, a write adds its slot)
struct SaveSlotBits {
    uint32_t present = 0;
};
inline SaveSlotBits& saveSlotBits() {
    static SaveSlotBits s;
    return s;
}

} // namespace od::port
