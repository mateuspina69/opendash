// The class states of escape_timer.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::escape_timer_state {

// the timer's fields
struct EscapeTimerFields : ActorClassState {
    explicit EscapeTimerFields(ControllerActor&) {}
    const uint32_t* timer = nullptr; // +0xc (the spawner's parameter word): the escape's ticks
    uint8_t frameCountdown = 0; // the stopwatch's next frame in ticks
    uint8_t stopwatchFrame = 0; // the stopwatch's frame (0..2)
    // the timer's address is the exe's parameter word (the base's ticks: a field of the game state)
    HeadPointer headPointer() const override { return HeadPointer::toField(kHeadParamsWord, timer); }
    void clearHeadPointer() override { timer = nullptr; }
};

} // namespace od::port::escape_timer_state
