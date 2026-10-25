// The parts of the script / stage block that st4b_st4c_rooms.cpp keeps (game/script_state ScriptPart,
// game/stage_state StagePart): their members and the exe offsets they stand for.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/script_state.hpp"
#include "game/state_array.hpp"

namespace od::port::st4b_st4c_rooms_state {

// The script's own bytes: the prop it turns, the camera shake's strength, the end without the DAT load, the prop
// spawned, the face rows' indices
struct Script59Fields : ScriptPart<Script59Fields> {
    ActorRef<StaticActor> prop; // +0x10
    uint32_t shake = 0; // +0x14
    uint8_t loaded = 0; // +0x18
    uint8_t propSpawned = 0; // +0x19
    StateArray<uint8_t, 2> faceRows{}; // +0x1a
    void clearFaceRows() { faceRows = {}; }
};

} // namespace od::port::st4b_st4c_rooms_state
