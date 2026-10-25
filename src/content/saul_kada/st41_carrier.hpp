// The ST41 pod carrier (pool 0x20 type 0x7c, exe 0x506d10) and the bytes its pods (static type 0x4f, exe 0x5b6be0:
// content/saul_kada/st41_carrier_pod) share with it. See content/saul_kada/st41_carrier.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/state_array.hpp"

namespace od::port {

// The carrier's own bytes: the dying blink's ticks, its hit box, the hit entry off, its bob phase and rest height, its
// pods (a bit per pod: the live ones, those that have signalled, those back home) and its two lights
struct PodCarrierFields : ActorClassState {
    explicit PodCarrierFields(AnimatedActor& actor) : pods(actor.shownParts), podsBack(actor.hopperBytes[0]) {}
    uint8_t& pods;
    uint8_t& podsBack;
    uint16_t blinkTicks = 0;
    StateArray<uint8_t, 2> clearedPad0{}; // bytes the block clear covers (no reader)
    HitBoxHead hitBox;
    uint8_t hitOff = 0;
    uint8_t bobPhase = 0;
    uint16_t restY = 0;
    uint8_t podsSignalled = 0;
    StateArray<uint8_t, 1> clearedPad1{}; // bytes the block clear covers (no reader)
    StateArray<uint32_t, 2> lights{}; // the lights' effect slots
    void clearBytes() {
        blinkTicks = 0;
        clearedPad0 = {};
        hitBox = {};
        hitOff = 0;
        bobPhase = 0;
        restY = 0;
        podsSignalled = 0;
        clearedPad1 = {};
        lights = {};
        pods = 0;
        podsBack = 0;
    }

    void clearBlock() override {
        blinkTicks = 0;
        clearedPad0 = {};
        hitBox = {};
        hitOff = 0;
        bobPhase = 0;
        restY = 0;
        podsSignalled = 0;
        clearedPad1 = {};
        lights = {};
    }
};
constexpr uint8_t kCarrierPods = 4;
constexpr uint8_t kAllPods = 0xf;

constexpr uint8_t kCarrierPodType = 0x4f;

// exe 0x506d10 (pool 0x20 type 0x7c: ST41): the pod carrier (bobs, drops up to 4 pods when the player is near)
void podCarrierUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
