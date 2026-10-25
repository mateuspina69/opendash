// The ruin blocks: static type 0x1f sub 0 of ST2D / ST2E / ST2F / ST58 (class 0x5a41a0, states [0x8cecdc] 0..3). Its
// siblings: sub 1 0x5a4c60 (content/saul_kada/st41_crumbling_platform), sub 2 0x5a5020 (content/yosyonke/
// ice_ruin_appearing_block).
//
// Its variant:
//   0  a crumbling platform: stood on, its timer drops 2 a tick (0x64 -> 0x32 the cracked model, sound 0x24f; -> 0 it
//      breaks: sound 0x250, 3 falling pieces and hidden); not stood on, it recovers 1 a tick (the whole model again at
//      0x32). It comes back after 0x5a ticks (a fade-in of 0x10 ticks).
//   1  a breakable block (3 x 2 hit capsules along x / z): a hit shatters it (sound 0x24d, 16 pieces); back after 0x96
//      ticks
//   2  a breakable wall (capsules along its face): a hit -> sound 0x254, 2 explosions (static type 8) and 32 pieces in
//      8 x 4 rows; gone for good
//   3  a block of the object-state group (static, type 0x1f, variant 3): it fades out (0x10 ticks, sound 0x261) when
//      the group is marked, back (a fade-in) when it is marked again; it starts hidden
// Its pieces (the debris mode non-zero, the model index of the pieces' models): mode 1 falls to 0x20 above its floor,
// mode 2 falls then bounces, mode 3 bounces at once (exe 0x5a4eb0: half the speed back up, blinking 0xc ticks), then
// freed.
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

// exe 0x5a41a0: the ruin blocks' update (ActorUpdateFn)
void ruinBlocksUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
