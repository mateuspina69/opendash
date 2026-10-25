// The guardian figures of the ST33 ruin (pool 0x20 type 0x45, exe 0x4ccb70), content/saul_kada/st33_guardian.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port {

// The guardian's parameter: the glows' signal (the stomper or a script sets it, the glower lights its glows on it)
struct GuardianParams {
    explicit GuardianParams(const AnimatedActor& actor) : GuardianParams(const_cast<AnimatedActor&>(actor)) {}
    explicit GuardianParams(AnimatedActor& actor) : signal(actor.params[0]) {}
    uint8_t& signal;
};
// the glower's steps (its header's step; a script moves its figure on by them)
enum class GlowerStep : uint8_t { Wake = 0, Light = 1, Turn = 2, Turn2 = 3, ShakeA = 4, ShakeB = 5, Done = 0xff };
// The guardian's own bytes. The stomper: its flags (kGuardianHoldPose | kGuardianShaking), the shake's timer, its
// animation's first key. The glower (the key script's figure too): its two glows (the identity words of effect slots
// over the same bytes: the animation tail reads the stomper's flags and key all the same), its pose held (decoded, not
// stepped: the figure's tail reads it), a byte, its animation's first key.
struct GuardianFields : ActorClassState {
    explicit GuardianFields(AnimatedActor&) {}
    StateArray<uint32_t, 2> glows{};
    PartField<uint8_t, uint32_t> flags{glows[0], 0};
    PartField<uint8_t, uint32_t> timer{glows[0], 1};
    PartField<uint8_t, uint32_t> animKey{glows[0], 2};
    uint8_t glowerHoldPose = 0;
    uint8_t glowByteB = 0;
    uint8_t glowerAnimKey = 0;

    void clearBlock() override {
        glows = {};
        glowerHoldPose = 0;
        glowByteB = 0;
        glowerAnimKey = 0;
    }
};

// exe 0x4ccb70 (pool 0x20 type 0x45: ST33 sub 0): the ruin's guardian figures (stomp + shake, glowing eyes, animation
// chains)
void guardianUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4cd010 / 0x4cd140: the glower's bind and its step routine (shared by script 0x2f's figure,
// content/saul_kada/st33_key_script)
void bindGlowerGuardian(AnimatedActor& actor);
void glowerGuardianStep(AnimatedActor& actor);

} // namespace od::port
