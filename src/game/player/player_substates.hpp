// The player's sub-state handlers (the exe's tables A 0x8c4fc0: the input, B 0x8c4f6c: the motion and the animation),
// each defined in the file of its subject: game/player/player_locomotion (idle, walk, run, turn in place, quick turn,
// side step, look), player_jumps (the jumps, the landing, the side hop), player_skates (the jet skates, the skate fall),
// player_hang (the bar, the ledge), player_damage (the knock-back, the knock-down), player_carry (the lift, being held),
// player_interaction (talking, the event poses). The dispatch (game/player/player_update) calls them by the handler
// address the exe's tables hold.
#pragma once

#include <cstdint>

namespace od::port {

struct Player;
struct PlayerEnv;

// ---- player_locomotion
void idleInput(Player& p);        // exe 0x579110 (A 0)
void idleMotion(Player& p);       // exe 0x575a70 (B 0)
void walkInput(Player& p);        // exe 0x579310 (A 1)
void walkMotion(Player& p);       // exe 0x575bc0 (B 1)
void runInput(Player& p);         // exe 0x5794c0 (A 2)
void runMotion(Player& p);        // exe 0x575d70 (B 2)
void turnInPlaceInput(Player& p); // exe 0x579ab0 (A 4)
void turnInPlaceMotion(Player& p); // exe 0x576750 (B 4)
void quickTurnMotion(Player& p);  // exe 0x576840 (B 5)
void sideStepInput(Player& p);    // exe 0x579c70 (A 6)
void sideStepMotion(Player& p);   // exe 0x576990 (B 6)
void lookInput(Player& p);        // exe 0x579e80 (A 13)
void lookMotion(Player& p);       // exe 0x577f30 (B 13)
// ---- player_jumps
void jumpInput(const PlayerEnv& env, Player& p); // exe 0x579db0 (A 7 / A 8)
void jumpMotion(Player& p);                      // exe 0x576ad0 (B 7)
void runningJumpMotion(Player& p);               // exe 0x576d20 (B 8)
void sideHopMotion(Player& p);                   // exe 0x577810 (B 10)
// ---- player_skates
void skateInput(Player& p);       // exe 0x5799f0 (A 3)
void skateMotion(Player& p);      // exe 0x576240 (B 3)
void skateFallMotion(Player& p);  // exe 0x577640 (B 9)
// ---- player_hang
void barHangInput(Player& p);     // exe 0x579e40 (A 11)
void barHangMotion(Player& p);    // exe 0x577a60 (B 11)
void ledgeHangInput(Player& p);   // exe 0x579e60 (A 12)
void ledgeHangMotion(const PlayerEnv& env, Player& p); // exe 0x577c70 (B 12)
// exe 0x57a630 (A 9 and the jumps' input): the ledge grab while falling next to a wall; 1 = grabbed
int tryLedgeGrab(const PlayerEnv& env, Player& p);
// ---- player_damage
void knockBackInput(Player& p);   // exe 0x579ee0 (A 14)
void knockBackMotion(Player& p);  // exe 0x5786d0 (B 14)
void knockDownMotion(Player& p);  // exe 0x578810 (B 15)
// ---- player_carry
void liftMotion(Player& p);       // exe 0x578b10 (B 16)
void heldInput(Player& p);        // exe 0x579f00 (A 17)
void heldMotion(Player& p);       // exe 0x578c60 (B 17)
// ---- player_interaction
void talkMotion(Player& p);       // exe 0x578d40 (B 18)
void eventPoseMotion(Player& p);  // exe 0x578e90 (B 19)
void itemGetMotion(Player& p);    // exe 0x578fd0 (B 20)

} // namespace od::port
