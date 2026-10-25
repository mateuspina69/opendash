// The player hurt: the hits of the tick (the result boxes the hit list and the actor collision filled) turned into the
// damage, the knock-back or the knock-down and the floor statuses; the knock-back's slide and its recovery hop, the
// knock-down's throw, bounce and getting up (or the death).
#pragma once

namespace od::port {

struct Player;

// exe 0x57a910: the tick's hits (the solid's and the hit list's result boxes): the damage and the reaction, the
// statuses they carry; without a hit the recent damage decays
void applyHits(Player& p);
// exe 0x572850(player, mode): knocked down: thrown up and back (mode 0: the throw's animation by the hit side, from its
// step 6 after the first; else without the throw's animation and backward speed); the targets dropped, immune
void knockDownThrow(Player& p, int mode);

} // namespace od::port
