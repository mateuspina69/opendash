// The town's cutscene puppets (the pool-0x20 cast of the town's scripts: exe 0x447aa0 / 0x448520 / 0x4474e0 of the
// story, 0x448e40 / 0x449270 of the side scripts; as content/calbania/calbania_cast 0x446b90): their class bytes, their
// state 0 and their tail -- the ground, the animation, the head turned toward a yaw / height (or relaxed), the pose.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

// the puppet's bytes: its variant index, the script's cue, floating (no ground snap), the head (kept in the spare
// lanes of the velocity / the acceleration), the point it looks at (a height and a yaw) and the turn's rate, its flags,
// the key its animation starts at and whether it looks
struct PuppetFields : ActorClassState {
    explicit PuppetFields(AnimatedActor& actor) : variant(actor.params[0]), go(actor.params[1]), floating(actor.params[3]), headPitch(actor.velocity.spare), headYaw(actor.acceleration.spare) {}
    FieldAlias<int8_t, uint8_t> variant;
    uint8_t& go;
    uint8_t& floating;
    int16_t& headPitch;
    int16_t& headYaw;
    int16_t headHeight = 0;
    int16_t headTarget = 0;
    int16_t headRate = 0;
    uint8_t puppetFlags = 0; // kPuppetResetAnim
    uint8_t animKey = 0;
    uint8_t headOn = 0;

    void clearBlock() override {
        headHeight = 0;
        headTarget = 0;
        headRate = 0;
        puppetFlags = 0;
        animKey = 0;
        headOn = 0;
    }
};
constexpr uint8_t kPuppetResetAnim = 0x1; // the animation set again every tick

// the shared state 0: the model of its variant (none: freed, false), the collision box (exe data), the class block
// cleared, state 1, the animation `anim` asked (none shown yet)
bool puppetBind(AnimatedActor& a, uint32_t box, uint8_t anim);
// the tail: the ground unless floating, the animation (a new one / kPuppetResetAnim: set at the key, else stepped),
// the head (toward the yaw / height by the rate while it looks, else relaxing to 0 by it; at most 0x300 / 0x200) into
// the head part's decoded angles, the pose (the shadow, exe 0x5f20f0, is render output)
void puppetTail(const PlayerEnv& env, AnimatedActor& a);

} // namespace od::port
