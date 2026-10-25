// Pokte Village's room-script handlers (see pokte_rooms.hpp), one case per exe routine. The handler's step is the room
// scripts' handler byte (game/room_scripts).
#include "content/manda/pokte_rooms.hpp"
#include "content/manda/pokte_rooms_state.hpp"

#include "engine/data_source.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/game_state.hpp"
#include "game/rewards.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/spawn.hpp"
#include "game/stage_state.hpp"
#include "game/story_flags.hpp"
#include "game/transition_state.hpp"

using namespace od::port::pokte_rooms_state;

namespace od::content {

using namespace od::port;

namespace {

// ---- the houses (exe data) --------------------------------------------------------------------------------------------
// A house's cell and its story flag, as damaged (0x8c9234) and as ruined (0x8c9244): the cell's grid column / row
struct HouseCell : DataRecord {
    uint8_t column = 0;
    uint8_t row = 0;
    uint16_t flag = 0;
    static HouseCell load(const TableReader& r) {
        HouseCell rec;
        rec.column = r.u8(0);
        rec.row = r.u8(1);
        rec.flag = r.u16(2);
        return rec;
    }
};
// A house's repair flags (0x8c9254): damaged / ruined, cleared as the donations rebuild it
struct HouseRepair : DataRecord {
    uint16_t damaged = 0;
    uint16_t ruined = 0;
    static HouseRepair load(const TableReader& r) {
        HouseRepair rec;
        rec.damaged = r.u16(0);
        rec.ruined = r.u16(2);
        return rec;
    }
};
constexpr size_t kHouses = 4;
const DataTable<RecordTable<HouseCell, kHouses, 4>> housesDamaged{TableId::PokteRoomsHousesDamaged};
const DataTable<RecordTable<HouseCell, kHouses, 4>> housesRuined{TableId::PokteRoomsHousesRuined};
const DataTable<RecordTable<HouseRepair, kHouses, 4>> houseRepairs{TableId::PokteRoomsHouseRepairs};
// the reward flags set when the first / the second house is standing (exe data)
const DataTable<ScalarTable<uint16_t, 3>> firstHouseRewards{TableId::PokteRoomsFirstHouseRewards};
const DataTable<ScalarTable<uint16_t, 2>> secondHouseRewards{TableId::PokteRoomsSecondHouseRewards};


constexpr uint32_t kFlagTronAtGate = 0x588;   // script 0x1b ran
constexpr uint32_t kFlagTankBeaten = 0x589;   // the tank's death (content/calbania/tron_boss)
constexpr uint32_t kFlagNoHouseDamaged = 0x100, kFlagVillageRebuilt = 0x101;
constexpr uint32_t kFlagRepairsPaid = 0x164;
constexpr uint32_t kFlagRuinGateTaken = 0x580, kFlagRuinGateCutscene = 0x581;
constexpr uint8_t kScriptTronAtGate = 0x1b, kScriptAfterTank = 0x1c, kScriptRuinGate = 0x14;
constexpr uint16_t kRepairPrice = 1000;
constexpr uint32_t kRuinedRepairPrice = 2000;
constexpr int kRebuiltBalanceBonus = 5000;
constexpr uint8_t kStoryAfterTron = 4, kStoryLateVillagers = 0xc;

// the variant of a house cell's segment (exe 0x58d5c0; -1 for no breakable segment)
int32_t houseVariant(const HouseCell& h) { return gridCellVariant(h.column, h.row); }
void setFlags(const DataTable<ScalarTable<uint16_t, 3>>& flags) {
    for (size_t i = 0; i < flags.size(); ++i) setStoryFlag(flags[i]);
}
void setFlags(const DataTable<ScalarTable<uint16_t, 2>>& flags) {
    for (size_t i = 0; i < flags.size(); ++i) setStoryFlag(flags[i]);
}

// exe 0x581bb0 (ST25 story 3 sub 6, the fight with Tron's tank): the houses' damage noted; step 0 the tank (record
// 0x8c9220); step 1 the tank's death -> script 0x1c, the donations cleared, the rewards of the houses left standing
bool tankFight(bool run) {
    if (!run) return true;
    RoomScriptState& room = roomScriptState();
    for (size_t i = 0; i < kHouses; ++i) {
        const HouseCell d = housesDamaged[i];
        const HouseCell r = housesRuined[i];
        const uint32_t fa = d.flag, fb = r.flag;
        if (!storyFlag(fa) && houseVariant(d) > 0) setStoryFlag(fa);
        if (storyFlag(fa) && !storyFlag(fb) && houseVariant(r) > 1) setStoryFlag(fb);
    }
    if (room.handlerStep == 0) {
        spawnRecordTracked(stateAddress(TableId::PokteRoomsTankRecord)); // exe 0x5110e0
    } else {
        if (room.handlerStep != 1 || !storyFlag(kFlagTankBeaten)) return true;
        scriptRun(kScriptAfterTank);
        gameState().scriptCounters[0] = 0;
        if (!storyFlag(houseRepairs[1].damaged)) setFlags(secondHouseRewards);
        if (!storyFlag(houseRepairs[0].damaged)) setFlags(firstHouseRewards);
        size_t n = 0;
        while (n < kHouses && !storyFlag(housesDamaged[n].flag)) ++n;
        if (n == kHouses) setStoryFlag(kFlagNoHouseDamaged);
    }
    ++room.handlerStep;
    return true;
}
// exe 0x581dc0 (ST25 story 4.. sub 0, the village after Tron): step 0 -- unless no house was damaged or the village is
// rebuilt, the donations rebuild the houses 1000 at a time (a ruined house back to damaged, a damaged one standing, its
// rewards); all standing: rebuilt, the balance raised; then the cost of what is left, the houses' cells by their state,
// the villagers; step 1 -- the donations reach the cost: the repairs paid
bool villageAfterTron(bool run) {
    if (!run) return true;
    RoomScriptState& room = roomScriptState();
    PokteVillageState& village = stageBlock().part<PokteVillageState>();
    uint16_t& donations = gameState().scriptCounters[0];
    if (room.handlerStep == 1) {
        if (int32_t(donations) >= village.repairCost) {
            setStoryFlag(kFlagRepairsPaid);
            ++room.handlerStep;
        }
        return true;
    }
    if (room.handlerStep != 0) return true;
    village.repairCost = 0;
    if (!storyFlag(kFlagNoHouseDamaged) && !storyFlag(kFlagVillageRebuilt)) {
        while (donations >= kRepairPrice) {
            donations -= kRepairPrice;
            for (size_t i = 0; i < kHouses; ++i) {
                const HouseRepair h = houseRepairs[i];
                if (!storyFlag(h.damaged)) continue;
                if (storyFlag(h.ruined)) {
                    clearStoryFlag(h.ruined);
                } else {
                    clearStoryFlag(h.damaged);
                    setStoryFlag(h.ruined);
                    if (i == 0) setFlags(firstHouseRewards);
                    else if (i == 1) setFlags(secondHouseRewards);
                }
                break;
            }
            size_t n = 0;
            while (n < kHouses && !storyFlag(houseRepairs[n].damaged)) ++n;
            if (n == kHouses) {
                setStoryFlag(kFlagVillageRebuilt);
                balanceAdd(kRebuiltBalanceBonus); // exe 0x5e51d0
                break;
            }
        }
        if (!storyFlag(kFlagVillageRebuilt))
            for (size_t i = 0; i < kHouses; ++i)
                if (storyFlag(houseRepairs[i].damaged))
                    village.repairCost += storyFlag(houseRepairs[i].ruined) ? kRuinedRepairPrice : uint32_t(kRepairPrice);
    }
    for (size_t i = 0; i < kHouses; ++i) {
        const HouseCell d = housesDamaged[i];
        const HouseCell r = housesRuined[i];
        if (!storyFlag(d.flag)) switchGridCellVariant(d.column, d.row, 0);
        else if (!storyFlag(r.flag)) switchGridCellVariant(d.column, d.row, 1);
        else switchGridCellVariant(r.column, r.row, 2);
    }
    spawnRecords(stateAddress(TableId::PokteRoomsVillageAfterTronSpawns), 1);
    if (gameState().story != kStoryAfterTron || storyFlag(kFlagRuinGateCutscene)) {
        spawnRecords(stateAddress(TableId::PokteRoomsVillageAfterTronHouseSpawns), 4);
        if (storyFlag(houseRepairs[1].ruined) && storyFlag(houseRepairs[1].damaged)) spawnRecords(stateAddress(TableId::PokteRoomsVillageAfterTronSecondHouseRepairSpawns), 1);
        if (storyFlag(houseRepairs[0].ruined) && storyFlag(houseRepairs[0].damaged)) spawnRecords(stateAddress(TableId::PokteRoomsVillageAfterTronFirstHouseRepairSpawns), 1);
    }
    room.handlerStep = (!storyFlag(kFlagNoHouseDamaged) && !storyFlag(kFlagVillageRebuilt)) ? 1 : 2;
    return true;
}
// exe 0x581950 (ST24 sub 0, story 4..; subs 3 / 6 at story 3): the villagers -- two, one more before story 0xc, and
// 1..6 by the balance (none while it is negative), once per visit
bool villagers(bool run) {
    RoomScriptState& room = roomScriptState();
    if (!run || room.handlerStep != 0) return true;
    const int16_t balance = gameState().balance;
    spawnRecords(stateAddress(TableId::PokteRoomsVillagersSpawns), 2);
    if (gameState().story < kStoryLateVillagers) spawnRecords(stateAddress(TableId::PokteRoomsVillagersEarlySpawns), 1);
    if (balance >= 0) {
        const uint32_t n = balance >= 0x4000   ? 6
                           : balance >= 0x3000 ? 4
                           : balance >= 0x2000 ? 3
                           : balance >= 0x1000 ? 2
                                               : 1;
        spawnRecords(stateAddress(TableId::PokteRoomsVillagersByBalanceSpawns), n);
    }
    ++room.handlerStep;
    return true;
}

} // namespace

bool pokteRoomHandler(RoutineId fn, bool run) {
    RoomScriptState& room = roomScriptState();
    switch (fn) {
    case RoutineId::St25Story3Sub5RoomHandler: // ST25 story 3 sub 5: once (its flag) script 0x1b, Tron at the gate
        if (!run || room.handlerStep != 0) return true;
        if (!storyFlag(kFlagTronAtGate)) {
            scriptRun(kScriptTronAtGate);
            setStoryFlag(kFlagTronAtGate);
        }
        ++room.handlerStep;
        return true;
    case RoutineId::PokteTankFightRoomHandler: return tankFight(run);
    case RoutineId::PokteAfterTronRoomHandler: return villageAfterTron(run);
    case RoutineId::PokteRecordRoomHandlerA:
    case RoutineId::PokteRecordRoomHandlerB:
    case RoutineId::PokteRecordRoomHandlerC:
    case RoutineId::St25Story4OnSub1To4RoomHandler: {
        // ST25 story 4.. subs 1..4 (the houses' insides): from story 5 or after the ruin gate one villager
        if (!run) return true;
        if (room.handlerStep != 0) return true;
        if (gameState().story != kStoryAfterTron || storyFlag(kFlagRuinGateCutscene)) {
            const uint32_t record = fn == RoutineId::PokteRecordRoomHandlerA   ? stateAddress(TableId::PokteRoomsHouseVillagerRecordA)
                                    : fn == RoutineId::PokteRecordRoomHandlerB ? stateAddress(TableId::PokteRoomsHouseVillagerRecordB)
                                    : fn == RoutineId::PokteRecordRoomHandlerC ? stateAddress(TableId::PokteRoomsHouseVillagerRecordC)
                                                     : stateAddress(TableId::PokteRoomsHouseVillagerRecordD);
            spawnRecordTracked(record); // exe 0x5110e0
        }
        ++room.handlerStep;
        return true;
    }
    case RoutineId::PokteVillagersRoomHandler: return villagers(run);
    case RoutineId::St24Story4Sub1RoomHandler: // ST24 story 4 sub 1: back from the ruin gate -> script 0x14 once
        if (!run || room.handlerStep != 0) return true;
        if (storyFlag(kFlagRuinGateTaken) && !storyFlag(kFlagRuinGateCutscene)) {
            setStoryFlag(kFlagRuinGateCutscene);
            scriptRun(kScriptRuinGate);
        }
        ++room.handlerStep;
        return true;
    case RoutineId::St24Story4Sub2RoomHandler:
        // ST24 story 4 sub 2 (the ruin gate): its actor; until the cutscene the gate's exit (its id 1)
        // marks the gate taken and fades the music
        if (!run) return true;
        if (room.handlerStep == 0) {
            spawnRecords(stateAddress(TableId::PokteRoomsRuinGateSpawns), 1);
            room.handlerStep = storyFlag(kFlagRuinGateCutscene) ? 2u : 1u;
        } else if (room.handlerStep == 1 && storyFlag(kFlagTransitionArmed)) {
            if (transitionRequest().exitId == 1) {
                setStoryFlag(kFlagRuinGateTaken);
                soundFadeOut(kMusicFades, 0x111, 0);
            }
            ++room.handlerStep;
        }
        return true;
    case RoutineId::St24Story4OnSub5RoomHandler: // ST24 sub 5 (story 4..): its actor, once per visit
        if (!run || room.handlerStep != 0) return true;
        spawnRecords(stateAddress(TableId::PokteRoomsRuinGateSpawns), 1);
        ++room.handlerStep;
        return true;
    default: return false;
    }
}

} // namespace od::content
