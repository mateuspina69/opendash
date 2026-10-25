// The gold-statue fight's minion spawner (ST3C pool 0xe0 type 0x27, exe 0x427740; hypothesis names). States
// [0x88c030]: 0 start 0x427760, 1 run 0x4277b0, 2 freed 0x427cf0. The exe keeps its block's address (+0x10) in the
// global 0xa59dd0 every tick.
// While fewer than 3 minions live its timer ((rng & 0x3f) + 0xb4 ticks) runs; then it picks points 0x600..0x9ff from
// the player (around the camera's yaw: +-0x200 for its first 10 tries while the player is outside the box 0x88c050,
// else any) until one takes a minion (exe 0x427950: the point's ground found and the point inside the box 0x88c04c):
// the minion there facing away from the player, given the spawner's first free slot byte (it clears it when it goes).
// Deviation: exe 0x5e40c0's 4th word (stack garbage, stored as the projection point's yaw) is 0.
#include "content/saul_kada/st3c_minion_spawner.hpp"
#include "content/saul_kada/st3c_minion_spawner_state.hpp"

#include "content/saul_kada/st3c_minion.hpp"
#include "content/saul_kada/st3c_minion_state.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/camera_state.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection_state.hpp"
#include "game/random.hpp"
#include "game/room_grid.hpp"
#include "game/story.hpp"
#include "game/vectors.hpp"

using namespace od::port::st3c_minion_spawner_state;

namespace od::port {

using namespace st3c_minion_spawner_state;

namespace {

enum class SpawnerState : uint8_t { Start = 0, Run = 1, Free = 2 };
enum class SpawnerPhase : uint8_t { Wait = 0, Place = 1 };
// the exe's global holding the running spawner's block
constexpr uint16_t kMaxAlive = 3;
constexpr uint8_t kNearTries = 0xa;
constexpr uint32_t kSlotsOffset = 0x14; // its slot bytes in its slot

uint16_t newTimer() { return uint16_t((rngStep() & 0x3f) + 0xb4); }
// exe 0x427950(actor, point): a minion at the s16 point when its ground is found and the point is inside the placing
// box; true once spawned
bool spawnAt(const PlayerEnv& env, SpawnerFields& spawner, const ShortVec4& point) {
    const int32_t at[3] = {int32_t(uint32_t(int32_t(point.x)) << 16), int32_t(uint32_t(int32_t(point.y)) << 16),
                           int32_t(uint32_t(int32_t(point.z)) << 16)};
    const int16_t p[3] = {point.x, point.y, point.z};
    if (probeGroundAt(env, p, CollisionBoxRef::record(stateAddress(TableId::St3cMinionSpawnerMinionBox)), 0x7fff, 2) < 0) return false; // exe 0x40f1c0
    if (!pointInCellBox(int16_t(uint32_t(at[0]) >> 16), int16_t(uint32_t(at[2]) >> 16), CellBox(stateAddress(TableId::St3cMinionSpawnerPlaceBox))))
        return false;                    // exe 0x5e5af0
    const uint32_t slot = allocActor20(); // exe 0x5e1160
    if (!slot) return false;
    AnimatedActor& minion = animatedActorAt(slot);
    minion.loopFlags |= kActorStageClass | kActorSweepGroup2;
    minion.flags = kActorLive;
    minion.type = kStatueMinionType;
    minion.subtype = 0;
    minion.variant = 0;
    minion.spawnByte = 0;
    minion.pos.x = at[0];
    minion.pos.y = at[1];
    minion.rotation.x = 0;
    minion.pos.z = at[2];
    minion.rotation.y = int16_t(angleOf(player().pos, minion.pos));
    minion.rotation.z = 0;
    spawner.alive += 1u;
    for (size_t i = 0; i < spawner.slots.size(); ++i)
        if (spawner.slots[i] == 0) {
            spawner.slots[i] = 1;
            StatueMinionParams(minion).spawnerSlot = slotFieldWord(spawner.owner, kSlotsOffset + uint32_t(i));
            classState<st3c_minion_state::MinionFields>(minion).spawnerSlot = &spawner.slots[i];
            return true;
        }
    return true;
}
// exe 0x427760
void start(ControllerActor& actor, SpawnerFields& spawner) {
    spawner.clearBlock(); // exe 0x40dc40
    spawner.timer = newTimer();
    actor.enterState(uint8_t(SpawnerState::Run));
}
// exe 0x4277b0
void run(const PlayerEnv& env, ControllerActor& actor, SpawnerFields& spawner) {
    spawner.alive = 0;
    for (size_t i = 0; i < spawner.slots.size(); ++i)
        if (spawner.slots[i] != 0) spawner.alive += 1u;
    switch (SpawnerPhase(actor.subState)) {
    case SpawnerPhase::Wait:
        if (spawner.alive >= kMaxAlive) return;
        spawner.timer -= 1u;
        if (spawner.timer != 0) return;
        spawner.timer = newTimer();
        spawner.nearTries = 0;
        ++actor.subState;
        return;
    case SpawnerPhase::Place: {
        const int distance = int((rngStep() & 0x3ff) + 0x600);
        int angle;
        if (spawner.nearTries < kNearTries && !pointInCellBox(player().pos, CellBox(stateAddress(TableId::St3cMinionSpawnerNearBox)))) { // exe 0x5e5af0
            angle = int((rngLastValue() >> 0xa) & 0x3ff) - 0x200;
            spawner.nearTries += 1u;
        } else {
            angle = int((rngLastValue() >> 0xa) & 0xfff) - 0x800;
        }
        int16_t dx, dz;
        angleVector(angle + cameraState().viewAngles.y, -distance, dx, dz); // exe 0x5e40c0 into the projection point
        LaneVec4& point = projectionScratch().point;
        point.x = dx;
        point.y = 0;
        point.z = dz;
        projectionScratch().pointYaw = 0;
        const Player& p = player();
        point.x = int16_t(uint16_t(uint16_t(p.pos.x.whole) + uint16_t(point.x)));
        point.y = int16_t(uint16_t(uint16_t(p.pos.y.whole) + uint16_t(point.y)));
        point.z = int16_t(uint16_t(uint16_t(p.pos.z.whole) + uint16_t(point.z)));
        if (spawnAt(env, spawner, point)) actor.subState = uint8_t(SpawnerPhase::Wait);
        return;
    }
    default: return;
    }
}
void minionSpawner(const PlayerEnv& env, ControllerActor& actor) { // exe 0x427740
    SpawnerFields& spawner = classState<SpawnerFields>(actor);
    st3cMinionSpawnerGlobals().spawnerBlock = slotFieldWord(actor, kSlotPositionOffset); // its timer's exe address (the record point's first lane)
    switch (SpawnerState(actor.state)) {
    case SpawnerState::Start: start(actor, spawner); return;
    case SpawnerState::Run: run(env, actor, spawner); return;
    case SpawnerState::Free: freeActor(actor); return; // 0x427cf0: exe 0x5e15c0
    default: notPortedAt(kStoryPort, "0x427740 state (table 0x88c030)", actor.state);
    }
}

} // namespace

void minionSpawnerUpdate(const PlayerEnv& env, uint32_t actor) { minionSpawner(env, controllerActorAt(actor)); }

} // namespace od::port
