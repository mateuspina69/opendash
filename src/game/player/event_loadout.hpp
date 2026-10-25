// The loadouts an event swaps in: the player's own equipment kept in one save block while the event's is worn, and
// put back at its end. The events' gives (what they save and what they put on) are theirs: the digger's licence exam's
// (exe 0x542730, content/late_game/exam_loadout) and the time-trial course's (exe 0x542960, content/late_game/st59).
// The restores here: the full one (exe 0x542810: the exam stage's end, the game over's rescue out of the exam) and
// the parts' one (exe 0x5429c0: the time trial).
#pragma once

#include <cstddef>
#include <cstdint>

#include "game/state_array.hpp"
#include "game/player/player_weapons.hpp"

namespace od::port {

// The equipment kept while an event runs (0xa5a028.., after the game loop's save area)
struct LoadoutSave {
    int16_t maxHitPoints = 0;
    int8_t activeSpecial = 0;
    int8_t specialWeapon = 0;
    int8_t helmetPart = 0;
    int8_t legPart = 0;
    int8_t armourPart = 0;
    uint8_t hitHeightLevel = 0;
    StateArray<uint32_t, 2> busterLevelWords{}; // the buster's eight level bytes as two words
};
LoadoutSave& loadoutSave();
// the buster's first level word (Player::weaponLevelWord) the save keeps, with the next one
constexpr size_t kLoadoutBusterLevelWord = weaponLevelIndex(kBusterLevelWeapon, WeaponStat::Attack) / 4;

// exe 0x542960: the parts saved, the status and its timers cleared, the time trial's parts put on (no helmet, no
// armour, the jet skates; the time trial (content/late_game/st59) and the ST3C race (content/saul_kada/pirate_base_rooms))
void loadoutGiveTimeTrial();
// exe 0x542810: the saved equipment back (the status and its timers, the immunity cleared)
void loadoutRestore();
// exe 0x5429c0: the saved parts back, the status and its timers cleared
void loadoutRestoreParts();

} // namespace od::port
