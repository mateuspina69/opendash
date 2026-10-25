// The spawners that place Roll by the room's entrance (see roll_spawners.hpp). Bit-exact against
// tests/data/story_*.bin (test_story).
#include "content/yosyonke/roll_spawners.hpp"
#include "content/yosyonke/roll_spawners_state.hpp"

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/breakable_walls.hpp"
#include "game/encounter.hpp"
#include "game/event.hpp"
#include "game/game_state.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace roll_spawners_state;

namespace {

constexpr const char* kSpawnerPort = "town story port"; // the area of its port-gap errors

enum class SpawnerState : uint8_t { Place = 0, Watch = 1 };

// The class record a spawner allocates Roll from (exe data): the actor's +0, its run / loop flags ORed in, its type
// word and its parameters
struct RollRecord : DataRecord {
    static constexpr uint32_t kBytes = 0xc;
    uint8_t flags = 0, runFlags = 0, loopFlags = 0;
    uint32_t typeWord = 0, paramsWord = 0;
    static RollRecord load(const TableReader& r) { return {{}, r.u8(0), r.u8(1), r.u8(3), r.u32(4), r.u32(8)}; }
};
// the point she is allocated at (exe data: s16 x / y / z, yaw)
struct RollPoint : DataRecord {
    static constexpr uint32_t kBytes = 8;
    std::array<int16_t, 3> point{};
    int16_t yaw = 0;
    static RollPoint load(const TableReader& r) { return {{}, {r.s16(0), r.s16(2), r.s16(4)}, r.s16(6)}; }
};
// the records: the follower, Roll at Joe's lab, ST09's follower, the field's Roll (exe data 0x88a4b0 / 0x88a4c4 /
// 0x88a4e0 / 0x88a500, each record followed by its point)
const DataTable<RollRecord> followerRecord{TableId::RollFollowerRecord}, shedRecord{TableId::RollShedRecord},
    st09FollowerRecord{TableId::RollSt09FollowerRecord}, fieldRecord{TableId::RollFieldRecord};
const DataTable<RollPoint> followerPoint{TableId::RollFollowerPoint}, shedPoint{TableId::RollShedPoint},
    st09FollowerPoint{TableId::RollSt09FollowerPoint}, fieldPoint{TableId::RollFieldPoint};

constexpr uint32_t kFlagAfterJunkShop = 0x5c1, kFlagAfterJoesLab = 0x5e1, kFlagTracksOpen = 0x5e2;
constexpr uint32_t kFlagRollFainted = 0x5d0;

// exe 0x41b190(record, position, out): an animated actor (exe 0x5e1160) with the record's flags / type / parameters
// (exe 0x50fd20) at the point (exe 0x50fd60); nullptr when the pool is full
AnimatedActor* spawnRoll(const DataTable<RollRecord>& record, const DataTable<RollPoint>& point) {
    const RollRecord& rec = *record;
    const RollPoint& at = *point;
    const uint32_t address = allocActor20();
    if (!address) return nullptr;
    AnimatedActor& a = animatedActorAt(address);
    a.flags = rec.flags;
    a.runFlags |= rec.runFlags;
    a.loopFlags |= rec.loopFlags;
    a.setTypeWord(rec.typeWord);
    a.setParamsWord(rec.paramsWord);
    Fixed16* const prev[3] = {&a.prevPos.x, &a.prevPos.y, &a.prevPos.z};
    Fixed16* const pos[3] = {&a.pos.x, &a.pos.y, &a.pos.z};
    for (size_t k = 0; k < 3; ++k) {
        const int32_t v = int32_t(uint32_t(int32_t(at.point[k])) << 16);
        *prev[k] = v;
        *pos[k] = v;
    }
    a.prevRotation.x = 0;
    a.rotation.x = 0;
    a.prevRotation.z = 0;
    a.rotation.z = 0;
    a.prevRotation.y = at.yaw;
    a.rotation.y = at.yaw;
    return &a;
}
// the actor the exe goes on writing through when the pool was full (a port gap here)
AnimatedActor& spawned(AnimatedActor* a, const char* who) {
    if (!a) notPorted(kSpawnerPort, who);
    return *a;
}
void place(AnimatedActor& a, int16_t x, int16_t z, int16_t yaw) {
    a.pos.x.whole = x;
    a.pos.z.whole = z;
    a.rotation.y = yaw;
}
// her remark (the talk event at her: exe 0x543af0(a, 0, 0, message))
void remark(AnimatedActor& a, uint16_t message) { eventStartAtActor(a, 0, 0, message); }

// exe 0x41b0b0 (ST08): Roll by the player's arrival point; 1..4 = the known entrances, 5 = elsewhere
int placeST08(AnimatedActor& a) {
    const int16_t x = player().pos.x.whole, z = player().pos.z.whole;
    if (x == 0) {
        if (z == 0xa00) {
            place(a, 0, 0x980, 0x800);
            return 1;
        }
    } else if (x == 0x820) {
        if (z == 0x600) {
            place(a, 0x8b0, 0x600, 0x400);
            return 2;
        }
    } else if (x == -0x580) {
        if (z == 0x10) {
            place(a, -0x500, 0x10, 0x400);
            return 3;
        }
    } else if (x == 0x600 && z == -0xab0) {
        place(a, 0x600, -0xa00, 0);
        return 4;
    }
    place(a, 0x90, -0x70, 0x6d0);
    return 5;
}
// exe 0x41b270 (ST09): Roll by the player's arrival point and her remark (messages 0x47..0x4c) when he comes out of a
// house; the entrance (0 = none of them)
int placeST09(AnimatedActor& a) {
    const int16_t x = player().pos.x.whole, z = player().pos.z.whole;
    int k = 0;
    int16_t message = -1;
    if (x == 0) {
        if (z == -0x1e00) place(a, 0, -0x1e80, 0x800);
        // then the ST08 gate test below (0x41b3f5 / 0x41b3ff)
    } else if (x == -0x3c0) {
        if (z == -0x1900) {
            place(a, -0x350, z, 0x400);
            k = 1;
        } else if (z == -0x1300) {
            place(a, -0x350, z, 0x400);
            k = 2;
            message = 0x47;
        } else {
            return 0;
        }
    } else if (x == -0xf00) {
        if (z != -0x15c0) return 0;
        place(a, x, -0x1550, 0);
        k = 3;
        message = 0x48;
    } else if (x == 0x350) {
        if (z != -0x1700) return 0;
        place(a, 0x2e0, z, 0xc00);
        k = 4;
        message = 0x49;
    } else if (x == -0xb50) {
        if (z != -0x300) return 0;
        place(a, x, -0x370, 0x800);
        k = 5;
        message = 0x4a;
    } else if (x == 0x980) {
        if (z != 0x100) return 0;
        place(a, 0x930, z, 0xc00);
        k = 6;
        message = 0x4a;
    } else if (x == 0xa00) {
        if (z != -0xd00) return 0;
        place(a, 0x9a0, z, 0xc00);
        k = 7;
        message = 0x4c;
    }
    if (message >= 0) remark(a, uint16_t(message));
    if (player().pos.x.whole == 0 && player().pos.z.whole == 0x700) {
        place(a, 0, 0x6a0, 0x800);
        k = 8;
        remark(a, 0x4b);
    }
    return k;
}

// exe 0x41af40 (ST08; its states 0x88a4d8: 0x41af60, ret)
void rollSpawnerST08(ControllerActor& e) {
    if (SpawnerState(e.state) != SpawnerState::Place) return;
    classState<RollSpawnerFields>(e).scratch = {};
    if (gameState().room == 0 && gameState().story == 0) {
        if (storyFlag(kFlagAfterJoesLab)) { // after Joe's lab: Roll follows; her remarks at the entrances 1 / 4
            AnimatedActor& a = spawned(spawnRoll(followerRecord, followerPoint), "0x41af40: the follower not spawned");
            const int k = placeST08(a);
            if (k == 1) {
                remark(a, 0x11);
            } else if (k == 4 && storyFlag(kFlagRollFainted)) {
                remark(a, 0x10);
                clearStoryFlag(kFlagRollFainted);
            }
        } else if (storyFlag(kFlagAfterJunkShop)) { // after the junk shop: Roll waits at Joe's lab (exe 0x443330)
            spawnRoll(shedRecord, shedPoint);
        } else {
            AnimatedActor& a = spawned(spawnRoll(followerRecord, followerPoint), "0x41af40: the follower not spawned");
            if (placeST08(a) == 4) remark(a, 0xf);
        }
    }
    e.enterState(uint8_t(SpawnerState::Watch));
}
// exe 0x41b1d0 (ST09; its states 0x88a4f4: 0x41b1f0, ret)
void rollSpawnerST09(ControllerActor& e) {
    if (SpawnerState(e.state) != SpawnerState::Place) return;
    classState<RollSpawnerFields>(e).scratch = {};
    if (gameState().room == 0 && gameState().story == 0 && !storyFlag(kFlagAfterJunkShop)) {
        if (AnimatedActor* a = spawnRoll(st09FollowerRecord, st09FollowerPoint)) placeST09(*a);
    }
    e.enterState(uint8_t(SpawnerState::Watch));
}

// ---- ST0D (the field to the ruins): exe 0x41b440 (its states 0x88a554: 0x41b460, 0x41b700) --------------------------
// Place: the tracks' cell (0x59, 0x3b) switched to variant 1 once the story is past 0 or flag 0x5e2 / 0x5e1 is set; with
// 0x5e1 (after Joe's lab) Roll (record 0x88a500 = exe 0x4e4c20) by the entrance: from ST09 (0x3600, 0xed00) next to
// the player and her "Let's hurry" (message 0x16) -- or, fainted before (flag 0x5d0), at one of four random spots;
// from ST0E (0x3000, -0x900) beside him. Then the field encounters suspended and Watch at once. Watch: the flags of the
// region the player is in (0x680 x > -0xe00, 0x683 between -0x1200 and -0x800 = on the tracks), the field encounters'
// formation picks 0x88a514; flag 0x681 = an actor of the pool other than Roll (0x60) / the critter (0x7e) is live
// (0x682 with 0x680).
enum class TracksStep : uint8_t { Off = 0, On = 1 };
constexpr uint32_t kFieldKind = 1u << uint32_t(EncounterKind::Field);
constexpr uint32_t kFlagEastOfTracks = 0x680, kFlagOthersLive = 0x681, kFlagOthersLiveEast = 0x682,
                   kFlagOnTracks = 0x683;
constexpr int16_t kRegionEast = -0xe00, kTracksEast = -0x800, kTracksWest = -0x1200;
constexpr uint8_t kRollType = 0x60, kCritterType = 0x7e;

// exe 0x41b700
void rollSpawnerST0DWatch(ControllerActor& e) {
    const int16_t px = player().pos.x.whole;
    if (TracksStep(e.step) == TracksStep::Off) {
        patchedRecordFor<FieldEncounterTable>(encounterState().fieldTable).formationPick = stateAddress(TableId::RollSpawnersFormationPicks);
        if (px > kRegionEast) setStoryFlag(kFlagEastOfTracks);
        else clearStoryFlag(kFlagEastOfTracks);
        if (px < kTracksWest) {
            clearStoryFlag(kFlagOnTracks);
            e.step = uint8_t(TracksStep::On);
        } else if (px > kTracksEast) {
            encounterSuspend(kFieldKind, 1);
            clearStoryFlag(kFlagOnTracks);
            e.step = uint8_t(TracksStep::On);
        }
    } else if (TracksStep(e.step) == TracksStep::On) {
        if (px < kTracksEast && px > kTracksWest) {
            encounterSuspend(kFieldKind, 0);
            setStoryFlag(kFlagOnTracks);
            e.step = uint8_t(TracksStep::Off);
        }
    }
    clearStoryFlag(kFlagOthersLive);
    // the exe walks 32 "slots" from slot 1 (byte 0 of slot k with the type of slot k), past the end of the pool: the
    // headers at the pool's stride, the last one past its slots -- "slot 32" is the effects' live masks (its byte 0
    // the first mask word's low byte, its "type" the second word's)
    for (uint32_t k = 1; k <= 32; ++k) {
        uint8_t f = 0, t = 0;
        if (k < AnimatedPool::size()) {
            const ActorHeader& slot = animatedPool()[k];
            f = slot.flags;
            t = slot.type;
        } else {
            const ActorSystemState& sys = actorSystem();
            f = uint8_t(sys.effectsLive[0]);
            t = uint8_t(sys.effectsLive[1]);
        }
        if ((f & kActorQueued) && t != kRollType && t != kCritterType && t != 0) {
            setStoryFlag(kFlagOthersLive);
            if (storyFlag(kFlagEastOfTracks)) setStoryFlag(kFlagOthersLiveEast);
            return;
        }
    }
}
// the tracks' cell switched to its variant 1 (exe 0x58d5f0 on the cell (0x59, 0x3b))
void openTracks() { switchGridCellVariant(0x59, 0x3b, 1); }
// exe 0x41b440
void rollSpawnerST0D(ControllerActor& e) {
    if (SpawnerState(e.state) == SpawnerState::Watch) {
        rollSpawnerST0DWatch(e);
        return;
    }
    if (SpawnerState(e.state) != SpawnerState::Place) notPortedAt(kSpawnerPort, "0x41b440 state", e.state);
    classState<RollSpawnerFields>(e).scratch = {};
    if (gameState().story != 0 || storyFlag(kFlagTracksOpen)) {
        openTracks();
    } else if (storyFlag(kFlagAfterJoesLab)) {
        openTracks();
        AnimatedActor& a = spawned(spawnRoll(fieldRecord, fieldPoint), "0x41b440: Roll not spawned");
        const int16_t px = player().pos.x.whole, pz = player().pos.z.whole;
        if (px == 0x3600) {
            if (pz == -0x1300) {
                if (storyFlag(kFlagRollFainted)) {
                    switch (rngStep() & 3) {
                    case 0: place(a, -0x200, -0xd00, 0xd00); break;
                    case 1: place(a, 0x1667, -0x1303, 0x530); break;
                    case 2: place(a, 0x1b00, -0x700, 0); break;
                    default: place(a, -0x1e00, -0x1b00, 0xa00); break;
                    }
                } else {
                    place(a, 0x3570, -0x1300, 0xc00);
                    remark(a, 0x16);
                }
            }
        } else if (px == 0x3000 && pz == -0x900) {
            place(a, 0x2f80, -0x900, 0xc00);
        }
    }
    e.enterState(uint8_t(SpawnerState::Watch));
    encounterSuspend(kFieldKind, 0);
    rollSpawnerST0DWatch(e);
}

} // namespace

void rollSpawnerST08Update(const PlayerEnv&, uint32_t actor) { rollSpawnerST08(controllerActorAt(actor)); }
void rollSpawnerST09Update(const PlayerEnv&, uint32_t actor) { rollSpawnerST09(controllerActorAt(actor)); }
void rollSpawnerST0DUpdate(const PlayerEnv&, uint32_t actor) { rollSpawnerST0D(controllerActorAt(actor)); }

} // namespace od::port
