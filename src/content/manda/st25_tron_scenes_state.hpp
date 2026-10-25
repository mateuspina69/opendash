// The class states of st25_tron_scenes.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/field_alias.hpp"
#include "game/script_state.hpp"
#include "game/state_array.hpp"

namespace od::port::st25_tron_scenes_state {

constexpr uint8_t kFigureHoldPose = 0x01, kFigureShadow = 0x02;
constexpr uint8_t kForceAnim = 0x01, kTankShadow = 0x02;

// ---- the tank landing (exe 0x471520; states [0x890364]: bind 0x4715d0, run 0x471660) -----------------------------
// Its class block: the key its animation starts at, its flags (kForceAnim: the animation set even when unchanged;
// kTankShadow: a shadow, render output) and the dust's ticks
struct TankLandingFields : ActorClassState {
    explicit TankLandingFields(AnimatedActor&) {}
    uint8_t startKey = 0;
    uint8_t flags = 0;
    int32_t dustTicks = 0;

    void clearBlock() override {
        startKey = 0;
        flags = 0;
        dustTicks = 0;
    }
};

// ---- the wreck (exe 0x471990; states: init 0x471a50, run 0x471b80) --------------------------------------------------
// Its class block: its flags (kForceAnim / a shadow, as the tank's), the key its animation starts at, a base height
// and the marks of its 12 exploding parts (by part, 1 .. 0xd: the blast's ticks)
struct WreckFields : ActorClassState {
    explicit WreckFields(AnimatedActor&) {}
    uint8_t flags = 0;
    uint8_t startKey = 0;
    int16_t baseY = 0;
    StateArray<uint8_t, 0xd> partMarks{}; // [k] for parts 1..0xc

    void clearBlock() override {
        flags = 0;
        startKey = 0;
        baseY = 0;
        partMarks = {};
    }
};

// ---- script 0x1c's figure (exe 0x45bce0; states [0x88f488]: bind 0x45bda0, run 0x45be30) ----------------------------
// Its class block: its texture tracks, its flags (kFigureHoldPose / kFigureShadow), its start key, its hover above the
// ground, its shadow's size and the spark counter
struct FigureFields : ActorClassState {
    explicit FigureFields(AnimatedActor&) {}
    KeyframeTracks tracks;
    uint8_t flags = 0;
    uint8_t startKey = 0;
    int16_t hover = 0;
    int16_t shadowSize = 0;
    int16_t sparkCounter = 0;

    void clearBlock() override {
        tracks = {};
        flags = 0;
        startKey = 0;
        hover = 0;
        shadowSize = 0;
        sparkCounter = 0;
    }
};

// The scripts' own bytes: the copies of the current step's tick, id and sub the runner makes for the cast, the talking
// pose the cast reads, the player's walk-in speed and (script 0x1c) the tank's arms broken off
struct PokteScriptBytes : ScriptPart<PokteScriptBytes> {
    uint8_t talkPose = 0; // +0x12: the pose of the talking cast (0, 2..5)
    int16_t walkSpeed = 0; // +0x14
    uint8_t brokenArms = 0; // +0x16: bit 0: the tank's left arm, bit 1: its right arm
};

} // namespace od::port::st25_tron_scenes_state
