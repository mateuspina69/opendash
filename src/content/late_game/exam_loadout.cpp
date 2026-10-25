// The digger's licence exam's loadout and rescue (see exam_loadout.hpp).
#include "content/late_game/exam_loadout.hpp"

#include "game/player/event_loadout.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

// the exam's loadout
constexpr int16_t kExamHitPoints = 0x50;
constexpr int8_t kExamHelmet = 1, kExamLegs = 0, kExamArmour = 2;
constexpr int8_t kExamBusterLevels[4] = {2, 0, 0, 2}; // attack, energy, range, rate

// the exam was entered from ST1B (else from ST0B): where its game over rescues to, and its end's message
constexpr uint32_t kFlagExamFromSt1b = 0x248;
// the rescue: the office the exam was entered from (sub-room, place, yaw)
constexpr GameOverRescuePlace kRescueToSt1b{0x1b, 1, 0x40, 0, 0x40, 0xa00};
constexpr GameOverRescuePlace kRescueToSt0b{0x0b, 1, -0x100, 0, 0x80, 0x400};

} // namespace

void examLoadoutGive() {
    Player& p = player();
    LoadoutSave& s = loadoutSave();
    s.maxHitPoints = p.maxHitPoints;
    s.activeSpecial = p.activeSpecial;
    s.specialWeapon = p.specialWeapon;
    s.legPart = p.legPart;
    s.hitHeightLevel = p.hitHeightLevel;
    s.helmetPart = p.helmetPart;
    s.busterLevelWords[1] = p.weaponLevelWord(kLoadoutBusterLevelWord + 1);
    s.armourPart = p.armourPart;
    s.busterLevelWords[0] = p.weaponLevelWord(kLoadoutBusterLevelWord);
    p.maxHitPoints = kExamHitPoints;
    p.hitPoints = kExamHitPoints;
    p.status = 0;
    for (size_t i = 0; i < p.statusTicks.size(); ++i) p.statusTicks[i] = 0;
    p.activeSpecial = 0;
    p.specialWeapon = 0;
    p.helmetPart = kExamHelmet;
    p.legPart = kExamLegs;
    p.armourPart = kExamArmour;
    p.hitHeightLevel = 0;
    const size_t buster = weaponLevelIndex(kBusterLevelWeapon, WeaponStat::Attack);
    for (size_t i = 0; i < 4; ++i) p.weaponLevels[buster + i] = kExamBusterLevels[i];
}

GameOverRescuePlace examRescuePlace() { return storyFlag(kFlagExamFromSt1b) ? kRescueToSt1b : kRescueToSt0b; }

bool examEnteredFromSt1b() { return storyFlag(kFlagExamFromSt1b); }

} // namespace od::port
