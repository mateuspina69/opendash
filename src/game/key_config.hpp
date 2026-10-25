// The options page's key configuration (state 3, exe 0x5ede10; the PC-only page): the columns (the pad, the keys,
// the mouse), then an action whose binding the next device press sets (exe 0x5ef2f0: the code swapped with the action
// that had it), drawn over the device's picture with the codes bound; start / circle keeps, cross restores the tables
// kept on entry. The binding tables: game/input_bindings.
#pragma once

#include <cstdint>

#include "game/input_bindings.hpp"
#include "game/state_array.hpp"

namespace od::port {

// The tables the key configuration keeps on entry (cross restores them) and the action being set
struct KeyConfigKeep {
    StateArray<uint32_t, kKeyBindings> keys{};
    StateArray<uint32_t, kMouseBindings> mouse{};
    int32_t action = 0;
    StateArray<uint32_t, kPadBindings> pad{};
};
KeyConfigKeep& keyConfigKeep();

struct StatusScreen;

// exe 0x5ede10: the key configuration's state one frame
void keyConfigState(StatusScreen& s);

} // namespace od::port
