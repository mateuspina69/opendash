// The scrolling list's cursor of dash2.exe (exe 0x53e330): the hand on a list of rows shown a window at a time (the
// shop's items, a stage script's entries), moved with the d-pad (L1 / R1 page), its row change and scroll animated over
// a few ticks, confirmed with circle / start or cancelled with cross; OpenDash's mouse (game/menu_pointer MenuPointer) points
// and clicks a row.
#pragma once

#include <cstdint>


namespace od::port {

// The cursor's bytes in its owner's block (+4 .. +9)
struct ListCursor {
    uint8_t row = 0;         // +0x4: the hand's visible row (kListNoRow: none)
    uint8_t firstRow = 0;    // +0x5: the first row shown
    uint8_t rows = 0;        // +0x6: the rows of the list
    uint8_t visibleRows = 0; // +0x7
    int8_t rowStep = 0;      // +0x8: the hand's move animation (ticks left, signed by the direction)
    int8_t scrollStep = 0;   // +0x9: the scroll's animation (3 a tick past 2, else 1)
};
constexpr uint8_t kListNoRow = 0xff;

// What a tick of the cursor gave
enum class ListAnswer : int {
    Cancelled = -1, // cross
    None = 0,
    Confirmed = 1,  // circle / start (or a click) on the hand's row
};

// exe 0x53e330(block): one tick of the list's cursor
ListAnswer listCursorStep(ListCursor& list);

} // namespace od::port
