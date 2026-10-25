// The blob boss of ST2F / ST58 (pool 0x20 class 0x4d6b40, stage type 0x4d: ST2F sub 7, ST58 sub 1), see blob_boss.cpp.
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

// exe 0x4d6b40: a body that stretches and squashes itself, a steered prop (static type 0x39, content/saul_kada/
// steered_prop) riding on its top. Its variant picks the form: 0 the small form (it wanders, flees the player), 1 the
// fight (the boss gauge: slams, jumps, lobs blobs, rings of falling shots, a second phase, a spin), 2 / 3 the scripted
// rise / sink. Spawn byte 0 is the tutorial run (messages 0x30..0x38 of bank 0x73c000 in window 4, story flag 0x75 at
// its end).
void blobBossUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
