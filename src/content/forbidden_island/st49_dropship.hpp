// The dropship of the landing on the Forbidden Island (ST49, script 0x18: content/forbidden_island/st49_landing): the
// craft Megaman rides down to the island (class exe 0x4f23a0, pool 0x20 type 0x6d; hypothesis from the oracle run of
// save 01 + flag 0x5c0, the model seen in the app), and, on the flag-0x5c1 path, the cargo it lowers (class exe
// 0x4f2ae0, pool 0x20 type 0x6e) on its cable (effect class exe 0x439060, pool 0xa0 stage type 0x17).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4f23a0: spawned by the script's records 0x8a23c4 + c * 0x14 with the variant +0xc = c: 0 parked in the
// Flutter's hold, 1 dropped from the Flutter, 2 falling far below, 3 landed; 4 / 5 the cargo's lowering (the cargo and
// its cable spawned): 4 shakes in place, then sinks with dust and rumble, 5 waits, rises and settles
void st49DropshipUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4f2ae0: hangs 0x600 below its dropship (0x300 and half scale when +0xc != 0)
void st49DropshipCargoUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x439060: a white additive line from the dropship to its cargo
void st49DropshipCableUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
