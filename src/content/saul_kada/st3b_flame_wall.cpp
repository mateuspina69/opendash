// The pirate base's burning beam / flame wall (static ST3B type 0x1c, exe 0x5a3330; hypothesis names): it lives its
// parameter's ticks; its two phase angles shade its OT polygons (render output, not drawn). Every tick: its variant's
// row (exe 0x8cec10) of capsules from its base to the point the row's reach ahead along its yaw, stacked up by the
// row's step (the damage of its type's row). States [0x8cec18]: 0 start 0x5a3350, 1 burn 0x5a33a0.
// Its own bytes overlay the actor's contact block from +0x64 (it has no model).
#include "content/saul_kada/st3b_flame_wall.hpp"
#include "content/saul_kada/st3b_flame_wall_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/hit_list.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st3b_flame_wall_state;

namespace {

enum class FlameState : uint8_t { Start = 0, Burn = 1 };
// A flame wall's shape (exe 0x8cec10, by variant): the capsules, their radius (* 4), the reach ahead, the step up
struct FlameRow : DataRecord {
    uint8_t capsules = 0;
    int16_t radius = 0;
    int16_t reach = 0;
    int16_t step = 0;
    static FlameRow load(const TableReader& r) {
        FlameRow rec;
        rec.capsules = r.u8(0);
        rec.radius = r.s16(2);
        rec.reach = r.s16(4);
        rec.step = r.s16(6);
        return rec;
    }
};
const DataTable<RecordTable<FlameRow, 8, 8>> flameRows{TableId::St3bFlameWallRows};
// its capsules: group 6, hurting what the player throws / a strong hit
constexpr uint32_t kFlameShape = hitGroup(6) | hitCapsule(3);
constexpr uint32_t kFlameAttack = kHitAttackThrown | kHitAttackStrong;

// exe 0x5a3350
void start(StaticActor& actor, FlameFields& flame) {
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.collisionBox = nullptr;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.maxHitPoints = 1;
    actor.hitPoints = 1;
    flame.clearBytes();
    flame.ticksLeft = FlameWallParams(actor).life;
    actor.enterState(uint8_t(FlameState::Burn));
}
// exe 0x5a33a0
void burn(StaticActor& actor, FlameFields& flame) {
    const uint16_t phaseA = uint16_t((flame.phaseA + 0x200u) & 0x7ff), phaseB = uint16_t((flame.phaseB + 0x100u) & 0x7ff);
    --flame.ticksLeft;
    const FlameRow row = flameRows[actor.variant];
    flame.phaseA = phaseA;
    flame.phaseB = phaseB;
    if (flame.ticksLeft == 0) {
        freeActor(actor); // exe 0x5e1540
        return;
    }
    int16_t dx, dz;
    angleVector(actor.rotation.y, row.reach, dx, dz); // exe 0x5e40c0
    int16_t base[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    int16_t tip[3] = {int16_t(uint16_t(actor.pos.x.whole) + uint16_t(dx)), actor.pos.y.whole,
                      int16_t(uint16_t(actor.pos.z.whole) + uint16_t(dz))};
    const uint16_t radius = uint16_t(int16_t(row.radius >> 2));
    const StaticActorStats stats = staticStats(actor.type, 0);
    for (uint32_t n = row.capsules; n != 0; --n) {
        hitListSubmitCapsule(base, radius, tip, radius, flame.hitBox, hitOwner(actor.slot) | kFlameShape,
                             uint32_t(stats.damage) | kFlameAttack); // exe 0x5e4190
        base[1] = int16_t(base[1] - row.step);
        tip[1] = int16_t(tip[1] - row.step);
    }
    // the flame polygons (OT, shaded by the phases): render output
}
// exe 0x5a3330
void flameWall(StaticActor& actor) {
    FlameFields& flame = classState<FlameFields>(actor);
    switch (FlameState(actor.state)) {
    case FlameState::Start: start(actor, flame); return;
    case FlameState::Burn: burn(actor, flame); return;
    default: notPortedAt(kStoryPort, "0x5a3330 state (table 0x8cec18)", actor.state);
    }
}

} // namespace

void flameWallUpdate(const PlayerEnv&, uint32_t actor) { flameWall(staticActorAt(actor)); }

} // namespace od::port
