// Glyde's ship arriving (ST30 sub 2 static type 0x1b variant 1 during script 0x25, exe 0x5a1dc0; hypothesis), one
// routine per exe routine. States [0x8ceb38]: 0 0x5a1e10 (the model by its variant, the box 0x8ceb20, 16 debris
// statics: type 3 variant 0x10 / 0x11 at the ground - 10, a random side yaw, 0x500 out, a drift from the CRT rand), 1
// 0x5a1f70 (sub-states [0x8ceb48]: 0 0x5a1f90 rise 0x2d0 (its parameter set: already up at y -0x1b0), 1 0x5a1fe0
// decelerate by 0x16 a tick, 2 0x5a2030 bob), 2 0x52ef00 (nothing), 3 0x5afdd0 (freed).
#include "content/nino/glyde_ship_arrival.hpp"

#include "engine/data_source.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/gunship_flight.hpp"

#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"

namespace od::port {

namespace {

// [0x8ceb38]
enum class ArrivalState : uint8_t { Init = 0, Rise = 1, Idle = 2, Gone = 3 };
// [0x8ceb48]
enum class ArrivalSub : uint8_t { Start = 0, Decelerate = 1, Bob = 2 };

// The debris' spawn parameters (the stage's static type 3): its drift and its life
struct DebrisParams {
    explicit DebrisParams(const StaticActor& actor) : DebrisParams(const_cast<StaticActor&>(actor)) {}
    explicit DebrisParams(StaticActor& actor) : driftX(actor.params[0]), driftZ(actor.params[1]), life(actor.params, 2) {}
    uint8_t& driftX;
    uint8_t& driftZ;
    BytesField<uint16_t, 4> life;
};

const DataTable<CollisionBoxRecord> arrivalBox{TableId::GlydeShipArrivalBox};
constexpr uint8_t kDebrisType = 3, kDebrisVariant = 0x10, kDebrisSmallVariant = 0x11;
constexpr int kDebrisCount = 16;
constexpr int16_t kRiseSpeed = 0x2d0, kDeceleration = 0x16;
constexpr int32_t kUpHeight = int32_t(0xfe500000u);

// exe 0x5a1e10: the model, the box, airborne, the class block cleared; 16 debris around it at the ground (their
// coordinates doubled, as the exe stores them)
void arrivalInit(const PlayerEnv& env, StaticActor& actor) {
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0(a, &+6)
        freeActor(actor);
        return;
    }
    actor.collisionBox.pointAt(arrivalBox.stateAddress());
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 1;
    actor.onPlatform = 0;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.velocity.setXY(0, 0);
    actor.velocity.z = 0;
    actor.clearClassBlock(); // exe 0x40dc40
    actor.enterState(uint8_t(ArrivalState::Rise));
    for (int k = 0; k < kDebrisCount; ++k) {
        const uint32_t d = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (!d) continue;
        StaticActor& debris = staticActorAt(d);
        auto&& params = DebrisParams(debris);
        debris.flags = kActorLive;
        debris.loopFlags |= kActorSweepGroup2;
        debris.type = kDebrisType;
        debris.variant = kDebrisVariant;
        debris.pos = actor.pos;
        debris.pos.y.whole = int16_t(actorGround(env, actor) - 10); // exe 0x40f190(a)
        uint32_t yaw = (crtRand() & 0x3ff) + 0x200 + uint32_t(int32_t(actor.rotation.y));
        if (crtRand() & 1) yaw ^= 0x800;
        debris.rotation.y = int16_t(yaw);
        integrateVelocity(debris, 0, 0, -0x500);
        int r;
        if ((crtRand() & 1) == 0) {
            r = int(crtRand() & 0x3f) + 0x40;
        } else {
            debris.variant = kDebrisSmallVariant;
            r = int(crtRand() & 0x1f) + 0x30;
        }
        int16_t dx, dz;
        angleVector(int(int16_t(uint16_t(yaw))), r, dx, dz); // exe 0x5e40c0
        params.driftZ = uint8_t(uint32_t(uint16_t(dz) >> 2) & 0xffu);
        params.driftX = uint8_t(uint32_t(uint16_t(dx) >> 2) & 0xffu);
        debris.pos.x <<= 1;
        params.life = 0;
        debris.pos.y <<= 1;
        debris.pos.z <<= 1;
    }
}

// exe 0x5a1f70: the rise -- started (already up when told), decelerated to a stop, then bobbing
void arrivalRise(StaticActor& actor) {
    switch (ArrivalSub(actor.subState)) {
    case ArrivalSub::Start: { // exe 0x5a1f90
        const uint8_t s = actor.subState;
        actor.subState = uint8_t(s + 1);
        actor.velocity.x = 0;
        actor.velocity.y = kRiseSpeed;
        actor.velocity.z = 0;
        if (actor.params[0] != 0) {
            actor.pos.y = kUpHeight;
            actor.subState = uint8_t(s + 2);
            actor.velocity.x = 0;
            actor.velocity.y = 0;
            actor.velocity.z = 0;
            actor.acceleration.y = -6;
        }
        return;
    }
    case ArrivalSub::Decelerate: // exe 0x5a1fe0
        actor.velocity.y = int16_t(actor.velocity.y - kDeceleration);
        integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z);
        if (actor.velocity.y < 0) {
            actor.velocity.y = 0;
            actor.acceleration.y = -6;
            actor.subState += 1u;
        }
        return;
    case ArrivalSub::Bob: // exe 0x5a2030
        bobVertical(actor); // exe 0x5a0670
        integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z);
        return;
    default: notPortedAt(kStoryPort, "static 0x5a1dc0 sub-state", actor.subState);
    }
}

// exe 0x5a1dc0
void arrival(const PlayerEnv& env, StaticActor& actor) {
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (ArrivalState(actor.state)) {
    case ArrivalState::Init: arrivalInit(env, actor); return;
    case ArrivalState::Rise: arrivalRise(actor); return;
    case ArrivalState::Idle: return;                      // exe 0x52ef00
    case ArrivalState::Gone: freeActor(actor); return;    // exe 0x5afdd0
    default: notPortedAt(kStoryPort, "static 0x5a1dc0 state", actor.state);
    }
}

} // namespace

void glydeShipArrivalUpdate(const PlayerEnv& env, uint32_t actor) { arrival(env, staticActorAt(actor)); }

} // namespace od::port
