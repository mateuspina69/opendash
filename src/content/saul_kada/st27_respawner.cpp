// The ST27 enemy respawner (static type 0x27, story 13, exe 0x5a9d80; hypothesis names).
//
// Its records: the variant's list [0x8cf274 + variant * 4] (spawn records, 0x14 bytes); the enemy ids first + i + 1
// (alive / defeated) and first + i + 0x14 ("away"). Start: every "away" id set. Each tick, per record: an "away"
// enemy within 0xc1c of the player (x / z) is spawned (pool 0x20, tracked in the script slot under its id, its defeated
// bit cleared) while fewer than 8 of the animated pool's slots 0..13 live; a live one farther than 0xc1c is marked
// away again (its actor despawns). The exe's countdowns (0x96 at the spawn) sit behind a test that is always false
// (sete al; test al, 0x80) and never run.
#include "content/saul_kada/st27_respawner.hpp"
#include "content/saul_kada/st27_respawner_state.hpp"

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/field_alias.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/spawn.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st27_respawner_state;

namespace {

enum class RespawnerState : uint8_t { Start = 0, Watch = 1 };
// The respawner's parameters: its records' count and the first of their enemy ids
struct RespawnerParams {
    explicit RespawnerParams(const StaticActor& actor) : RespawnerParams(const_cast<StaticActor&>(actor)) {}
    explicit RespawnerParams(StaticActor& actor) : count(actor.params[0]), firstId(actor.params[2]) {}
    FieldAlias<int8_t, uint8_t> count;
    FieldAlias<int8_t, uint8_t> firstId;
};
// by variant (exe data 0x8cf274: the spawn record lists' addresses; the records read as game/spawn's, through the game
// memory until the native state)
const DataTable<ScalarTable<uint32_t, 8>> recordLists{TableId::St27RespawnerRecordLists};
constexpr uint32_t kAwayIds = 0x14;
constexpr int kSpawnDistance = 0xc1c;
constexpr int8_t kMaxLive = 8;
constexpr uint8_t kSpawnCountdown = 0x96;
constexpr uint8_t kRunDespawn = 0x20; // the actor's run flag that despawns it

// exe 0x5e3b40: the planar distance of two s16 points
int distanceXZ(int x0, int z0, int x1, int z1) {
    const int16_t from[3] = {int16_t(x0), 0, int16_t(z0)}, to[3] = {int16_t(x1), 0, int16_t(z1)};
    return planarDistance16(from, to);
}

void respawner(StaticActor& actor) {
    const RespawnerParams params(actor);
    RespawnerFields& fields = classState<RespawnerFields>(actor);
    const int n = params.count;
    if (RespawnerState(actor.state) == RespawnerState::Start) { // 0x5aa01c
        actor.state = uint8_t(RespawnerState::Watch);
        for (int8_t i = params.firstId; i < params.firstId + n; ++i) enemyMarkDefeated(uint32_t(int32_t(i) + kAwayIds));
        return;
    }
    if (RespawnerState(actor.state) != RespawnerState::Watch) {
        notPortedAt(kStoryPort, "0x5a9d80 state", actor.state);
        return;
    }
    const int px = player().pos.x.whole, pz = player().pos.z.whole;
    uint32_t record = recordLists[actor.variant];
    uint8_t live = 0;
    for (uint32_t bit = 31; bit >= 18; --bit)
        if (actorSystem().animatedLive & (1u << bit)) ++live;
    for (int8_t i = 0; i < n; ++i, record += kSpawnRecordSize) {
        const SpawnRecord rec = dataRecord<SpawnRecord>(record, SpawnRecord::kBytes);
        const uint32_t aliveId = uint32_t(int32_t(params.firstId) + i + 1);
        const uint32_t awayId = uint32_t(int32_t(params.firstId) + i + int32_t(kAwayIds));
        if (enemyDefeated(aliveId) && !enemyDefeated(awayId)) continue; // the dead countdown branch
        if (enemyDefeated(awayId)) {
            if (enemyDefeated(aliveId)) enemyClearDefeated(aliveId);
            if (int8_t(live) >= kMaxLive || distanceXZ(px, pz, rec.point.x, rec.point.z) >= kSpawnDistance) continue;
            const uint32_t slot = allocActor20(); // exe 0x5e1160
            if (!slot) continue;
            AnimatedActor& enemy = animatedActorAt(slot);
            enemy.flags = rec.flags;
            enemy.loopFlags |= rec.loopFlags;
            fields.slots[size_t(i)] = enemy.slot;
            fields.countdowns[size_t(i)] = kSpawnCountdown;
            enemy.setTypeWord(rec.typeWord);
            enemy.setParamsWord(rec.paramsWord);
            const int32_t x = int32_t(uint32_t(int32_t(rec.point.x)) << 16);
            const int32_t y = int32_t(uint32_t(int32_t(rec.point.y)) << 16);
            const int32_t z = int32_t(uint32_t(int32_t(rec.point.z)) << 16);
            enemy.prevPos.x = x;
            enemy.pos.x = x;
            enemy.prevPos.y = y;
            enemy.pos.y = y;
            enemy.prevPos.z = z;
            enemy.pos.z = z;
            enemy.prevRotation.x = 0;
            enemy.rotation.x = 0;
            enemy.prevRotation.z = 0;
            enemy.prevRotation.y = rec.yaw;
            enemy.rotation.y = rec.yaw;
            enemy.rotation.z = 0;
            encounterTrackInScriptSlot(enemy, uint16_t(aliveId)); // exe 0x5e6500
            enemyClearDefeated(awayId);
            ++live;
        } else if (distanceXZ(px, pz, rec.point.x, rec.point.z) >= kSpawnDistance) {
            actorHeaderAt(animatedPoolSlot(fields.slots[size_t(i)])).runFlags |= kRunDespawn;
            enemyMarkDefeated(awayId);
        }
    }
}

} // namespace

void respawnerUpdate(const PlayerEnv&, uint32_t actor) { respawner(staticActorAt(actor)); }

} // namespace od::port
