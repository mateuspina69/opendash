// Spawning actors from records (see spawn.hpp).
#include "game/loaded_data.hpp"
#include "game/actor_state.hpp"
#include "game/spawn.hpp"

#include "game/actor_pools.hpp"
#include "game/game_state.hpp"
#include "game/player/player_model.hpp"
#include "game/player/player_state.hpp"
#include "game/room_scripts.hpp"

namespace od::port {

namespace {

// a 16.16 coordinate from a record's integer one
int32_t wholeToFixed(int16_t v) { return int32_t(uint32_t(int32_t(v)) << 16); }

// exe 0x5e02a0 / 0x510e70's copy of a record into a model-bearing actor: the flags (ORed into the actor's by a room
// script's record), the loop flags ORed, the type and parameter words, the position and the previous position, the
// rotation cleared but for the yaw (the exe's byte stores) and the yaw
void placeFromRecord(Actor& a, const SpawnRecord& r, bool mergeFlags) {
    a.flags = mergeFlags ? uint8_t(a.flags | r.flags) : uint8_t(r.flags);
    a.loopFlags |= r.loopFlags;
    a.setTypeWord(r.typeWord);
    a.setParamsWord(r.paramsWord);
    const int32_t x = wholeToFixed(r.point.x), y = wholeToFixed(r.point.y), z = wholeToFixed(r.point.z);
    a.prevPos.x = x;
    a.pos.x = x;
    a.prevPos.y = y;
    a.pos.y = y;
    a.prevPos.z = z;
    a.pos.z = z;
    a.prevRotation.x = 0;
    a.rotation.x = 0;
    const int16_t yaw = r.yaw;
    a.prevRotation.z = 0;
    a.prevRotation.y = yaw;
    a.rotation.y = yaw;
    a.rotation.z = 0;
}

// the identity of a record in an effect / controller actor (the flags set or ORed, the loop flags ORed, the type and
// parameter words)
void identityFromRecord(ActorHeader& a, const SpawnRecord& r, bool mergeFlags) {
    a.flags = mergeFlags ? uint8_t(a.flags | r.flags) : uint8_t(r.flags);
    a.loopFlags |= r.loopFlags;
    a.setTypeWord(r.typeWord);
    a.setParamsWord(r.paramsWord);
}

// the player placed by a room's spawn list: its flags (with its colour tables), its kind and the loaded body's
// variant, the record's parameters, state 0; placed: the position, the previous one, the yaw (the rest of the rotation
// and the previous one cleared)
void placePlayer(const SpawnRecord& r, int32_t placement) {
    Player& p = player();
    p.flags = uint8_t(r.flags | kActorColourTables);
    p.setTypePair(gameState().playerKind);
    p.setVariantPair(loadedBodyVariant());
    p.setParamsWord(r.paramsWord);
    p.enterState(PlayerState::Init);
    if (placement < 0) return;
    const int32_t x = wholeToFixed(r.point.x), y = wholeToFixed(r.point.y), z = wholeToFixed(r.point.z);
    p.pos.x = x;
    p.pos.y = y;
    p.pos.z = z;
    p.prevRotation.setXY(0, 0);
    p.rotation.x = 0;
    p.rotation.y = r.yaw;
    p.prevRotation.setZSpare(0, 0);
    p.rotation.z = 0;
    p.prevPos.x = x;
    p.prevPos.y = y;
    p.prevPos.z = z;
    p.prevRotation.y = p.rotation.y;
}

// the loop flags of a room script's static / effect record that ask for the stage's class table
uint8_t stageClassOf(const SpawnRecord& r) { return (r.loopFlags & kActorStageClass) ? kActorStageClass : 0; }

} // namespace

void spawnList(uint32_t list, int32_t placement) {
    for (uint32_t at = list;; at += kSpawnRecordSize) {
        const SpawnRecord r = dataRecord<SpawnRecord>(at, SpawnRecord::kBytes);
        if (r.flags == kSpawnListEnd) return;
        switch (SpawnPool(r.pool)) {
        case SpawnPool::Player:
            if (placement != kSpawnSkipPlayer) placePlayer(r, placement);
            break;
        case SpawnPool::Animated:
            if (const uint32_t a = allocActor20()) placeFromRecord(animatedActorAt(a), r, false);
            break;
        case SpawnPool::StaticStage:
        case SpawnPool::StaticGlobal: {
            const uint8_t loop = SpawnPool(r.pool) == SpawnPool::StaticStage ? kActorStageClass : 0;
            if (const uint32_t a = allocActor60(loop)) placeFromRecord(staticActorAt(a), r, false);
            break;
        }
        case SpawnPool::EffectStage:
        case SpawnPool::EffectGlobal:
            if (const uint32_t a =
                    allocActorA0(SpawnPool(r.pool) == SpawnPool::EffectStage ? kActorStageClass : 0)) {
                EffectActor& e = effectActorAt(a);
                identityFromRecord(e, r, false);
                e.setPositionWords(r.point);
            }
            break;
        case SpawnPool::Controller:
            if (const uint32_t a = allocActorE0()) {
                ControllerActor& c = controllerActorAt(a);
                identityFromRecord(c, r, false);
                c.setRecordPointWords(r.point);
            }
            break;
        default: break;
        }
    }
}

uint32_t spawnRecord(uint32_t record) {
    const SpawnRecord r = dataRecord<SpawnRecord>(record, SpawnRecord::kBytes);
    uint32_t a = 0;
    switch (SpawnPool(r.pool)) {
    case SpawnPool::Animated:
        a = allocActor20();
        if (a) placeFromRecord(animatedActorAt(a), r, true);
        return a;
    case SpawnPool::Projectile:
        a = allocActor40(kActorStageClass);
        if (a) placeFromRecord(projectileActorAt(a), r, true);
        return a;
    case SpawnPool::StaticStage:
    case SpawnPool::StaticGlobal:
        a = allocActor60(stageClassOf(r));
        if (a) placeFromRecord(staticActorAt(a), r, true);
        return a;
    case SpawnPool::EffectStage:
    case SpawnPool::EffectGlobal:
        a = allocActorA0(stageClassOf(r));
        if (a) {
            EffectActor& e = effectActorAt(a);
            identityFromRecord(e, r, true);
            e.pos.x = r.point.x;
            e.pos.y = r.point.y;
            e.pos.z = r.point.z;
            e.setRecordYaw(r.yaw);
        }
        return a;
    case SpawnPool::Controller:
        a = allocActorE0();
        if (a) {
            ControllerActor& c = controllerActorAt(a);
            identityFromRecord(c, r, true);
            c.setRecordPointShorts(r.point.x, r.point.y, r.point.z);
            c.setRecordYaw(r.yaw);
        }
        return a;
    default: return 0;
    }
}

uint32_t spawnRecords(uint32_t list, uint32_t n) {
    RoomScriptState& room = roomScriptState();
    uint32_t i = 0;
    for (uint32_t at = list; i < n; ++i, at += kSpawnRecordSize) {
        const SpawnRecord r = dataRecord<SpawnRecord>(at, SpawnRecord::kBytes);
        const uint32_t a = spawnRecord(at);
        if (!a) break;
        room.actors[r.listSlot] = a;
        room.actorMask |= 1u << (i & 0x1f); // the bit of the list index (the exe's: not the record's slot)
    }
    return i;
}

uint32_t spawnRecordTracked(uint32_t record) {
    const uint32_t a = spawnRecord(record);
    if (!a) return 0;
    const SpawnRecord r = dataRecord<SpawnRecord>(record, SpawnRecord::kBytes);
    RoomScriptState& room = roomScriptState();
    room.actors[r.listSlot] = a;
    room.actorMask |= 1u << (r.listSlot & 0x1f);
    return a;
}

} // namespace od::port
