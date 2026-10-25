// OpenDash: the mouse on the sub-screens (the status sub-screen, the title's menus, the shop / development room lists).
// The host hit-tests the rows the screen drew (ScreenPrim::row) and sets `row` when the mouse moved onto one
// (`moved`); the cursor lists then take that row as if the d-pad had moved there (the move sound, the row's help).
// Clicks are the host's: a confirm / cancel press of the pad.
#pragma once

namespace od::port {

struct MenuPointer {
    int row = -1;
    bool moved = false;
    bool click = false; // a scrolling list's cell clicked: confirmed once the cursor is on it
};
// the pointer (one for the program)
inline MenuPointer& menuPointer() {
    static MenuPointer p;
    return p;
}
// the scrolling lists' cells (ScreenPrim::row): kListCell + visible row * 2 + column
constexpr int kListCell = 0x100;

} // namespace od::port
