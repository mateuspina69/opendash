// Spawning actors from records: the 0x14-byte spawn records of the exe's data -- a room's spawn list (the room setup
// places the player and the room's actors from it, exe 0x5e02a0) and the room scripts' / the scripts' records (exe
// 0x510e70 and its list forms, which keep the actor in the room script's actor list).
#pragma once

#include <cstdint>

#include "game/loaded_data.hpp"
#include "game/vectors.hpp"

namespace od::port {

// +2 of a spawn record: the pool the actor is allocated in (and how)
enum class SpawnPool : uint8_t {
    Player = 0x00,       // the room's spawn list only: the player's placement
    Animated = 0x20,     // pool 0x20
    Projectile = 0x40,   // pool 0x40 (a room script's records only; stage class)
    StaticStage = 0x60,  // pool 0x60, the stage's class table (a room script's record: by its loop flags)
    StaticGlobal = 0x61, // pool 0x60, the global class table
    EffectStage = 0xa0,  // pool 0xa0, the stage's class table (likewise)
    EffectGlobal = 0xa1, // pool 0xa0, the global class table
    Controller = 0xe0,   // pool 0xe0
};

// A spawn record (0x14 bytes; a list ends with kSpawnListEnd in +0)
struct SpawnRecord : DataRecord {
    static constexpr uint32_t kBytes = 0x14;
    uint8_t flags = 0; // +0x0: the actor's +0 (kActorLive | kActorDrawn ...); kSpawnListEnd ends a list
    uint8_t listSlot = 0; // +0x1: the actor list slot it is kept in (the room script's, a script's)
    uint8_t pool = 0; // +0x2: SpawnPool
    uint8_t loopFlags = 0; // +0x3: ORed into the actor's +3 (kActorStageClass: a room script's static /
    uint32_t typeWord = 0; // +0x4: the actor's type, sub-type, variant and spawn byte
    uint32_t paramsWord = 0; // +0x8: the actor's four parameters
    ShortVec4 point; // +0xc: the integer position (x, y, z) and, in the 4th lane, the yaw
    int16_t yaw = 0; // +0x12
    template <class R> static SpawnRecord load(const R& r) {
        SpawnRecord v;
        v.flags = r.template read<uint8_t>(0x0);
        v.listSlot = r.template read<uint8_t>(0x1);
        v.pool = r.template read<uint8_t>(0x2);
        v.loopFlags = r.template read<uint8_t>(0x3);
        v.typeWord = r.template read<uint32_t>(0x4);
        v.paramsWord = r.template read<uint32_t>(0x8);
        v.point = {r.template read<int16_t>(0xc), r.template read<int16_t>(0xe), r.template read<int16_t>(0x10), r.template read<int16_t>(0x12)};
        v.yaw = r.template read<int16_t>(0x12);
        return v;
    }
};
constexpr uint8_t kSpawnListEnd = 0xff;
constexpr uint32_t kSpawnRecordSize = 0x14;

// The player placement asked of a room's spawn list (the room setup passes the transition request's keepPosition)
constexpr int32_t kSpawnSkipPlayer = 0; // the player's record left alone; < 0: its identity only; > 0: placed there

// exe 0x5e02a0(list, placement): the room's spawn list -- the player's record (by `placement`), each actor allocated
// in its pool and placed; a full pool skips its record
void spawnList(uint32_t list, int32_t placement);
// exe 0x510e70(record): one record of a room script's list (the pools 0x20 / 0x40 / 0x60 / 0xa0 / 0xe0); the actor's
// address, 0 when the record's pool is full or none of these
uint32_t spawnRecord(uint32_t record);
// exe 0x511090(list, n): n records, each actor kept in the room script's actor list at its record's slot, the bit of
// its list index in the actor mask; the count spawned (stops at the first full pool)
uint32_t spawnRecords(uint32_t list, uint32_t n);
// exe 0x5110e0(record): one record into the room script's actor list (the bit of its slot in the actor mask); the
// actor's address, 0 when its pool is full
uint32_t spawnRecordTracked(uint32_t record);

} // namespace od::port
