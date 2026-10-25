// The ST1A movers, class 0x590100 (static pool type 1 of the ST1A table; ST1A is one of Nino's stages, ST15..ST1B):
// the moving solids near the ceiling of the ST1A restaurant (the family of ST18's still 0x58ffa0, content/nino/
// st18_props), moved here from game/actors. States [0x8cdcc4 + state * 4]: 0 = 0x590120 (bind the model, scenery, the
// motion block cleared, speed 8), 1 = 0x590170 (variant 0 turns 0x10 a tick; variant 1 slides along its facing between
// z +-0x110 and bobs 8 * sin(16 * k) on y), 2 = 0x52ef00 (ret). A stand-on solid (type 1) either way.
#include "content/nino/st1a_movers.hpp"

#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/port_gap.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

namespace {

constexpr const char* kSt1aPort = "ST1A movers"; // the area of its port-gap errors

// a mover's own field: the bob's phase (the velocity's spare lane)
// a mover's own field: the bob's phase (the velocity's spare lane)
struct MoverFields {
    explicit MoverFields(const StaticActor& actor) : MoverFields(const_cast<StaticActor&>(actor)) {}
    explicit MoverFields(StaticActor& actor) : bobPhase(actor.velocity.spare) {}
    int16_t& bobPhase; // 0 .. 0xff; the bob is the sine of its low byte * 16
};

enum class MoverState : uint8_t {
    Init = 0, // exe 0x590120
    Move = 1, // exe 0x590170
    Stop = 2, // exe 0x52ef00 (ret)
};
enum class MoverVariant : uint8_t {
    Turning = 0,
    Sliding = 1,
};
constexpr int16_t kSlideSpeed = 8, kTurnSpeed = 0x10, kSlideLimit = 0x110, kBobPeriod = 0x100;

// exe 0x590100's body: the state by variant (turning / sliding and bobbing), a solid
void st1aMover(StaticActor& actor) {
    MoverFields mover(actor);
    switch (MoverState(actor.state)) {
    case MoverState::Init:
        if (!bindActorModel(actor, actor.variant)) { // exe 0x5e1540: freed
            freeActor(actor);
            return;
        }
        actor.flags |= kActorScenery;
        actor.clearMotion(); // exe 0x40dc40(+0x38, +0x68)
        actor.velocity.x = kSlideSpeed;
        actor.tint = kTintNeutral;
        actor.enterState(uint8_t(MoverState::Move));
        return;
    case MoverState::Move: {
        actor.tint = kTintNeutral;
        actor.savePreviousPose();
        const uint8_t v = actor.variant;
        if (v == uint8_t(MoverVariant::Turning)) {
            actor.rotation.y = int16_t(uint16_t(actor.rotation.y + kTurnSpeed));
            if (actor.rotation.y >= kHalfTurn) actor.rotation.y = int16_t(uint16_t(actor.rotation.y - 0x1000));
        } else if (v == uint8_t(MoverVariant::Sliding)) {
            if (actor.velocity.x > 0) {
                if (actor.pos.z.whole > kSlideLimit) actor.velocity.x = int16_t(-kSlideSpeed);
            } else if (actor.pos.z.whole < -kSlideLimit) {
                actor.velocity.x = kSlideSpeed;
            }
            integrateVelocity(actor, actor.velocity.x, 0, 0); // exe 0x5e3690
            ++mover.bobPhase;
            if (mover.bobPhase >= kBobPeriod) mover.bobPhase = 0;
            actor.pos.y += uint32_t(int32_t(fixedSin(int(uint8_t(mover.bobPhase)) * 0x10))) << 3;
        }
        actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0);
        return;
    }
    case MoverState::Stop: return;
    default: notPortedAt(kSt1aPort, "state", actor.state);
    }
}

} // namespace

void st1aMoverUpdate(const PlayerEnv&, uint32_t address) { st1aMover(staticActorAt(address)); }

} // namespace od::port
