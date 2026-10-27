// OpenDash's gameplay changes to the exe (on by default):
//   - the lock-on: a locked target without a lock-on point keeps the lock a moment (game/lock_on_targets
//     selectLockOnTargets) and a knock-back keeps it on its target (game/player/player_update updateLockOn);
//   - moving fire: moving never cancels the buster's shot (game/player/moving_fire).
// Off: the exe's gameplay, which the actor goldens hold (test_actors).
#pragma once

namespace od::port {

bool modernPlay();
void setModernPlay(bool on);

} // namespace od::port
