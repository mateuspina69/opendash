// The story system: the dispatch of the running story script (a script block id != 0: its handler [0x89b6d0 + id * 4],
// which game/script_engine's runner ticks) to the game's own script handlers (the stage callbacks: the shops, Roll's
// development room; the travel map) and to the content's script modules (game/content_registry), and the room-script
// stage cases of the content (the inline stage cases of exe 0x512890). The scripts themselves are code, not bytecode:
// every id has its own handler dispatching the block's state through its own table.
#pragma once

#include <cstdint>

#include "engine/data_source.hpp"

namespace od::port {

struct PlayerEnv;

inline constexpr const char* kStoryPort = "story port"; // the area of the story ports' port-gap errors

// the handler of script `id` (the exe table 0x89b6d0: a named routine; an id past the table: none)
RoutineRef storyScriptHandler(uint8_t id);
// exe 0x5128b0 for an id != 0: the script's handler. false = the handler is not ported (the caller throws the port
// gap with the id).
bool storyScriptTick(const PlayerEnv& env, uint8_t id);
// whether storyScriptTick knows the handler of script `id`
bool storyScriptPorted(uint8_t id);

// exe 0x512890's inline stage cases (not table handlers): the content's (the attract, the opening, the chapters).
// false when the stage has no such case.
bool storyRoomCase();

} // namespace od::port
