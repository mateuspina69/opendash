// The ST20 props (static type 1, exe 0x5904d0; hypothesis names): variant 0 a solid bell / gong (a thrown hit rings it:
// sound 0xc7, 0xf ticks between), 1 a spinner (its roll -0x10 a tick), 2 a swinger (its pitch the sine >> 7 of its
// tick). States [0x8cdcfc]: 0 bind 0x5904f0, 1 run 0x590580, 4 a plain bind 0x58eeb0, 5 a counter 0x590690, 2 / 3 /
// 6 / 7 nothing.
#include "content/saul_kada/st20_props.hpp"
#include "content/saul_kada/st20_props_state.hpp"

#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/hit_list.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st20_props_state;

namespace {

enum class PropState : uint8_t {
    Bind = 0, Run = 1, Idle = 2, IdleToo = 3, PlainBind = 4, Count = 5, Idle6 = 6, Idle7 = 7,
};
enum class PropVariant : uint8_t { Gong = 0, Spinner = 1, Swinger = 2 };
// The prop's parameter: the counter state 5 advances
struct PropParams {
    explicit PropParams(const StaticActor& actor) : PropParams(const_cast<StaticActor&>(actor)) {}
    explicit PropParams(StaticActor& actor) : counter(actor.params[0]) {}
    uint8_t& counter;
};
constexpr uint16_t kRingSound = 0xc7;
constexpr uint16_t kRingCooldown = 0xf;
// the gong's hit entry: a body (radius 0x40), not hurting, a bump
constexpr uint32_t kGongShape = hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x40);
constexpr uint32_t kGongAttack = kHitAttackHarmless | kHitAttackBump;

// exe 0x5904f0
void bind(StaticActor& actor, PropFields& prop) {
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
        freeActor(actor);
        return;
    }
    prop.hitBox.result = 0;
    prop.word = 0;
    actor.velocity.clearWords();
    actor.acceleration.clearWords();
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.prevPos = actor.pos;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.collisionBox = nullptr;
    actor.lastMover = nullptr;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.prevRotation = actor.rotation;
    ++actor.state;
}
// exe 0x590580
void run(StaticActor& actor, PropFields& prop) {
    ++prop.tick;
    if (prop.ringCooldown) --prop.ringCooldown;
    switch (PropVariant(actor.variant)) {
    case PropVariant::Swinger:
        actor.rotation.x = int16_t(fixedSin(int(prop.tick & 0x7fu) << 5) >> 7);
        return;
    case PropVariant::Spinner: actor.rotation.z = int16_t((uint16_t(actor.rotation.z) - 0x10u) & 0xfff); return;
    case PropVariant::Gong: {
        actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0); // exe 0x414050
        if (prop.ringCooldown == 0 && (prop.hitBox.result & kHitAttackThrown)) {
            soundPlayAtPos(kRingSound, actor.pos);
            prop.ringCooldown = kRingCooldown;
        }
        const int16_t pos[3] = {actor.pos.x.whole, int16_t(uint16_t(actor.pos.y.whole) - 0x40u), actor.pos.z.whole};
        prop.hitBox.result = 0;
        prop.hitBox.previousX = actor.prevPos.x.whole;
        prop.hitBox.previousZ = actor.prevPos.z.whole;
        hitListSubmit(pos, prop.hitBox, hitOwner(actor.slot) | kGongShape, kGongAttack); // exe 0x5e4120
        return;
    }
    default: return;
    }
}

void st20Prop(StaticActor& actor) {
    PropFields& prop = classState<PropFields>(actor);
    switch (PropState(actor.state)) {
    case PropState::Bind: bind(actor, prop); return;
    case PropState::Run: run(actor, prop); return;
    case PropState::PlainBind: // 0x58eeb0
        if (!bindActorModel(actor, actor.variant)) {
            freeActor(actor);
            return;
        }
        ++actor.state;
        return;
    case PropState::Count: ++PropParams(actor).counter; return; // 0x590690
    case PropState::Idle:
    case PropState::IdleToo:
    case PropState::Idle6:
    case PropState::Idle7: return; // 0x52ef00
    default: notPortedAt(kStoryPort, "0x5904d0 state (table 0x8cdcfc)", actor.state);
    }
}

} // namespace

void st20PropUpdate(const PlayerEnv&, uint32_t actor) { st20Prop(staticActorAt(actor)); }

} // namespace od::port
