// The solid ring of the pirate base's gate rooms (ST41 / ST42 static type 0x31, exe 0x5acaa0; hypothesis names).
// States [0x8cf63c]: 0 start 0x5acae0 (its model; the exe stays here until it binds; piece 0 spawns pieces 1..3; each
// piece turned variant * 0x400 and pushed out 0x1c00; additive), 1 a solid 0x5acba0, 2 freed 0x5afdd0.
#include "content/saul_kada/st41_ring.hpp"

#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

namespace {

enum class RingState : uint8_t { Start = 0, Solid = 1, Free = 2 };
constexpr uint8_t kRingType = 0x31;
constexpr uint8_t kRingPieces = 4;
constexpr int kRingRadius = 0x1c00;

// exe 0x5acae0
void start(StaticActor& actor) {
    if (!bindActorModel(actor, 0)) return; // exe 0x5e0ba0
    actor.collisionBox.pointAt(TableId::St41RingBox);
    actor.clearClassBlock();
    actor.enterState(uint8_t(RingState::Solid));
    if (actor.variant == 0)
        for (uint8_t v = 1; v < kRingPieces; ++v) {
            const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
            if (!slot) continue;
            StaticActor& piece = staticActorAt(slot);
            piece.flags = kActorLiveDrawn;
            piece.type = kRingType;
            piece.variant = v;
            piece.pos = actor.pos;
            piece.rotation = actor.rotation;
        }
    actor.rotation.x = 0;
    actor.rotation.y = int16_t(uint16_t(uint32_t(actor.variant) << 10));
    actor.rotation.z = 0;
    integrateVelocity(actor, 0, 0, -kRingRadius);
    actor.semiTransparency = semiTransparencyBits(1, 1); // exe 0x5e1a70(actor, 1, 1)
}
void solidRing(StaticActor& actor) { // exe 0x5acaa0
    actor.savePreviousPose();
    switch (RingState(actor.state)) {
    case RingState::Start: start(actor); return;
    case RingState::Solid: actorCollisionSubmitSolid(actor, SolidType::StandOn, 0, 0); return; // exe 0x414050
    case RingState::Free: freeActor(actor); return;                                           // 0x5afdd0 -> 0x5e1540
    default: notPortedAt(kStoryPort, "0x5acaa0 state (table 0x8cf63c)", actor.state);
    }
}

} // namespace

void solidRingUpdate(const PlayerEnv&, uint32_t actor) { solidRing(staticActorAt(actor)); }

} // namespace od::port
