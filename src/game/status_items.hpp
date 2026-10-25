// The status sub-screen's items page (page 4, exe 0x5e9f50; SUBSCN02): the rows (the items / the key items), the
// two-column list of the items owned (their icons, names, the description of the one under the cursor, the charges of
// the canister and the cure), and the use of an item by its class (exe table 0x91ade0: the life refills, the status
// cure, the special weapon's energy refill, the letters, the "cannot use" messages).
#pragma once

namespace od::port {

struct StatusScreen;

// exe 0x5e9f50: page 4 one frame (the rows or the list, the page's rows / title / frame)
void itemsPage(StatusScreen& s);

} // namespace od::port
