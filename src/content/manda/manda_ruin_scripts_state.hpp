// The class states of manda_ruin_scripts.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/script_state.hpp"

namespace od::port::manda_ruin_scripts_state {

// ---- the grounded figure (exe 0x487c80) -------------------------------------------------------------------------------
// its class block: the key its animation starts at (written by its script)
struct GroundedFigureFields : ActorClassState {
    explicit GroundedFigureFields(AnimatedActor&) {}
    uint8_t startKey = 0;

    void clearBlock() override {
        startKey = 0;
    }
};

// ---- the burning wreck (exe 0x491ce0; states [0x892a48]: 0 0x491d00, 1 0x491d90, 2 freed) -------------------------
// its class block: its ground height, its life and its blast timer; its colour step in the header's step byte
struct BurningWreckFields : ActorClassState {
    explicit BurningWreckFields(AnimatedActor&) {}
    int16_t groundY = 0;
    int16_t life = 0;
    int16_t blastTimer = 0;

    void clearBlock() override {
        groundY = 0;
        life = 0;
        blastTimer = 0;
    }
};

// The ruin scripts' own byte: their DAT already loaded by a step (the wrap-up skips its load)
struct RuinScriptBytes : ScriptPart<RuinScriptBytes> {
    uint8_t datLoaded = 0; // +0xc
};

// The script's own bytes past the runner's copies: the player's walk speed (two bytes the steps store one at a time)
// and the angle counter of the platform's sparks
struct TerminalScriptBytes : ScriptPart<TerminalScriptBytes> {
    int16_t walkSpeed = 0; // +0x12
    uint16_t sparkAngle = 0; // +0x14
    void setWalkSpeedBytes(uint8_t low, uint8_t high) {
        walkSpeed = int16_t(uint16_t(low | high << 8));
    }
};

} // namespace od::port::manda_ruin_scripts_state
