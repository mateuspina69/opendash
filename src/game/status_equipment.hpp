// The status sub-screen's equipment page (page 5, exe 0x5eb180; SUBSCN03): the rows (the special weapon, the body
// parts, the buster parts), the special weapon (the hand / the weapon in the room's weapon mode, the weapons owned and
// their stat bars with the turning model), the body parts (helmet / armour / legs, their lists), the buster parts
// (the three slots, the parts list and its sorts, the buster's levels as the sum of the parts' bonuses). The
// player's fields: game/player/player_state (specialWeapon, specialAllowed, the body parts, busterParts, ownedParts,
// weaponLevels).
#pragma once

namespace od::port {

struct StatusScreen;

// exe 0x5eb180: page 5 one frame (its state's handler, the page's rows / title / frame)
void equipmentPage(StatusScreen& s);

} // namespace od::port
