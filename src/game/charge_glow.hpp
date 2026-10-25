// The charge glow (pool 0xa0 class 0x42ef70, stage effect type 4 of ST00, ST0F, ST22, ST2E, ST41 / ST42, ST4D..ST4F):
// the reaverbot turret's charge (game/reaverbot_turret) -- three spark rings and a sound at its start, then it grows,
// holds while its owner keeps it (+0xc set each tick) and shrinks; while growing / held it is a hit entry. Its bolts and
// disc are OT packets (render output), but their draw steps the RNG per projected segment (game state).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port {

struct PlayerEnv;

// The glow's parameters (its owner writes them): kept alive this tick, the damage of its hit entry, its hit box
struct ChargeGlowParams : ActorClassState {
    explicit ChargeGlowParams(EffectActor& actor) : held(actor.params[0]), damage(actor.posSpare) {}
    uint8_t& held;
    int16_t& damage;
    HitBoxHead hitBox;
    void clearHitBoxWords() {
        hitBox = {};
    }
};
constexpr uint8_t kChargeGlowStageType = 4;

// exe 0x42ef70: the glow's update (ActorUpdateFn)
void chargeGlowUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
