// The class states of repair_machine.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::repair_machine_state {

// The class block: the hit result box, the smoke countdown, the damage stage, the hit cooldown, the smoke columns'
// slots and the destroyed countdown
struct MachineFields : ActorClassState {
    explicit MachineFields(AnimatedActor& actor) : hitCooldown(actor.guardianBits), endTicks(actor.hopperBytes[2]) {}
    uint8_t& hitCooldown;
    uint8_t& endTicks;
    HitBox hit;
    int16_t smokeTicks = 0;
    uint8_t damageStage = 0;
    StateArray<uint8_t, 4> smokeSlots{};

    void clearBlock() override {
        hit = {};
        smokeTicks = 0;
        damageStage = 0;
        smokeSlots = {};
    }
};

// The class block: the machine, her flags, the work loops left, the tool shown
struct RollFields : ActorClassState {
    explicit RollFields(AnimatedActor&) {}
    ActorRef<AnimatedActor> machine;
    uint8_t flags = 0; // kRollFree | kRollIgnoresMachine
    uint8_t workLoops = 0;
    uint8_t toolShown = 0;

    void clearBlock() override {
        machine = nullptr;
        flags = 0;
        workLoops = 0;
        toolShown = 0;
    }
};

struct PartFields : ActorClassState {
    explicit PartFields(AnimatedActor&) {}
    int8_t shakeTicks = 0;
    uint32_t rider = 0; // the static riding it (its address; 0: none)

    void clearBlock() override {
        shakeTicks = 0;
        rider = 0;
    }
};

struct PropFields : ActorClassState {
    explicit PropFields(AnimatedActor& actor) : detached(actor.shownParts), frozen(actor.hopperBytes[1]) {}
    uint8_t& detached; // bit 0: not placed on the part
    uint8_t& frozen; // the frame held (decoded, not stepped)
    KeyframeTracks tracks;
    ActorRef<AnimatedActor> part;

    void clearBlock() override {
        tracks = {};
        part = nullptr;
    }
};

} // namespace od::port::repair_machine_state
