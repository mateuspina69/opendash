// The actors' stat tables of the chosen difficulty (exe 0x50f260 points them at the difficulty's tables:
// selectDifficultyStats): per type, the animated actors' attack / hit-entry / balance values and the statics' hit
// points, damages and push values (by variant). The classes read their row; the tables are exe data.
#pragma once

#include <cstdint>

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"

namespace od::port {

// a row of the animated actors' table (by type)
struct AnimatedActorStats : DataRecord {
    static constexpr uint32_t kBytes = 0xd;
    uint8_t hitPoints = 0;
    uint8_t attack = 0;            // the damage of the actor's attack entry
    uint8_t strongAttack = 0;      // the damage of its strong attack (a body jumping at the player)
    uint8_t shotAttack = 0;        // the damage of its shots
    uint8_t hitHeight = 0;         // the hit entry's height / shape bits (<< 9 into its shape)
    uint8_t dropTable = 0;         // the drops it leaves (game/pickups spawnDrops)
    uint8_t bodyAttack = 0;        // +8: the damage of the body as a thrown / attacking entry
    uint8_t hitSize = 0;           // the hit entry's size (>> 2: its radius bits)
    uint8_t balanceLossThrown = 0; // the balance lost (x 4) when the player throws it at something
    uint8_t balanceLossStrong = 0; // ... when a strong hit (0x480000) lands on it
    uint8_t balanceLossHit = 0;    // ... when a hit (0x40000) lands on it
    template <class R> static AnimatedActorStats load(const R& r) {
        return {{}, r.u8(0), r.u8(1), r.u8(2), r.u8(3), r.u8(4), r.u8(5), r.u8(8), r.u8(9), r.u8(0xa), r.u8(0xb), r.u8(0xc)};
    }
};

// a row of the statics' table (by type, then 0x10 bytes per variant)
struct StaticActorStats : DataRecord {
    static constexpr uint32_t kBytes = 0xa;
    uint8_t hitPoints = 0;
    uint8_t damage = 0;        // the damage of its hit entry
    uint8_t power = 0;         // the second strength: the box's other hit kind, the bomb's blast size
    uint8_t deathAttack = 0;   // the attack of its death's blasts (the ST55 floating mine)
    uint8_t hitHeight = 0;     // the hit entry's height / shape bits (<< 9 into its shape)
    uint8_t dropTable = 0;     // the drops it leaves (game/pickups spawnDrops)
    uint8_t pushThreshold = 0; // +8: the damage that pushes it
    uint8_t pushDistance = 0;  // how far a push throws it (x 0x200)
    template <class R> static StaticActorStats load(const R& r) {
        return {{}, r.u8(0), r.u8(1), r.u8(2), r.u8(3), r.u8(4), r.u8(5), r.u8(8), r.u8(9)};
    }
};

// the stat tables: u32 per type, the pointer to the type's row(s) (0 = none)
struct ActorStatTables {
    DataPtr<> animated; // 0xa714c0: the animated actors' table (AnimatedActorStats by type)
    DataPtr<> statics; // 0xa6bb84: the statics' table (StaticActorStats rows by type, 0x10 per variant)
};
inline ActorStatTables& actorStatTables() {
    static ActorStatTables s;
    return s;
}
// the difficulties' stat tables (exe data 0x8993a4 / 0x8993b8, by difficulty): the tables' addresses, as the stage
// globals store them (the address is the record's identity). Five difficulties; a difficulty past them reads the words that follow, as
// the exe does (difficulty 5: the statics' table's first word)
constexpr size_t kDifficultyWords = 8;
inline const DataTable<ScalarTable<uint32_t, kDifficultyWords>> difficultyAnimatedStats{TableId::DifficultyAnimatedStats};
inline const DataTable<ScalarTable<uint32_t, kDifficultyWords>> difficultyStaticStats{TableId::DifficultyStaticStats};
// exe 0x50f260(difficulty): the stat tables of the difficulty
inline void selectDifficultyStats(uint8_t difficulty) {
    ActorStatTables& t = actorStatTables();
    t.animated.pointAt(difficultyAnimatedStats[difficulty]);
    t.statics.pointAt(difficultyStaticStats[difficulty]);
}

// the pointer to a type's row(s) in a stat table (0: none)
inline uint32_t statTableRow(const DataPtr<>& table, uint8_t type) { return dataScalar<uint32_t>(table.value() + uint32_t(type) * 4); }
// the pointer to the row of an animated actor's type (and spawn byte: +7, the types whose table has a 0x10-byte row
// per spawn byte) / of a static actor's type and variant (a class with a row of its own reads it from there)
inline uint32_t animatedStatRow(uint8_t type, uint8_t spawnByte = 0) {
    return statTableRow(actorStatTables().animated, type) + uint32_t(spawnByte) * 0x10;
}
inline uint32_t staticStatRow(uint8_t type, uint8_t variant) {
    return statTableRow(actorStatTables().statics, type) + uint32_t(variant) * 0x10;
}
// the row at a row pointer (a class that reads a row of its own: a column of its spawn byte, a variant)
inline AnimatedActorStats animatedStatsAt(uint32_t row) { return dataRecord<AnimatedActorStats>(row, AnimatedActorStats::kBytes); }
inline StaticActorStats staticStatsAt(uint32_t row) { return dataRecord<StaticActorStats>(row, StaticActorStats::kBytes); }
// the row of an animated actor's type
inline AnimatedActorStats animatedStats(uint8_t type) {
    return dataRecord<AnimatedActorStats>(animatedStatRow(type), AnimatedActorStats::kBytes);
}
// whether the animated actors' table has a row for the type (the classes read 0 hit points otherwise)
inline bool hasAnimatedStats(uint8_t type) { return statTableRow(actorStatTables().animated, type) != 0u; }
// the row of an animated actor's type and spawn byte
inline AnimatedActorStats animatedStats(uint8_t type, uint8_t spawnByte) {
    return dataRecord<AnimatedActorStats>(animatedStatRow(type, spawnByte), AnimatedActorStats::kBytes);
}
// whether the statics' table has rows for the type (the exe reads 0 for the hit points otherwise)
inline bool hasStaticStats(uint8_t type) { return statTableRow(actorStatTables().statics, type) != 0u; }
// the row of a static actor's type and variant
inline StaticActorStats staticStats(uint8_t type, uint8_t variant) {
    return dataRecord<StaticActorStats>(staticStatRow(type, variant), StaticActorStats::kBytes);
}

} // namespace od::port
