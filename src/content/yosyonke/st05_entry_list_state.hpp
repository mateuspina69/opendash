// The state st05_entry_list.cpp keeps (its classes', its script parts, its globals): the members and the exe
// addresses they stand for.
#pragma once

#include <cstdint>

#include "game/list_cursor.hpp"

#include "game/actor_state.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port::st05_entry_list_state {

// The exe globals st05_entry_list.cpp keeps (their exe addresses in the comments)
struct St05EntryListGlobals {
    StateArray<uint8_t, 0x28> entryListRows{}; // 0xa59fa8
};
inline St05EntryListGlobals& st05EntryListGlobals() {
    static St05EntryListGlobals s;
    return s;
}

// The list's block (0xa59fd0): the menu's state machine, a sub-window up (the rows dimmed), the cursor (+4 .. +9) and
// the entry chosen
struct EntryListBlock {
    uint8_t menuState = 0; // +0x0
    uint8_t menuStep = 0;  // +0x1
    int8_t asking = 0;     // +0x2
    uint8_t unread03 = 0;  // +0x3 (no reader traced)
    ListCursor list;       // +0x4 .. +0x9
    uint8_t unread0a = 0;  // +0xa (no reader traced)
    uint8_t unread0b = 0;
    uint16_t entry = 0;    // +0xc

    // the menu state s at step 0 (the exe's dword store at +0)
    void enterMenuState(uint8_t s) {
        menuState = s;
        menuStep = 0;
        asking = 0;
        unread03 = 0;
    }
    // exe 0x40dc40(block, block + 8): the head and the cursor cleared one byte at a time (the entry kept)
    void clear() {
        const uint16_t e = entry;
        *this = EntryListBlock{};
        entry = e;
    }
};
inline EntryListBlock& entryListBlock() {
    static EntryListBlock s; // 0xa59fd0
    return s;
}

} // namespace od::port::st05_entry_list_state
