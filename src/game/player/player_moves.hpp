// What the input starts: the tests the sub-states' input handlers make to enter another sub-state (exe 0x579f20 ..
// 0x57a4b0, 0x57a8b0) -- the run in each direction, the walk, the side step, the turn in place, the quick turn, the
// jumps, the side hop, the jet skates, the look. Each returns 1 when it switched the sub-state.
#pragma once

#include <cstdint>

namespace od::port {

struct Player;

// how a run starts (exe 0x579fa0 .. 0x57a150's mode): fresh (its start animation) or turned from another move
constexpr int kRunFresh = 0;
constexpr int kRunTurned = 1;

// exe 0x579fa0 / 0x57a020 / 0x57a0b0 / 0x57a150(player, mode): the run forward / left / right / back (its direction
// held, the walk modifier not; a turned start needs the current sub-state past its first steps)
int tryRunForward(Player& p, int mode);
int tryRunLeft(Player& p, int mode);
int tryRunRight(Player& p, int mode);
int tryRunBack(Player& p, int mode);
// exe 0x579f20 / 0x579f60: the walk forward / backward (the walk modifier with forward / back)
int tryWalkForward(Player& p);
int tryWalkBack(Player& p);
// exe 0x57a390: the side step (the walk modifier with left / right)
int trySideStep(Player& p);
// exe 0x57a280: the turn in place (the turn stick out, nothing locked on)
int tryTurnInPlace(Player& p);
// exe 0x57a2b0(player, directions): the quick turn (use pressed with a direction of the mask held; from idle only away
// from the facing)
int tryQuickTurn(Player& p, uint16_t directions);
// exe 0x57a3e0: the jump (pressed, or buffered)
int tryJump(Player& p);
// exe 0x57a420: the running jump once the run is past its step 3, else the jump buffered
int tryRunningJump(Player& p);
// exe 0x57a4b0: the side hop on landing with left / right (the turn stick's side)
int trySideHop(Player& p);
// exe 0x57a1e0: the jet skates (the walk modifier held 9 ticks with the skates on the ground / the water skates in
// water); the hold counted, else the skating cleared
int trySkates(Player& p);
// exe 0x57a8b0: the look mode (the look button without the lock-on, no aim recentre)
int tryLook(Player& p);

} // namespace od::port
