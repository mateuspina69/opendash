// The player's tick: the per-tick reset (exe 0x574830), the pad read and the state machine (the init, the play with
// its sub-states, the death, the scripted state), the ground snap and the animation channels (exe 0x574860 ->
// 0x575200), then the update after the actors moved (exe 0x574d30: the platform, the trigger volumes, the water sink,
// the pull, the push-out, the start of a fall, the pose, the statuses, the hit-list entry); the restore a room exit
// can ask for (exe 0x542430).
#pragma once

namespace od::port {

struct PlayerEnv;

// exe 0x574860: the pad copied into the player (game/player/player_input), then exe 0x575200 (the state handlers, the
// integration, the push-out, the animation)
void playerInputAndDispatch(const PlayerEnv& env);
// exe 0x574d30: the update after the actors' pass
void playerUpdate(const PlayerEnv& env);
// exe 0x574830 (the field mode's tick, before the event runner): the per-tick player state reset -- the status's floor
// forms (kStatusTickResetKeep), the hidden bones, the pull, the floor's depth / slipperiness / water and the burden
// cleared
void playerTickReset();
// exe 0x542430 (a room left with story flag 0x6f1): the player's hit points back to full, its status and the status
// timers cleared
void playerRestore();

} // namespace od::port
