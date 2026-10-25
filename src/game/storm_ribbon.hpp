// The storm ribbon (effect class exe 0x431f50: pool 0xa0 stage type 0xb of ST10 / ST11, the stormy Forbidden Island;
// hypothesis: the island's whirlwind): a tall vertical ribbon of colour bands wobbling sideways as it runs away in z,
// drawn as gouraud quads (draw commands). Its start (exe 0x431f70) and draw (exe 0x431fb0) are also variants 7 / 8 of
// the Nino ruin's effect 0x431e90 (content/nino/ruin_effects: the exe's variant table runs into this class's state
// table). No spawner of it is known (no record, no code writes pool 0xa0 type 0xb in ST10 / ST11).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

// The ribbon's fields (its effect slot): the bands' count from the spawn parameters, the colour phases and the three
// wobble accumulators (the draw reads each accumulator's high byte as a 64-step sine angle)
struct StormRibbonFields : ActorClassState {
    explicit StormRibbonFields(EffectActor& actor) : steadyBands(actor.params[0]), colourCountdown(actor.params[1]) {}
    FieldAlias<int8_t, uint8_t> steadyBands; // the bands between the 16 fading in and the 16 fading out
    uint8_t& colourCountdown; // ticks to the next colour-phase step (3 per step)
    uint16_t wobbleSlow = 0; // the wobble accumulators (high byte: the phase of each sine)
    uint16_t wobbleMid = 0;
    uint16_t wobbleFast = 0;
    uint8_t colourPhaseLow = 0; // the colour phases: the low (blue) byte's sine ...
    uint8_t colourPhaseMid = 0; // ... the middle (green) byte's ...
    uint8_t colourPhaseHigh = 0; // ... and the high (red) byte's
};

// exe 0x431f70: the ribbon's start (state 1, the late loop, the slow wobble at a random phase)
void stormRibbonStart(EffectActor& ribbon);
// exe 0x431fb0: one tick of the ribbon's colours and wobble, and its bands as draw commands
void stormRibbonDraw(EffectActor& ribbon);
// exe 0x431f50 (pool 0xa0 stage type 0xb)
void stormRibbonUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
