// The sliding doors of the pirate base's gate rooms (ST41 / ST42 static type 0x58, exe 0x5ba660; hypothesis names).
// States [0x8d0c2c]: 0 start 0x5ba6a0 (variant 0 spawns its twin and closes its cell), 1 wait 0x5ba780 (the player in
// the 0x800 x 0x200 box in front of it: locked -> its mirror flag set; else its cell opened), 2 slide 0x5ba8c0 (8
// ticks of +-0x1c0 across), 3 freed 0x5afdd0.
#include "content/saul_kada/st41_sliding_door.hpp"
#include "content/saul_kada/st41_sliding_door_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/field_alias.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace st41_sliding_door_state;

namespace {

enum class DoorState : uint8_t { Start = 0, Wait = 1, Slide = 2, Free = 3 };
enum class SlidePhase : uint8_t { Start = 0, Slide = 1, Done = 2 };
enum class DoorSide : uint8_t { Main = 0, Twin = 1 };
// The door's spawn parameters: its grid cell (column / row), its mirror flag's number (0x710 + it; 0: none), its lock's
// index in the lock flags
struct DoorParams {
    explicit DoorParams(const StaticActor& actor) : DoorParams(const_cast<StaticActor&>(actor)) {}
    explicit DoorParams(StaticActor& actor) : column(actor.params[0]), row(actor.params[1]), mirrorFlag(actor.params[2]), lock(actor.params[3]) {}
    FieldAlias<int8_t, uint8_t> column;
    FieldAlias<int8_t, uint8_t> row;
    FieldAlias<int8_t, uint8_t> mirrorFlag;
    FieldAlias<int8_t, uint8_t> lock;
};
constexpr uint8_t kDoorType = 0x58;
const DataTable<ScalarTable<uint16_t, 7>> lockFlags{TableId::St41SlidingDoorLockFlags}; // 0x684, 0x685, 0x686, 0x684, 0x685, 0x686, 0x687
constexpr uint32_t kMirrorFlags = 0x710;
constexpr uint16_t kOpenSound = 0xba, kStopSound = 0xbd;
constexpr int kSlideSpeed = 0x1c0;

bool isMain(const StaticActor& actor) { return DoorSide(actor.variant) == DoorSide::Main; }
// exe 0x58d5f0 on its cell (the exe's inline cell index of its column / row)
void switchCell(const DoorParams& params, uint8_t variant) {
    switchGridCellVariant(uint32_t(int32_t(params.column.get())), uint32_t(int32_t(params.row.get())), variant);
}
bool locked(const DoorParams& params) { return storyFlag(lockFlags[size_t(int32_t(params.lock.get()))]); } // 0x510e50

// exe 0x5ba6a0: its model (by its variant; the exe stays here until it binds), its box; the main door spawns its twin
// and closes its cell
void start(StaticActor& actor) {
    if (!bindActorModel(actor, actor.variant)) return; // exe 0x5e0bc0
    actor.collisionBox.pointAt(TableId::St41SlidingDoorBox);
    actor.clearClassBlock(); // exe 0x40dc40
    actor.setDrawn(false);
    actor.enterState(uint8_t(DoorState::Wait));
    if (!isMain(actor)) return;
    const DoorParams params(actor);
    if (const uint32_t slot = allocActor60(kActorStageClass)) { // exe 0x5e12f0: the twin
        StaticActor& twin = staticActorAt(slot);
        DoorParams twinParams(staticActorAt(slot));
        twin.flags = kActorLive;
        twin.type = kDoorType;
        twin.variant = uint8_t(DoorSide::Twin);
        twin.pos = actor.pos;
        twin.rotation = actor.rotation;
        twinParams.mirrorFlag = params.mirrorFlag;
        twinParams.lock = params.lock;
    }
    switchCell(params, 0); // closed
}
// exe 0x5ba780: the player in the 0x800 x 0x200 box in front of it (its axes by the yaw's bit 0x400)
void wait(StaticActor& actor) {
    const DoorParams params(actor);
    const int32_t dx = int32_t(actor.pos.x.whole) - int32_t(player().pos.x.whole);
    const int32_t dz = int32_t(actor.pos.z.whole) - int32_t(player().pos.z.whole);
    const bool across = (uint16_t(actor.rotation.y) & 0x400) != 0;
    const int32_t along = across ? dx : dz, side = across ? dz : dx;
    if (uint32_t(along + 0x400) < 0x800 && uint32_t(side + 0x100) < 0x200) {
        const int32_t mirror = params.mirrorFlag;
        if (mirror != 0) {
            if (locked(params)) {
                setStoryFlag(uint32_t(mirror + int32_t(kMirrorFlags))); // exe 0x510e10
                return;
            }
            clearStoryFlag(uint32_t(mirror + int32_t(kMirrorFlags))); // exe 0x510e30
        }
        actor.setDrawn(true);
        actor.enterState(uint8_t(DoorState::Slide));
        if (isMain(actor)) switchCell(params, 1); // open
        return;
    }
    if (!isMain(actor)) return;
    const int32_t mirror = params.mirrorFlag;
    if (mirror == 0) return;
    if (locked(params)) clearStoryFlag(uint32_t(mirror + int32_t(kMirrorFlags)));
}
// exe 0x5ba8c0: the slide (8 ticks across, the twin the other way)
void slide(StaticActor& actor, DoorFields& door) {
    switch (SlidePhase(actor.subState)) {
    case SlidePhase::Start:
        ++actor.subState;
        actor.setDrawn(true);
        door.slideTicks = 8;
        if (isMain(actor)) soundPlayAtPos(kOpenSound, actor.pos);
        break;
    case SlidePhase::Slide: break;
    default: return;
    }
    door.slideTicks -= 1u;
    if (door.slideTicks == 0) {
        ++actor.subState;
        actor.setDrawn(false);
        if (isMain(actor)) soundPlayAtPos(kStopSound, actor.pos);
    }
    integrateVelocity(actor, isMain(actor) ? -kSlideSpeed : kSlideSpeed, 0, 0); // exe 0x5e3690
}
void slidingDoor(StaticActor& actor) { // exe 0x5ba660
    DoorFields& door = classState<DoorFields>(actor);
    actor.savePreviousPose();
    switch (DoorState(actor.state)) {
    case DoorState::Start: start(actor); return;
    case DoorState::Wait: wait(actor); return;
    case DoorState::Slide: slide(actor, door); return;
    case DoorState::Free: freeActor(actor); return; // 0x5afdd0 -> exe 0x5e1540
    default: notPortedAt(kStoryPort, "0x5ba660 state (table 0x8d0c2c)", actor.state);
    }
}

} // namespace

void slidingDoorUpdate(const PlayerEnv&, uint32_t actor) { slidingDoor(staticActorAt(actor)); }

} // namespace od::port
