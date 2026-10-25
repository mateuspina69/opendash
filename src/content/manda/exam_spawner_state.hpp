// The class states of exam_spawner.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"

namespace od::port::exam_spawner_state {

// The spawner's own bytes: +0x18..+0x1f, cleared at its init (their reader is not traced)
struct ExamSpawnerFields : ActorClassState {
    explicit ExamSpawnerFields(ControllerActor&) {}
    StateArray<uint8_t, 8> unread{};
};

} // namespace od::port::exam_spawner_state
