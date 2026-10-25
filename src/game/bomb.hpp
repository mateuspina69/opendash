// The bomb, class 0x5a7330 (static type 0x22: dropped by the ST15 / ST30 gunships, carried by the ST18 bomber
// Birdbots): it falls and bounces, rests with its fuse lit, can be lifted, carried and thrown, held by its carrier, and
// blows up (a blast, static type 7) when the fuse runs out or a strong hit lands.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"

namespace od::port {

constexpr uint8_t kBombStageType = 0x22;  // its stage static type
constexpr uint8_t kBombVariantHeld = 1;   // spawned held by its carrier (variant 0: loose)
// What a carrier writes into its held bomb: kCarrierLetGo (it falls loose) / kCarrierBlowUp
struct BombCarrierParams {
    explicit BombCarrierParams(const StaticActor& actor) : BombCarrierParams(const_cast<StaticActor&>(actor)) {}
    explicit BombCarrierParams(StaticActor& actor) : carrierCommand(actor.params[0]) {}
    uint8_t& carrierCommand;
};
constexpr uint8_t kCarrierLetGo = 1, kCarrierBlowUp = 2;

// exe 0x5a7330: the bomb's update (ActorUpdateFn)
void bombUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
