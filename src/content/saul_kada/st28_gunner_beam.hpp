// The fire-ruin gunner boss's beam (ST28 pool 0x40 type 0x19, exe 0x5c9770), see content/saul_kada/st28_gunner_beam.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"

namespace od::port {

constexpr uint8_t kGunnerBeamType = 0x19;
enum class GunnerBeamVariant : uint8_t { Ray = 0, Shot = 1 };
// the beam's signal to the boss: the ray found the player / the shot hit a wall; the shot caught the player
enum class GunnerBeamSignal : uint8_t { None = 0, Fire = 1, Caught = 2 };
// The beam's fields the boss reads and writes: its signal, its boss
struct GunnerBeamFields {
    explicit GunnerBeamFields(const ProjectileActor& actor) : GunnerBeamFields(const_cast<ProjectileActor&>(actor)) {}
    explicit GunnerBeamFields(ProjectileActor& actor) : signal(actor.params[0]), boss(actor.hitBox.result) {}
    uint8_t& signal;
    uint32_t& boss;
};

// exe 0x5c9770: variant 0 the aiming ray (its dots; the player in it -> fire), 1 the homing shot (caught: its effect
// around the player)
void gunnerBeamUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
