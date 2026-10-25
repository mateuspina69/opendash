// The lock-on candidates (exe 0x5e4b60, run by the hit pass before its pair tests, game/hit_list): the hit-list entries
// of the lock-on groups, projected with the game camera, pick the player's target slot (the nearest one on screen,
// else the nearest one in reach), the screen-centre slot and their aim points, mark the closest candidate's and the
// locked actor's boxes, and tell the HUD whether an enemy is near. The player's lock-on itself (holding the lock,
// turning to the target) is game/player/player_lock_on.
#pragma once

#include <cstdint>

namespace od::port {

// exe 0x5e4b60(count, groupMask): the selection over the first `count` entries of the hit list
void selectLockOnTargets(uint32_t count, uint32_t groupMask);
// OpenDash (modern aim): the actor in `slot` is no longer a target (a fire put out, still smoking): a lock on it is
// dropped now, as the exe's selection drops it the tick it sends no point, so the hold for a target without a point
// (selectLockOnTargets) does not keep it aimed at the actor's position and the lock goes to the next one
void releaseLockOnTarget(uint8_t slot);

} // namespace od::port
