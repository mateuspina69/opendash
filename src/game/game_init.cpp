// A run's start (see game_init.hpp), one function per exe function; every write keeps the exe's width and order.
#include "game/game_init.hpp"

#include "game/actor_stats.hpp"
#include "game/audio_logic.hpp"
#include "game/game_state.hpp"
#include "game/items.hpp"
#include "game/player/player_model.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/save_image.hpp"
#include "game/shop.hpp"
#include "game/story_flags.hpp"
#include "game/transition_state.hpp"
#include "game/visited_rooms.hpp"

namespace od::port {

namespace {

// ---- a new run's game state ----
// the best times' default records (the race courses' rows: the holder's three letters on the first, the times)
constexpr uint16_t kDefaultHolder[3] = {0x53, 0x55, 0x45};
constexpr int32_t kDefaultBestTimes[kBestTimes] = {0x6125, 0x70e4, 0x7d40, 0x7504, 0x8ee3, 0xa087, 0x7862, 0x9901, 0xa4a5};
constexpr uint32_t kNoRecord = 0xffffffff; // the eight unread records of a new run
constexpr uint8_t kArrivalFadeIn = 2;      // the arrival's fade in (TransitionRequest::fadeIn) of a new / loaded run
constexpr uint8_t kKeepLoadedPosition = 0xff; // the arrival placement keeps the loaded position (exe 0x542370)
constexpr uint8_t kNewRunArrival = 1;

// ---- the player's init ----
constexpr uint8_t kPlayerFlags = kActorLiveDrawn;
constexpr uint8_t kPlayerRunFlags = kActorRun | kActorShown;
// the difficulties (exe 0x573fd0's cases)
constexpr uint8_t kEasiestDifficulty = 0, kVeryHardDifficulty = 3;
constexpr int16_t kEasiestHitPoints = 0xa0, kNewRunHitPoints = 0x50;
constexpr uint32_t kDifficultyStoryFlags = 0x222; // + the difficulty (2, 3): their story flag
constexpr uint8_t kFullLevel = 7;
constexpr uint8_t kNewRunBusterWeapon = 2;
// the buster's level bytes (Player::weaponLevels: the buster's slot)
constexpr size_t kBusterLevels = weaponLevelIndex(kBusterLevelWeapon, WeaponStat::Attack);

// the buster's four level bytes
void setBusterLevels(Player& p, uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    p.weaponLevels[kBusterLevels + 0] = int8_t(a);
    p.weaponLevels[kBusterLevels + 1] = int8_t(b);
    p.weaponLevels[kBusterLevels + 2] = int8_t(c);
    p.weaponLevels[kBusterLevels + 3] = int8_t(d);
}
// the special weapon (owned too), the buster weapon, the special weapon allowed
void setWeapons(Player& p, uint8_t special, uint8_t allowed) {
    p.specialWeapon = int8_t(special);
    p.busterWeapon = int8_t(kNewRunBusterWeapon);
    p.specialAllowed = allowed;
}
// the helmet / the legs / the armour
void setBody(Player& p, uint8_t helmet, uint8_t legs, uint8_t armour) {
    p.helmetPart = int8_t(helmet);
    p.legPart = int8_t(legs);
    p.armourPart = int8_t(armour);
}
// the maximum and the hit points
void setHitPoints(Player& p, int16_t v) {
    p.maxHitPoints = v;
    p.hitPoints = v;
}
void own(uint32_t item) { setStoryFlag(item); }

} // namespace

void gameStateInit(int load) {
    GameState& g = gameState();
    GameOptions& o = gameOptions();
    ShopUpgrades& charges = shopUpgrades();
    TransitionRequest& arrival = transitionRequest();
    BestTimes& times = bestTimes();
    if (load == 0) {
        g.playTime = 0;
        g.playTimeCopy = 0;
        g.zenny = 0;
        g.unreadCounter = 0;
        g.damageTaken = 0;
        g.damageShown = 0;
        g.defeats = 0;
        g.defeatsTotal = 0;
        g.balance = 0;
        g.toolKitBalance = 0;
        g.balanceTier = int8_t(BalanceTier::Middle);
        g.toolKitTier = int8_t(BalanceTier::Middle);
        charges.first.level = 0;
        charges.first.bought = 0;
        charges.second.level = 0;
        charges.second.bought = 0;
        for (size_t i = 0; i < charges.unreadBytes.size(); ++i) charges.unreadBytes[i] = 0;
        g.scriptCounters[0] = 0;
        g.scriptCounters[1] = 0;
        g.setDockCountersWord(0, 0);
        g.setDockCountersWord(1, 0);
        g.countedDock = 0;
        arrival.state = kTransitionNone;
        for (size_t i = 0; i < GameState::kEntryLogWords; ++i) g.setEntryLogWord(i, kNoRecord);
        times.records[0].name[3] = 0;
        for (size_t i = 0; i + 1 < kBestTimes; ++i) times.records[i].time = kDefaultBestTimes[i];
        for (size_t i = 0; i < 3; ++i) times.records[0].name[i] = kDefaultHolder[i];
        times.records[kBestTimes - 1].time = kDefaultBestTimes[kBestTimes - 1];
        arrival.keepPosition = kNewRunArrival;
        arrival.fadeIn = kArrivalFadeIn;
        return;
    }
    const SaveImage& b = saveImageAt(kSaveBuf);
    g.stage = b.stage;
    g.room = b.room;
    g.playerKind = b.playerKind;
    g.story = b.story;
    g.difficulty = b.difficulty;
    g.flutterDock = b.flutterDock;
    g.nextStory = b.nextStory;
    // the records kept across saves (unsigned): the smaller time, the larger others
    if (g.bestTime > b.bestTime) g.bestTime = b.bestTime;
    if (g.bestDifficulty < b.bestDifficulty) g.bestDifficulty = b.bestDifficulty;
    if (g.clears < b.clears) g.clears = b.clears;
    g.playTime = b.playTime;
    g.playTimeCopy = b.playTimeCopy;
    g.zenny = int32_t(uint32_t(b.counters[0]));
    g.unreadCounter = b.counters[1];
    g.damageTaken = b.counters[2];
    g.damageShown = b.counters[3];
    g.defeats = b.counters[4];
    g.defeatsTotal = b.counters[5];
    g.balance = int16_t(b.balance);
    g.toolKitBalance = int16_t(b.toolKitBalance);
    g.balanceTier = int8_t(b.balanceTier);
    g.toolKitTier = int8_t(b.toolKitTier);
    o.minimapShown = b.minimapShown;
    charges.first.level = int8_t(b.charges[0]);
    charges.first.bought = int8_t(b.charges[1]);
    charges.second.level = int8_t(b.charges[2]);
    charges.second.bought = int8_t(b.charges[3]);
    for (size_t i = 0; i < charges.unreadBytes.size(); ++i) charges.unreadBytes[i] = b.charges[4 + i];
    g.scriptCounters[0] = b.scriptCounters[0];
    g.scriptCounters[1] = b.scriptCounters[1];
    g.setDockCountersWord(0, b.dockCounters[0]);
    g.setDockCountersWord(1, b.dockCounters[1]);
    g.countedDock = b.countedDock;
    for (size_t i = 0; i < GameState::kEntryLogWords; ++i) g.setEntryLogWord(i, b.unreadRecords[i]);
    for (size_t i = 0; i < kBestTimeWords; ++i) times.setWord(i, b.bestTimes[i]);
    arrival.state = kTransitionNone;
    arrival.keepPosition = kKeepLoadedPosition; // exe 0x542370: keep the loaded position (no arrival point)
    arrival.fadeIn = kArrivalFadeIn;
}

void storyFlagsInit(int load) {
    if (load == 0) {
        clearStoryFlagGroup(kFlagGroupAll); // exe 0x512810(0)
        return;
    }
    const SaveImage& b = saveImageAt(kSaveBuf);
    StoryFlags& flags = storyFlags();
    for (size_t i = 0; i < b.storyFlags.size(); ++i) flags.setWord(i, b.storyFlags[i]);
    clearStoryFlag(kFlagStoryMenuOpen);
}

void visitedRoomsInit(int load) {
    VisitedRoomBlock& v = visitedRooms();
    if (load == 0) {
        v.routeFlags = 0;
        v.listId = kNoRoomList;
        v.targetStage = kNoRouteRoom;
        v.targetRoom = kNoRouteRoom;
        v.nextLink = kNoRouteRoom;
        v.roomCount = 0;
        v.linkCount = 0;
        v.flagBase = kRoomFlagBaseNone;
        v.rooms = nullptr;
        v.linkRecords = 0;
        return;
    }
    const SaveImage& b = saveImageAt(kSaveBuf);
    for (size_t i = 0; i < kVisitedRoomWords; ++i) v.setWord(i, b.visitedRooms[i]);
}

void extraFlagsInit(int load) {
    ExtraFlags& extra = extraFlags();
    if (load == 0) {
        extra.clearBytes();
        return;
    }
    const SaveImage& b = saveImageAt(kSaveBuf);
    for (size_t i = 0; i < b.extraFlags.size(); ++i) extra.setWord(i, b.extraFlags[i]);
}

void playerInit(int load) {
    ShopUpgrades& charges = shopUpgrades();
    Player& p = player();
    GameState& g = gameState();
    p.flags = kPlayerFlags;
    p.clearTypeWord();
    p.type = g.playerKind;
    p.runFlags = kPlayerRunFlags;
    p.setParamsWord(0);
    p.enterState(PlayerState::Init);
    p.variant = loadedBodyVariant();
    p.collisionBox.pointAt(TableId::PlayerCollisionBox);
    p.immunity = 0;
    p.immunityTicks = 0;
    p.invulnerable = 0;
    if (load != 0) {
        const SaveImage& b = saveImageAt(kSaveBuf);
        p.rotation.z = 0;
        p.rotation.x = 0;
        p.pos.x = b.playerPos.x;
        p.rotation.y = int16_t(b.playerYaw);
        p.pos.y = b.playerPos.y;
        p.pos.z = b.playerPos.z;
        p.hitPoints = int16_t(b.hitPoints);
        p.maxHitPoints = int16_t(b.maxHitPoints);
        p.pulled = b.pulled;
        p.status = b.status;
        p.wadeDepth = int8_t(b.wadeDepth);
        p.slippery = b.slippery;
        p.drag = int8_t(b.drag);
        p.inWater = b.inWater;
        p.statusTicks[0] = int16_t(b.statusTicks[0]);
        p.burden = int8_t(b.burden);
        p.statusTicks[1] = int16_t(b.statusTicks[1]);
        p.statusTicks[3] = int16_t(b.statusTicks[3]);
        p.statusTicks[2] = int16_t(b.statusTicks[2]);
        p.specialWeapon = int8_t(b.specialWeapon);
        p.specialAllowed = b.specialAllowed;
        p.busterWeapon = int8_t(b.busterWeapon);
        p.energyReserve = int16_t(b.gauge[0]);
        p.gaugeMax = int16_t(b.gauge[2]);
        p.energyPool = int16_t(b.gauge[1]);
        p.gaugeRate = int16_t(b.gauge[3]);
        p.gauge = int16_t(b.gauge[5]);
        p.shotCost = int16_t(b.gauge[4]);
        p.helmetPart = int8_t(b.helmetPart);
        p.armourPart = int8_t(b.armourPart);
        p.legPart = int8_t(b.legPart);
        p.setBusterPartsWord(b.busterParts);
        for (size_t i = 0; i < kOwnedPartWords; ++i) // the owned parts' bytes as the image's dwords
            p.setOwnedPartsWord(i, uint32_t(b.ownedParts[i * 4]) | uint32_t(b.ownedParts[i * 4 + 1]) << 8 |
                                       uint32_t(b.ownedParts[i * 4 + 2]) << 16 | uint32_t(b.ownedParts[i * 4 + 3]) << 24);
        for (size_t k = 0; k < kSaveWeaponLevels; ++k)
            for (size_t i = 0; i < kSaveLevelBytes; ++i) p.weaponLevels[k * 8 + i] = b.weaponLevels[k * 8 + i];
    } else {
        p.status = 0;
        p.pulled = 0;
        p.wadeDepth = 0;
        p.drag = 0;
        p.slippery = 0;
        p.inWater = 0;
        p.burden = 0;
        for (size_t i = 0; i < 4; ++i) p.statusTicks[i] = 0;
        p.helmetPart = 0;
        p.legPart = 0;
        p.armourPart = 0;
        for (size_t i = 0; i < kOwnedPartWords; ++i) p.setOwnedPartsWord(i, 0);
        for (size_t k = 0; k < kSaveWeaponLevels; ++k)
            for (size_t i = 0; i < kSaveLevelBytes; ++i) p.weaponLevels[k * 8 + i] = 0;
        const uint8_t difficulty = g.difficulty;
        switch (difficulty) {
        case kEasiestDifficulty:
            setHitPoints(p, kEasiestHitPoints);
            p.setBusterPartsWord(0x1f);
            own(kDifficultyMarkItems - difficulty);
            own(kBusterPartItems + 0x1f);
            setBusterLevels(p, kFullLevel, kFullLevel, kFullLevel, kFullLevel);
            break;
        case 1:
        case kHardestDifficulty:
            p.setBusterPartsWord(0);
            setHitPoints(p, kNewRunHitPoints);
            own(kDifficultyMarkItems - difficulty);
            break;
        case 2:
        case kVeryHardDifficulty:
            setHitPoints(p, kNewRunHitPoints);
            p.setBusterPartsWord(0);
            own(kDifficultyMarkItems - difficulty);
            setStoryFlag(kDifficultyStoryFlags + difficulty);
            break;
        default: break;
        }
        // the entry mode's kit: the special weapon (owned), the buster parts, the levels, the body parts, the items
        switch (EntryMode(g.entryMode)) {
        case EntryMode::NewGame:
            setWeapons(p, 0xf, 1);
            own(kSpecialWeaponItems + 0xf);
            break;
        case EntryMode::Tutorial:
            setWeapons(p, 6, 0);
            own(kSpecialWeaponItems + 6);
            break;
        case EntryMode::Kit2:
            setWeapons(p, 7, 1);
            own(kSpecialWeaponItems + 7);
            own(kBusterPartItems + 2);
            own(kBusterPartItems + 5);
            setBusterLevels(p, 4, 4, 0, 0);
            own(kHelmetItems + 1);
            own(kLegItems + 2);
            own(kArmourItems + 4);
            setBody(p, 1, 2, 4);
            break;
        case EntryMode::Kit3:
            setWeapons(p, 0, 0);
            own(kBusterPartItems + 1);
            own(kBusterPartItems + 0xe);
            setBusterLevels(p, 3, 0, 2, 0);
            own(kHelmetItems + 2);
            own(kLegItems + 1);
            own(kArmourItems + 2);
            setBody(p, 2, 1, 2);
            break;
        case EntryMode::Demo:
            setWeapons(p, 5, 1);
            own(kSpecialWeaponItems + 5);
            own(kBusterPartItems + 1);
            own(kBusterPartItems + 0xe);
            setBusterLevels(p, 3, 0, 2, 0);
            p.weaponLevels[0x2c] = 3; // the special weapon 5's fifth level
            own(kLegItems + 1);
            own(kArmourItems + 4);
            p.legPart = 1;
            p.armourPart = 4;
            break;
        case EntryMode::Kit5:
            setWeapons(p, 6, 1);
            own(kSpecialWeaponItems + 6);
            own(kBusterPartItems + 0x1f);
            p.setBusterPartsWord(0x1f);
            setBusterLevels(p, kFullLevel, kFullLevel, kFullLevel, kFullLevel);
            own(kHelmetItems + 2);
            own(kLegItems + 1);
            own(kArmourItems + 5);
            setBody(p, 2, 1, 5);
            own(kLifeCanisterItem);
            charges.first.level = 5;
            charges.first.bought = 5;
            break;
        case EntryMode::Kit6:
            setWeapons(p, 5, 1);
            own(kSpecialWeaponItems + 5);
            own(kBusterPartItems + 3);
            own(kBusterPartItems + 9);
            p.setBusterPartsWord(0x903);
            setBusterLevels(p, kFullLevel, 0, kFullLevel, 0);
            own(kHelmetItems + 2);
            own(kLegItems + 1);
            own(kArmourItems + 6);
            setBody(p, 2, 1, 6);
            own(kLifeCanisterItem);
            own(kEnergyRefillItem);
            charges.first.level = 5;
            charges.first.bought = 5;
            break;
        case EntryMode::Kit7:
            setWeapons(p, 0xd, 1);
            own(kSpecialWeaponItems + 0xd);
            own(kBusterPartItems + 2);
            own(kBusterPartItems + 8);
            p.setBusterPartsWord(0x802);
            setBusterLevels(p, 4, 0, 4, 0);
            own(kHelmetItems + 2);
            own(kLegItems + 1);
            own(kArmourItems + 4);
            setBody(p, 2, 1, 4);
            own(kLifeCanisterItem);
            charges.first.level = 10;
            charges.first.bought = 10;
            break;
        default: break;
        }
        setupSpecialGauge(true); // every case (and the default) ends in exe 0x573600(1)
    }
    soundSetListener(ListenerRef::playerPosition(), ListenerRef::playerRotation(), 0); // exe 0x5d5a60
}

void gameModeInitState() {
    GameState& g = gameState();
    const int load = g.loadSave;
    gameStateInit(load);
    storyFlagsInit(load);
    visitedRoomsInit(load);
    extraFlagsInit(load);
    // exe 0x5d4f30(load) is empty
    selectDifficultyStats(g.difficulty);
    playerInit(load);
}

} // namespace od::port
