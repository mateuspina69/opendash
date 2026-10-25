// The story-12 room handlers of Saul Kada's town (the pirate attack): ST29 (case 0x583730, table 0x8c9840), ST2A
// (0x583aa0, 0x8c9d70), ST2B (0x583f00, 0x8ca1e8), ST2C (0x584430, 0x8ca698), ST48 (0x58ada0, 0x8ccfe0), and ST29's
// case tail. One case per exe handler (its address in the case); each keeps its step in the room handler's byte 0.
#include "content/saul_kada/saul_kada_town_rooms.hpp"

#include "content/saul_kada/mission_rooms.hpp"

#include "game/breakable_walls.hpp"
#include "game/encounter.hpp"
#include "game/game_state.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/spawn.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

constexpr int16_t kMusicDipStep = 0x222;
constexpr uint32_t kFlagDoor = 0x700;
constexpr uint32_t kSandPitScreen = 2; // the encounter kind ST48's sand pit suspends
constexpr uint8_t kSt29 = 0x29;
constexpr uint8_t kTownLateStory = 0xc;
constexpr uint8_t kTownLateScriptBlock = 8;

uint8_t step() { return roomScriptState().handlerStep; }
void setStep(uint8_t s) { roomScriptState().handlerStep = s; }
void nextStep() { roomScriptState().handlerStep += 1u; }
// a one-shot room: its set-up once (step 0 -> 1)
template <class SetUp> bool once(bool run, SetUp setUp) {
    if (!run || step() != 0) return true;
    setUp();
    nextStep();
    return true;
}

// exe 0x583790 (ST29 story 12 sub 0): the mission's flags, its record; the first visit: the mission opening, else
// the music dips at once
void st29Square() {
    switch (step()) {
    case 0:
        clearStoryFlag(0x710);
        clearStoryFlag(0x711);
        setStoryFlag(0x5c8);
        setStoryFlag(0x5c9);
        setStoryFlag(0x5ca);
        spawnRecords(stateAddress(TableId::SaulKadaTownRoomsSt29SquareSpawns), 1);
        if (storyFlag(0x5c0)) {
            setStep(2);
            missionMusicDip(kMusicDipStep);
            return;
        }
        setStoryFlag(0x5c0);
        nextStep();
        [[fallthrough]];
    case 1:
        if (missionOpening(true, 0x3c)) nextStep(); // exe 0x583930
        return;
    default: return;
    }
}
// exe 0x583830 (ST29 story 12 sub 1): its defeats kept as flags (0x8c9d38, 4 rows), the script slot's group, the
// music back until it is cleared
void st29Street() {
    if (step() == 0) {
        clearStoryFlag(0x710);
        clearStoryFlag(0x711);
        defeatsFromFlags(stateAddress(TableId::SaulKadaTownRoomsSt29StreetRows), 4);
        encounterSpawnGroupScriptSlot(0);
        if (!storyFlag(0x5b0)) missionMusicBack();
        nextStep();
    }
    groupClearedMusic(0x5b0);
    if (storyFlag(kFlagDoor)) flagsFromDefeats(stateAddress(TableId::SaulKadaTownRoomsSt29StreetRows), 4);
}
// exe 0x583ad0 (ST2A story 12 sub 0): enemy 1's defeat kept in flag 0x5b4, the script slot's group, the music dips
void st2aEntry() {
    if (step() == 0) {
        clearStoryFlag(0x710);
        if (storyFlag(0x5b4)) enemyMarkDefeated(1);
        encounterSpawnGroupScriptSlot(0);
        missionMusicDip(kMusicDipStep);
        nextStep();
    }
    if (storyFlag(kFlagDoor)) {
        if (enemyDefeated(1)) setStoryFlag(0x5b4);
        scriptRun(0);
    }
}
// exe 0x583b60 (ST2A story 12 sub 1): its defeats (0x8ca1b0, 6 rows), group 1, the music back until cleared, the three
// walls opened by their flags (and their flags once they are broken)
void st2aStreet() {
    if (step() == 0) {
        clearStoryFlag(0x710);
        defeatsFromFlags(stateAddress(TableId::SaulKadaTownRoomsSt2aStreetRows), 6);
        encounterSpawnGroupScriptSlot(1);
        if (!storyFlag(0x5bf)) missionMusicBack();
        if (storyFlag(0x5e0)) switchGridCellVariant(0x3f, 0x3d, 1); // exe 0x58d5f0
        if (storyFlag(0x5e1)) switchGridCellVariant(0x3f, 0x3f, 1);
        if (storyFlag(0x5e2)) switchGridCellVariant(0x3f, 0x41, 1);
        nextStep();
    }
    groupClearedMusic(0x5bf);
    const auto brokenFlag = [](uint32_t flag, uint32_t column, uint32_t row) {
        if (!storyFlag(flag) && gridCellSegmentBroken(column, row)) setStoryFlag(flag);
    };
    brokenFlag(0x5e0, 0x3f, 0x3d);
    brokenFlag(0x5e1, 0x3f, 0x40);
    brokenFlag(0x5e2, 0x3f, 0x42);
    if (storyFlag(kFlagDoor)) {
        flagsFromDefeats(stateAddress(TableId::SaulKadaTownRoomsSt2aStreetRows), 6);
        scriptRun(0);
    }
}
// exe 0x583e70 (ST2A story 12 subs 2..4): the music dips once
void st2aBackRooms() {
    if (step() == 0) {
        clearStoryFlag(0x710);
        missionMusicDip(kMusicDipStep);
        nextStep();
    }
    roomDoorScript(); // exe 0x582490
}
// exe 0x583f70 (ST2B story 12 sub 1): the mine's mission -- its seven defeats by flags 0x583..0x589, the script slot's
// group, the music, the extra records, the opened passage (with zone group 2); the first time the mission opening
// (exe 0x584350); then the cleared music and the defeats as flags
void st2bMine() {
    static constexpr uint32_t kDefeatFlags[7] = {0x583, 0x584, 0x585, 0x586, 0x587, 0x588, 0x589};
    switch (step()) {
    case 0:
        clearStoryFlag(0x710);
        spawnRecords(stateAddress(TableId::SaulKadaTownRoomsSt2bMineSpawns), 1);
        for (uint32_t i = 0; i < 7; ++i)
            if (storyFlag(kDefeatFlags[i])) enemyMarkDefeated(i + 1);
        encounterSpawnGroupScriptSlot(0);
        if (!storyFlag(0x58b)) missionMusicBack();
        if (!storyFlag(0x582)) {
            setStoryFlag(0x711);
            if (storyFlag(0x583)) {
                spawnRecords(stateAddress(TableId::SaulKadaTownRoomsSt2bMineFirstSpawns), 1);
                spawnRecords(stateAddress(TableId::SaulKadaTownRoomsSt2bMineSecondSpawns), 1);
            }
        } else {
            clearStoryFlag(0x711);
        }
        if (!storyFlag(0x583)) spawnRecords(stateAddress(TableId::SaulKadaTownRoomsSt2bMineThirdSpawns), 2);
        if (storyFlag(0x58a)) {
            switchGridCellVariant(0x48, 0x40, 1);
            switchGridCellVariant(0x48, 0x41, 1);
            encounterSpawnGroupZoneSlot(2); // exe 0x5e6640
        }
        if (storyFlag(0x5c1)) {
            setStep(2);
            break;
        }
        setStoryFlag(0x5c1);
        nextStep();
        [[fallthrough]];
    case 1:
        if (missionOpening(false, 0x10)) nextStep();
        break;
    case 2:
        if (enemyDefeated(1)) setStoryFlag(0x5cc);
        if (storyFlag(0x3eb)) {
            setStoryFlag(0x582);
            setStoryFlag(0x5cd);
            clearStoryFlag(0x711);
        }
        break;
    default: break;
    }
    groupClearedMusic(0x58b);
    for (uint32_t i = 0; i < 7; ++i)
        if (enemyDefeated(i + 1)) setStoryFlag(kDefeatFlags[i]);
}
// exe 0x58ade0 (ST48 story 12 sub 0): the sand pit's encounters suspended; the first time its record, flag 0x580 and
// script 0x58; else two records, then the third
void st48Desert() {
    switch (step()) {
    case 0:
        encounterSuspend(kSandPitScreen, 0); // exe 0x5e6870(2, 0)
        if (!storyFlag(0x580)) {
            spawnRecords(stateAddress(TableId::SaulKadaTownRoomsSt48DesertSpawns), 1);
            setStoryFlag(0x580);
            scriptRun(0x58);
            return;
        }
        spawnRecords(stateAddress(TableId::SaulKadaTownRoomsSt48DesertSpawns), 2);
        nextStep();
        return;
    case 1:
        spawnRecords(stateAddress(TableId::SaulKadaTownRoomsSt48DesertMoreSpawns), 1);
        nextStep();
        return;
    default: return;
    }
}

} // namespace

bool saulKadaTownRoomHandler(RoutineId fn, bool run) {
    switch (fn) {
    // ---- ST29 ----------------------------------------------------------------------------------------------------
    case RoutineId::St29RoomHandler:
        if (run) st29Square();
        return true;
    case RoutineId::St29RoomHandlerA:
        if (run) st29Street();
        return true;
    case RoutineId::St29RoomHandlerB: // story 12 sub 2 / later sub 0
        return once(run, [] {
            clearStoryFlag(0x710);
            clearStoryFlag(0x711);
            spawnRecords(stateAddress(TableId::SaulKadaTownRoomsSt29CrowdSpawns), 3);
        });
    case RoutineId::St29RoomHandlerC: // story 12 sub 3 / later sub 1
        return once(run, [] {
            clearStoryFlag(0x710);
            setStoryFlag(0x711);
            spawnRecords(stateAddress(TableId::SaulKadaTownRoomsSt29LargeCrowdSpawns), 5);
        });
    // ---- ST2A ----------------------------------------------------------------------------------------------------
    case RoutineId::St2ARoomHandler:
        if (run) st2aEntry();
        return true;
    case RoutineId::St2ARoomHandlerA:
        if (run) st2aStreet();
        return true;
    case RoutineId::St2ARoomHandlerB:
        if (run) st2aBackRooms();
        return true;
    // ---- ST2B ----------------------------------------------------------------------------------------------------
    case RoutineId::St2BStoryCSub0RoomHandler: // story 12 sub 0
        return once(run, [] {
            missionMusicDip(kMusicDipStep);
            clearStoryFlag(0x710);
        });
    case RoutineId::St2BRoomHandler:
        if (run) st2bMine();
        return true;
    case RoutineId::St2BRoomHandlerA: // story 12 subs 2 / 3
        return once(run, [] {
            clearStoryFlag(0x710);
            missionMusicDip(kMusicDipStep);
            setStoryFlag(0x5cc);
        });
    // ---- ST2C ----------------------------------------------------------------------------------------------------
    case RoutineId::St2CStoryCSub0RoomHandler: // story 12 sub 0
        return once(run, [] {
            clearStoryFlag(0x710);
            setStoryFlag(0x581);
            spawnRecords(stateAddress(TableId::SaulKadaTownRoomsSt2CSpawns), (storyFlag(0x5a8) || storyFlag(0x5a0)) ? 1 : 2);
        });
    // ---- ST48 ----------------------------------------------------------------------------------------------------
    case RoutineId::St48RoomHandler:
        if (run) st48Desert();
        return true;
    case RoutineId::St48StoryCSub1RoomHandler: // story 12 sub 1 (the sand pit): its encounters back
        return once(run, [] {
            encounterSuspend(kSandPitScreen, 1); // exe 0x5e6870(2, 1)
            spawnRecords(stateAddress(TableId::SaulKadaTownRoomsSt48TownSpawns), 7);
        });
    case RoutineId::St48RoomHandlerA: // story 12 sub 3 / later sub 1
        return once(run, [] { spawnRecords(stateAddress(TableId::SaulKadaTownRoomsSt48TownMoreSpawns), 3); });
    default: return false;
    }
}

void saulKadaTownRoomCaseTail() { // exe 0x583730's tail: past story 0xc with flag 0x6ff -> script block 8
    const GameState& g = gameState();
    if (g.stage == kSt29 && !storyFlag(kFlagDoor) && g.story > kTownLateStory && storyFlag(0x6ff))
        scriptStart(kTownLateScriptBlock);
}

} // namespace od::port
