// The turret's bullet (pool 0x40 class 0x5c3ee0, stage projectile type 4 of ST00, ST0F, ST22, ST2E, ST41 / ST42,
// ST4D..ST4F; the sub-class [0x8d1168 + variant * 4] = 0x5c4250 for variants 0 / 1): held by its owner (game/
// reaverbot_turret) while it charges, then homing on the player 0x100 ticks with a wobble and a trail, then fading;
// a hit (or a wall) ends it in a burst of spark rings in its colour. Its glow billboards (the bullet and its trail)
// are 0x2c draw commands.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

struct PlayerEnv;

// The bullet's parameters (its owner writes them; it holds the bullet by setting the header's sub-state to 1 each tick)
struct TurretBulletParams {
    explicit TurretBulletParams(const ProjectileActor& actor) : TurretBulletParams(const_cast<ProjectileActor&>(actor)) {}
    explicit TurretBulletParams(ProjectileActor& actor) : damage(actor.params[0]), trailShrink(actor.params[1]), trailCounter(actor.params[2]), wobbleShift(actor.params[3]), texture(actor.hitBox.result) {}
    FieldAlias<int8_t, uint8_t> damage;
    FieldAlias<int8_t, uint8_t> trailShrink; // the trail's billboards shrink by this each
    uint8_t& trailCounter; // counts the ticks (the trail's depth ramp)
    uint8_t& wobbleShift; // the homing wobble's speed (a shift of the phase)
    uint32_t& texture; // the billboards' texture word (from the owner's model)
};
constexpr uint8_t kTurretBulletStageType = 4;
enum class TurretBulletState : uint8_t { Start = 0, Held = 1, Homing = 2, Fading = 3 };

// exe 0x5c3ee0: the bullet's update (ActorUpdateFn)
void turretBulletUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
