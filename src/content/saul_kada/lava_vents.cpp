// The lava floor of the fire ruins (ST27 / ST28 / ST34: pool e0 type 0x16, exe 0x4251e0; hypothesis names), one routine
// per exe routine. States [0x88bdf4]: 0 start 0x425200 (the first plume in 0x3c ticks), 1 run 0x425220, 2 freed
// 0x427cf0.
#include "content/saul_kada/lava_vents.hpp"
#include "content/saul_kada/lava_vents_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/camera_state.hpp"
#include "game/footstep_dust.hpp"
#include "game/game_state.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_status.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/room_grid.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace lava_vents_state;

namespace {

enum class VentsState : uint8_t { Start = 0, Run = 1, Free = 2 };
// A list of the segment kinds that raise plumes (exe data): s8 count, then the kinds
struct VentKindList {
    explicit VentKindList(uint32_t list) : at(list), count(dataScalar<int8_t>(list)) {}
    uint32_t at;
    int8_t count;
    uint8_t kind(int i) const { return dataScalar<uint8_t>(at + 1 + uint32_t(i)); }
};
// by LavaVentsParams::ventList (exe data 0x88be4c: the lists' addresses; list 0 is filled at run time in the game
// state, the others are exe data: their reads resolve the address, game/loaded_data)
const DataTable<ScalarTable<uint32_t, 4>> ventKindLists{TableId::LavaVentKindLists};
const DataTable<ScalarTable<int16_t, 4>> burnHeights{TableId::LavaVentsBurnHeights}; // by variant: the player burns below it
const DataTable<ScalarTable<uint16_t, 4>> plumeAngles{TableId::LavaVentsPlumeAngles}; // the plume's bearing from the player, around the camera's yaw
const DataTable<ScalarTable<int16_t, 4>> plumeDistances{TableId::LavaVentsPlumeDistances}; // ... and its distance
constexpr uint8_t kLavaPlumeType = 0x2e;
constexpr uint16_t kFirstPlumeTicks = 0x3c;
// the segment kinds whose plumes stand half a 1024 block off the segment's edge
constexpr uint8_t kVentKindNear = 0x40, kVentKindLeft = 0x41, kVentKindFar = 0x42;
enum class VentKind : int { Absent = -1, Centre = 0, Near = 1, Left = 2, Far = 3 };
// the lava cells (floor attributes 5..8)
constexpr int kLavaAttributeFirst = 5, kLavaAttributeLast = 8;

int16_t truncate1024(int16_t v) { return int16_t((int32_t(v) / 0x400) * 0x400); } // (v + (v < 0 ? 0x3ff : 0)) >> 10 << 10

// exe 0x425510(vents, segment): the segment's kind in the vents' list -> Absent when not listed; the listed kinds
// 0x40 / 0x41 / 0x42 -> Near / Left / Far, any other listed kind -> Centre
VentKind ventKind(const ControllerActor& actor, const SegmentEntry& segment) {
    const VentKindList list(ventKindLists[size_t(int32_t(LavaVentsParams(actor).ventList.get()))]);
    const int n = list.count;
    for (int i = 0; i < n; ++i)
        if (segment.poolIndex == list.kind(i)) {
            const uint8_t k = list.kind(i);
            return k == kVentKindNear ? VentKind::Near
                   : k == kVentKindLeft ? VentKind::Left
                   : k == kVentKindFar  ? VentKind::Far
                                        : VentKind::Centre;
        }
    return VentKind::Absent;
}
// a lava plume (stage static type 0x2e), nullptr when the pool is full
StaticActor* spawnPlume() {
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!slot) return nullptr;
    StaticActor& plume = staticActorAt(slot);
    plume.flags = kActorLive;
    plume.loopFlags |= kActorSweepGroup2;
    plume.type = kLavaPlumeType;
    return &plume;
}
void placePlume(StaticActor& plume, int x, int y, int z) {
    plume.pos.x.whole = int16_t(x);
    plume.pos.y.whole = int16_t(y);
    plume.pos.z.whole = int16_t(z);
}

// exe 0x425220
void runVents(const PlayerEnv& env, ControllerActor& actor, VentsFields& vents) {
    Player& p = player();
    if (burnFloor(env, p, burnHeights[actor.variant]) && (gameState().frame & 7) == 0)
        footstepDustAtFoot(p); // exe 0x5ad170(player, 0, point): variant 0 reads no point
    if (vents.plumeTicks != 0) {
        --vents.plumeTicks;
        return;
    }
    vents.plumeTicks = uint16_t((rngStep() & 0x3f) + 0x28);
    rngStep();
    const uint32_t i = rngStep() & 3;
    int16_t dx, dz;
    angleVector(int((plumeAngles[i] + uint16_t(cameraState().viewAngles.y)) & 0xfff), plumeDistances[i], dx,
                dz); // exe 0x5e40c0
    // the point near the player (integer parts), its height from the ground probe under the plume's box
    int16_t point[3] = {int16_t(dx + p.pos.x.whole), p.pos.y.whole, int16_t(dz + p.pos.z.whole)};
    point[1] = int16_t(probeGroundAt(env, point, CollisionBoxRef::record(stateAddress(TableId::LavaVentsPlumeProbeBox)), 0, 0xffff)); // exe 0x40f1c0
    // exe 0x40f0b0: the grid cell under the point
    const RoomGridState& grid = roomGrid();
    const uint32_t column = uint32_t(gridColumn(point[0])), row = uint32_t(gridColumn(point[2]));
    if (column - grid.originX >= grid.width || row - grid.originZ >= grid.depth) return;
    const uint16_t cell = GridCell(gridCellAtIndex(column, row)).word;
    if (cell & kCellFloor) { // a lava cell: the plume on the cell's centre
        const int attribute = cell & 0x1f;
        if (attribute < kLavaAttributeFirst || attribute > kLavaAttributeLast) return;
        StaticActor* plume = spawnPlume();
        if (!plume) return;
        placePlume(*plume, ((int16_t(point[0]) >> 9) << 9) + 0x100, point[1], ((int16_t(point[2]) >> 9) << 9) + 0x100);
        return;
    }
    const VentKind kind = ventKind(actor, segmentEntry(cell & kCellSegmentMask));
    if (kind == VentKind::Absent) return;
    StaticActor* plume = spawnPlume();
    if (!plume) return;
    switch (kind) {
    case VentKind::Near:
        placePlume(*plume, point[0], point[1], truncate1024(point[2]) + (point[2] & 0x1ff) + 0x200);
        break;
    case VentKind::Left:
        placePlume(*plume, truncate1024(point[0]) - (point[0] & 0x1ff) - 0x200, point[1], point[2]);
        break;
    case VentKind::Far:
        placePlume(*plume, point[0], point[1], truncate1024(point[2]) - (point[2] & 0x1ff) - 0x200);
        break;
    default: placePlume(*plume, point[0], point[1], point[2]); break;
    }
}

// exe 0x4251e0 [0x88bdf4]
void lavaVents(const PlayerEnv& env, ControllerActor& actor) {
    VentsFields& vents = classState<VentsFields>(actor);
    switch (VentsState(actor.state)) {
    case VentsState::Start: // 0x425200
        ++actor.state;
        vents.plumeTicks = kFirstPlumeTicks;
        return;
    case VentsState::Run: runVents(env, actor, vents); return;
    case VentsState::Free: freeActor(actor); return; // 0x427cf0 -> exe 0x5e15c0
    default: notPortedAt(kStoryPort, "0x4251e0 state", actor.state);
    }
}

} // namespace

void lavaVentsUpdate(const PlayerEnv& env, uint32_t actor) { lavaVents(env, controllerActorAt(actor)); }

} // namespace od::port
