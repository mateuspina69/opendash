// The arrival on Kattelox after the crash landing: script 0x55 (ST08: the Flutter comes down outside), script 0x4c
// (ST04, inside the Flutter: three messages, then ST08 free play) -- content/yosyonke/arrival.cpp -- and the classes of the
// Flutter's cabin (ST04) -- content/yosyonke/flutter_cabin.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"

namespace od::port {

struct PlayerEnv;

// scripts 0x55 (exe handler 0x532ec0) / 0x4c (exe handler 0x5302d0): whether `handler` is one / its tick
bool arrivalScriptPorted(RoutineId handler);
bool arrivalScriptTick(const PlayerEnv& env, RoutineId handler);

// The Flutter's cabin (ST04)
// exe 0x4cd290 (pool 0x20 type 0x47): the animation of its spawn bytes
void cabinAnimatedUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x58dba0 (static type 1): a placed model
void cabinModelUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4e7290 (pool 0x20 type 0x60): an NPC turning its head
void cabinNpcUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4e6dc0 (ST04 sub 0 pool 0x20 type 0x60): Roll in the Flutter -- talking to her opens the travel map
void flutterRollUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
