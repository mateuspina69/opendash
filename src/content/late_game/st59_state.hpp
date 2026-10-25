// The class states of st59.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/stage_state.hpp"
#include "game/state_array.hpp"

namespace od::port::st59_state {

// The class's own bytes (by kind): the countdown's GO; the HUD position; the countdown's slide (16ths) and timer, its
// digit's uv and CLUT; the gauge's value; the controller's best-time rank
struct RaceActorFields : ActorClassState {
    explicit RaceActorFields(ControllerActor& actor) : go(actor.params[0]), x(actor.recordPoint.x), y(actor.recordPoint.y), slide(actor.recordPoint.spare) {}
    uint8_t& go;
    int16_t& x;
    int16_t& y;
    int16_t& slide;
    uint16_t timer = 0;
    int16_t gaugeValue = 0;
    uint16_t clut = 0;
    StateArray<uint8_t, 2> clearedPad0{}; // bytes the block clear covers (no reader)
    PartField<uint8_t, uint16_t> timerLow{timer, 0};
    PartField<uint8_t, int16_t> u{gaugeValue, 0};
    PartField<uint8_t, int16_t> rank{gaugeValue, 0};
    PartField<uint8_t, int16_t> v{gaugeValue, 1};
    void clearScratchBytes() {
        timer = 0;
        gaugeValue = 0;
        clut = 0;
        clearedPad0 = {};
    }
};

// The race block (the stage's block)
struct RaceBlock : StagePart<RaceBlock> {
    uint32_t time = 0; // +0x0
    ActorRef<ControllerActor> countdown; // +0x4
    ActorRef<ControllerActor> gauge; // +0x8
    ActorRef<ControllerActor> watcher; // +0xc
    uint16_t wait = 0; // +0x10
    uint16_t freeze = 0; // +0x12
    int8_t progress = 0; // +0x14
    uint8_t course = 0; // +0x15
    uint16_t tickSoundTimer = 0; // +0x16
    uint8_t jitterRow = 0; // +0x18
    uint8_t jitterColumn = 0; // +0x19
};

// The globals the class sets before its dispatch: the race block, and the class's bytes (actor + 0x18) its routines
// take through three exe globals
struct RaceClassGlobals {
    uint32_t raceBlock = 0;   // the race block's exe address (a value the exe keeps; no reader in the port)
    uint32_t classBytesA = 0; // the class's bytes' exe address (values the exe keeps)
    uint32_t classBytesB = 0;
    uint32_t classBytesC = 0;
};
inline RaceClassGlobals& raceGlobals() {
    static RaceClassGlobals s;
    return s;
}

} // namespace od::port::st59_state
