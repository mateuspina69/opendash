// The attract intro's effect classes (ST02 / ST03; content/intro/attract_effects.cpp): the lightning over the airship
// (static type 0x15), the ring / flash the scripts' timed tables spawn (controller type 0xe) and the animated VRAM tile
// of ST03 (controller type 5). The scripts set the ring / flash's radius through the fields below. (The scrolling sky
// layer, effect type 9, is a class of two areas: game/blizzard_layer.)
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"

namespace od::port {

struct PlayerEnv;

// The ring / flash's bytes (exe 0x4236a0): its radius (the timed tables' parameter), its centre (the spawn's point),
// the radius's step and the phase's ticks, the ring's wobble seed
struct RingFlashFields : ActorClassState {
    explicit RingFlashFields(ControllerActor& actor) : radius(actor.params, 0), centre(actor.recordPoint) {}
    BytesField<uint16_t, 4> radius;
    ShortVec4& centre; // the spawn's point (the slot's record point: the spawner sets it, the screen flash moves it)
    uint16_t radiusStep = 0;
    uint8_t phaseTicks = 0;
    uint8_t wobbleSeed = 0;
};
constexpr uint8_t kRingFlashType = 0xe; // the controller's stage class type in ST02 / ST03
// +6: the variants
enum class RingFlashVariant : uint8_t { Ring = 0, ScreenFlash = 1, Flash = 2 };

// exe 0x59aea0 (ST02 static type 0x15): the lightning emitters and their bolts
void attractLightningUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4236a0 (ST02 / ST03 controller type 0xe): a ring growing and shrinking, or a flash
void attractRingFlashUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x418550 (ST03 controller type 5): an animated VRAM tile (the screens' and the water's frames)
void attractVramTileUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
