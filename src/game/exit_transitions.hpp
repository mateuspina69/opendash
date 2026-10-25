// The exits' transitions: script 0 (exe 0x5128d0), which the room scripts start when an exit or a door actor armed a
// transition (story flag 0x700). The stage's kind handler (game/stage_state transitionHandlers, by the request's kind)
// plays the kind's states: the walk-out (the player placed and driven on the replay pad, the door leaves, the
// transition camera, a step sequence), the handshake with the field mode (request 2 -> 3) and, once the room switched,
// the end (and for the passages the walk-in in the room entered). The kinds, states and steps dispatch on the named
// routines (engine/routines.csv).
#pragma once

namespace od::port {

struct PlayerEnv;

// exe 0x5128d0: one tick of script 0 (the armed transition's kind handler)
void exitTransitionTick(const PlayerEnv& env);

} // namespace od::port
