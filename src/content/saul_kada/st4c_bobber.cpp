// The ST4C bobbing object (static type 0x40, exe 0x5b1760; hypothesis names): it spins (0x20 a tick) and bobs around
// its rest height (sin(frame << 4) << 8); a hit capsule (radius 0x10, 0x80 above / below it) that deflects the player's
// shots and takes no hit from them.
#include "content/saul_kada/st4c_bobber.hpp"

#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/field_alias.hpp"
#include "game/game_state.hpp"
#include "game/hit_list.hpp"

namespace od::port {

namespace {

enum class BobberState : uint8_t { Bind = 0, Bob = 1 };
// The bobber's rest height (16.16), kept in its parameters' word
struct BobberParams {
    explicit BobberParams(const StaticActor& actor) : BobberParams(const_cast<StaticActor&>(actor)) {}
    explicit BobberParams(StaticActor& actor) : restY(actor.params, 0) {}
    BytesField<int32_t, 4> restY;
};
// Its own bytes: its hit box
struct BobberFields {
    explicit BobberFields(const StaticActor& actor) : BobberFields(const_cast<StaticActor&>(actor)) {}
    explicit BobberFields(StaticActor& actor) : hitBox(actor.hitBox) {}
    HitBoxHead& hitBox;
};
// its capsule: a body (not the lock-on's, a capsule of 4 steps), no damage from the player's attacks, deflecting
constexpr uint32_t kBobberShape = hitGroup(kHitGroupBodies) | kHitNoLockOn | hitCapsule(2);
constexpr uint32_t kBobberAttack = hitImmunity(kHitGroupPlayerAttacks) | kHitAttackHarmless | kHitAttackDeflects;

void bobber(StaticActor& actor) {
    BobberParams params(actor);
    BobberFields bobber(actor);
    actor.prevPos = actor.pos;
    actor.wasAirborne = actor.airborne;
    actor.prevRotation = actor.rotation;
    actor.wasOnPlatform = actor.onPlatform;
    if (BobberState(actor.state) == BobberState::Bind) {
        if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
            freeActor(actor);                         // exe 0x5e1540
            return;
        }
        actor.collisionBox.pointAt(TableId::St4cBobberBox);
        actor.airborne = 1;
        actor.enterState(uint8_t(BobberState::Bob));
        actor.onPlatform = 0;
        actor.lastMover = nullptr;
        actor.rotation.z = 0;
        actor.rotation.x = 0;
        actor.velocity.z = 0;
        actor.velocity.y = 0;
        actor.velocity.x = 0;
        actor.acceleration.z = 0;
        actor.acceleration.y = 0;
        actor.acceleration.x = 0;
        actor.interaction = 0;
        params.restY = actor.pos.y;
    }
    actor.rotation.y = int16_t((uint16_t(actor.rotation.y) + 0x20u) & 0xfff);
    actor.pos.y = int32_t((uint32_t(int32_t(fixedSin(int(uint8_t(gameState().frame)) << 4))) << 8) +
                          uint32_t(params.restY.get()));
    bobber.hitBox.previousX = actor.prevPos.x.whole;
    bobber.hitBox.result = 0;
    bobber.hitBox.previousZ = actor.prevPos.z.whole;
    const int16_t top[3] = {actor.pos.x.whole, int16_t(uint16_t(actor.pos.y.whole) + 0x80u), actor.pos.z.whole};
    const int16_t low[3] = {actor.pos.x.whole, int16_t(uint16_t(actor.pos.y.whole) - 0x80u), actor.pos.z.whole};
    hitListSubmitCapsule(low, 0x10, top, 0x10, bobber.hitBox, hitOwner(actor.slot) | kBobberShape,
                         kBobberAttack); // exe 0x5e4190
}

} // namespace

void bobberUpdate(const PlayerEnv&, uint32_t actor) { bobber(staticActorAt(actor)); }

} // namespace od::port
