// The loadouts an event swaps in (see event_loadout.hpp).
#include "game/player/event_loadout.hpp"

#include "game/player/player_state.hpp"

namespace od::port {

LoadoutSave& loadoutSave() {
    static LoadoutSave s;
    return s;
}

void loadoutRestore() {
    Player& p = player();
    const LoadoutSave& s = loadoutSave();
    p.activeSpecial = s.activeSpecial;
    p.status = 0;
    for (size_t i = 0; i < p.statusTicks.size(); ++i) p.statusTicks[i] = 0;
    p.immunity = 0;
    p.immunityTicks = 0;
    p.specialWeapon = s.specialWeapon;
    p.maxHitPoints = s.maxHitPoints;
    p.legPart = s.legPart;
    p.helmetPart = s.helmetPart;
    p.armourPart = s.armourPart;
    p.hitHeightLevel = s.hitHeightLevel;
    p.setWeaponLevelWord(kLoadoutBusterLevelWord, s.busterLevelWords[0]);
    p.setWeaponLevelWord(kLoadoutBusterLevelWord + 1, s.busterLevelWords[1]);
}

void loadoutRestoreParts() {
    Player& p = player();
    const LoadoutSave& s = loadoutSave();
    p.legPart = s.legPart;
    p.status = 0;
    for (size_t i = 0; i < p.statusTicks.size(); ++i) p.statusTicks[i] = 0;
    p.helmetPart = s.helmetPart;
    p.armourPart = s.armourPart;
}

void loadoutGiveTimeTrial() {
    constexpr int8_t kTimeTrialHelmet = 0, kTimeTrialArmour = 0;
    Player& p = player();
    LoadoutSave& s = loadoutSave();
    s.helmetPart = p.helmetPart;
    s.legPart = p.legPart;
    p.status = 0;
    for (size_t i = 0; i < p.statusTicks.size(); ++i) p.statusTicks[i] = 0;
    s.armourPart = p.armourPart;
    p.helmetPart = kTimeTrialHelmet;
    p.legPart = int8_t(kLegsJetSkates);
    p.armourPart = kTimeTrialArmour;
}

} // namespace od::port
