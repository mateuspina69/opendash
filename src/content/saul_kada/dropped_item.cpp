// The dropped item of the pirate attack (static ST2B / ST3B type 8, exe 0x596670; hypothesis names): tossed out (kind
// 0), floating (1) or spinning (2); touched by the player (within its variant's reach of a point raised by its variant's
// height: exe 0x8ce0fc) -> its flag, the "got it" event (its message) and freed.
// States: 0 bind 0x5966d0, 1 run 0x5967c0 (the sub-states: 0 toss 0x596870, 1 float 0x596910), 2 / 3 the sub-states
// directly.
#include "content/saul_kada/dropped_item.hpp"
#include "content/saul_kada/dropped_item_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/distances.hpp"
#include "game/event.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace dropped_item_state;

namespace {

enum class ItemState : uint8_t { Bind = 0, Run = 1, Toss = 2, Float = 3 };
enum class ItemSub : uint8_t { Toss = 0, Float = 1 };
enum class ItemKind : int8_t { Tossed = 0, Floating = 1, Spinning = 2 };
constexpr uint8_t kTossWall = 0x01, kTossFloor = 0x02;
// A variant's touch (exe 0x8ce0fc, 4 bytes): the reach, the height its point is raised by
struct ItemTouch : DataRecord {
    int16_t reach = 0;
    uint16_t height = 0;
    static ItemTouch load(const TableReader& r) {
        ItemTouch rec;
        rec.reach = r.s16(0);
        rec.height = r.u16(2);
        return rec;
    }
};
const DataTable<RecordTable<ItemTouch, 0x10, 4>> itemTouches{TableId::DroppedItemTouches};
constexpr uint32_t kGotItEvent = 0x20000;

// exe 0x596870: falls, bounces once off the floor, then floats
void toss(const PlayerEnv& env, StaticActor& actor, ItemFields& item) {
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e3690
    actor.velocity.y += 0x40u;
    actor.rotation.x += 0x200u;
    if (item.contacts & kTossFloor) {
        if (actor.step == 0) {
            actor.velocity.y = int16_t(-(actor.velocity.y >> 1));
            actor.rotation.x = 0;
            actor.enterStep(1);
        } else {
            actor.subState = uint8_t(ItemSub::Float);
            actor.restartStep();
        }
    }
    item.contacts = 0;
    if (int32_t(actorTestMove(env, actor)) >= 0) return; // exe 0x40f570
    const uint32_t result = actorPushOut(env, actor);    // exe 0x410960
    if (result & 0xf00) item.contacts = kTossWall;
    if (reflectRotationOffWalls(actor.rotation, result)) item.contacts |= kTossFloor; // exe 0x410740
}
// exe 0x596910: settles 8 units, then bobs (the 64-step sine) and spins
void floatAround(StaticActor& actor, ItemFields& item) {
    if (actor.step == 0) {
        actor.rotation.x &= 0xfff;
        item.bobPhase = 0x20;
        item.restY = actor.pos.y.whole;
        item.settleTicks = 8;
        actor.enterStep(1);
    }
    if (actor.step == 1) {
        --item.restY;
        --item.settleTicks;
        if (item.settleTicks == 0) actor.enterStep(2);
    }
    actor.pos.y.whole = int16_t((trig64Tables().sine[size_t(int32_t(int8_t(item.bobPhase)))] >> 8) + item.restY);
    item.bobPhase = uint8_t((item.bobPhase + 1u) & 0x3f);
    const int8_t kind = DroppedItemParams(actor).kind;
    if (kind < 0 || kind > 1) return;
    if (actor.rotation.x != 0) {
        const int16_t pitch = int16_t(uint16_t(actor.rotation.x) + 0x200u);
        actor.rotation.x = pitch;
        if (pitch > 0xfff) actor.rotation.x = 0;
        return;
    }
    actor.rotation.y += 0x80u;
}
// exe 0x5966d0
void bind(StaticActor& actor) {
    if (!bindActorModel(actor, actor.variant)) {
        freeActor(actor);
        return;
    }
    actor.runFlags |= kActorWarpKept;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.velocity.y = int16_t(0xfd00);
    actor.velocity.z = int16_t(0xffc0);
    actor.spin.x = 0x200;
    actor.spin.z = int16_t(0xfff0);
    actor.airborne = 1;
    actor.onPlatform = 0;
    actor.collisionBox.pointAt(TableId::DroppedItemBox);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.clearClassBlock();
    actor.enterState(uint8_t(ItemState::Run));
    switch (ItemKind(DroppedItemParams(actor).kind.get())) {
    case ItemKind::Tossed: actor.rotation.y = int16_t((rngStep() & 0x1f) << 7); return;
    case ItemKind::Floating:
        actor.rotation.y = int16_t((rngStep() & 0x1f) << 7);
        actor.subState = uint8_t(ItemSub::Float);
        return;
    case ItemKind::Spinning:
        actor.rotation.z = 0x400;
        actor.subState = uint8_t(ItemSub::Float);
        return;
    default: return;
    }
}
// exe 0x5967c0
void run(const PlayerEnv& env, StaticActor& actor, ItemFields& item) {
    if (ItemSub(actor.subState) == ItemSub::Toss) toss(env, actor, item);
    else if (ItemSub(actor.subState) == ItemSub::Float) floatAround(actor, item);
    const Player& p = player();
    if (p.carryFlags != 0 || (p.wasAirborne | p.airborne) != 0) return;
    // exe 0x5e3a60(point, player): the integer parts, the item's point raised by its variant's height
    const ItemTouch touch = itemTouches[actor.variant];
    const int16_t point[3] = {actor.pos.x.whole, int16_t(uint16_t(actor.pos.y.whole) + uint16_t(touch.height)),
                              actor.pos.z.whole};
    const int16_t playerPoint[3] = {p.pos.x.whole, p.pos.y.whole, p.pos.z.whole};
    const uint32_t d = uint32_t(distance3(point, playerPoint));
    if (d >= uint32_t(int32_t(touch.reach))) return;
    const DroppedItemParams params(actor);
    setStoryFlag(params.flag);
    eventStartAtActor(actor, kGotItEvent, 0, uint16_t(int16_t(params.message.get()))); // exe 0x543af0
    freeActor(actor);
}

void droppedItem(const PlayerEnv& env, StaticActor& actor) {
    ItemFields& item = classState<ItemFields>(actor);
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (ItemState(actor.state)) {
    case ItemState::Bind: bind(actor); return;
    case ItemState::Run: run(env, actor, item); return;
    case ItemState::Toss: toss(env, actor, item); return;
    case ItemState::Float: floatAround(actor, item); return;
    default: notPortedAt(kStoryPort, "0x596670 state", actor.state);
    }
}

} // namespace

void droppedItemUpdate(const PlayerEnv& env, uint32_t actor) { droppedItem(env, staticActorAt(actor)); }

} // namespace od::port
