// The player's globals outside the player's slot in the exe's layout (game/player/event_loadout, game/shop): the event
// loadout's save and the buster upgrades with the records after them.
#include "game/player/event_loadout.hpp"
#include "game/shop.hpp"
#include "game/player/player_state.hpp"
#include "translator/layout.hpp"

namespace od::test {

using namespace od::port;

void playerGlobalsLayout(LayoutVisitor& v) {
    v.field(0xa72220, playerTail().bytes); // the 0x10 bytes after the player's struct
    LoadoutSave& s = loadoutSave();
    v.field(0xa5a028, s.maxHitPoints);
    v.field(0xa5a02a, s.activeSpecial);
    v.field(0xa5a02b, s.specialWeapon);
    v.field(0xa5a02d, s.helmetPart);
    v.field(0xa5a02e, s.legPart);
    v.field(0xa5a02f, s.armourPart);
    v.field(0xa5a030, s.hitHeightLevel);
    v.field(0xa5a032, s.busterLevelWords);

    ShopUpgrades& u = shopUpgrades();
    v.field(0xa6d1f0, u.first.level);
    v.field(0xa6d1f1, u.first.bought);
    v.field(0xa6d1f2, u.second.level);
    v.field(0xa6d1f3, u.second.bought);
    v.field(0xa6d1f4, u.unreadBytes);
}

} // namespace od::test
