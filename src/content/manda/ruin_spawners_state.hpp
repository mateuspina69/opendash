// The class states of ruin_spawners.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::ruin_spawners_state {


// The bytes every spawner clears at its init (each variant names them in its view)
// the spawners' common base (the class block +0x18..+0x1f is each variant's: the state 0 clears it, ControllerActor::
// clearClassBlock)
struct SpawnerFields : ActorClassState {
    explicit SpawnerFields(ControllerActor&) {}
};

struct RespawnFields : SpawnerFields {
    explicit RespawnFields(const ControllerActor& actor) : RespawnFields(const_cast<ControllerActor&>(actor)) {}
    explicit RespawnFields(ControllerActor& actor) : SpawnerFields(actor) {}
    uint8_t respawnsLeft = 0;

    void clearBlock() override { respawnsLeft = 0; }
};

// variant 1's three emitters of walkers: each a timer and the half of its points it spawns at next; the side picked by
// the player's x at the start
struct EmitterFields : SpawnerFields {
    explicit EmitterFields(const ControllerActor& actor) : EmitterFields(const_cast<ControllerActor&>(actor)) {}
    explicit EmitterFields(ControllerActor& actor) : SpawnerFields(actor) {}
    uint8_t side = 0; // 1: the player at x <= 0x300
    StateArray<uint8_t, 3> timers{}; // by emitter
    StateArray<uint8_t, 3> halves{};

    void clearBlock() override {
        side = 0;
        timers = {};
        halves = {};
    }
};

// variant 3 (and ST13's variant 2): actors dropped around the player, one every one or two ticks
struct DropFields : SpawnerFields {
    explicit DropFields(const ControllerActor& actor) : DropFields(const_cast<ControllerActor&>(actor)) {}
    explicit DropFields(ControllerActor& actor) : SpawnerFields(actor), anchorX(actor.recordPoint.x), anchorZ(actor.recordPoint.z) {}
    int16_t& anchorX; // the player's x at the drop's start (its reader is not traced)
    int16_t& anchorZ; // cleared with it
    uint8_t count = 0; // the actors left to drop
    uint8_t delay = 0;

    void clearBlock() override {
        count = 0;
        delay = 0;
    }
};

// variant 0's guardian hoppers: the four slot bytes (slot - 0x1f; 0: none), the point set, this tick's alive mask and
// count, the guardian's enemy-group entry; the guardian's index and the refill delay in the spawn parameters
struct HopperSetFields : SpawnerFields {
    explicit HopperSetFields(const ControllerActor& actor) : HopperSetFields(const_cast<ControllerActor&>(actor)) {}
    explicit HopperSetFields(ControllerActor& actor) : SpawnerFields(actor), guardian(actor.params[0]), delay(actor.params[1]) {}
    FieldAlias<int8_t, uint8_t> guardian; // the guardian's animated pool index (the hoppers get it)
    uint8_t& delay;
    StateArray<uint8_t, 4> slots{};
    uint8_t pointSet = 0;
    uint8_t aliveMask = 0;
    uint8_t aliveCount = 0;
    uint8_t group = 0;

    void clearBlock() override {
        slots = {};
        pointSet = 0;
        aliveMask = 0;
        aliveCount = 0;
        group = 0;
    }
};

// variant 1's drone slots (the entries' lone drones), set at the init
struct DroneSetFields : SpawnerFields {
    explicit DroneSetFields(const ControllerActor& actor) : DroneSetFields(const_cast<ControllerActor&>(actor)) {}
    explicit DroneSetFields(ControllerActor& actor) : SpawnerFields(actor) {}
    StateArray<uint8_t, 3> drones{};

    void clearBlock() override { drones = {}; }
};

struct St14RespawnFields : SpawnerFields {
    explicit St14RespawnFields(const ControllerActor& actor) : St14RespawnFields(const_cast<ControllerActor&>(actor)) {}
    explicit St14RespawnFields(ControllerActor& actor) : SpawnerFields(actor) {}
    uint8_t respawnsLeft = 0;

    void clearBlock() override { respawnsLeft = 0; }
};

} // namespace od::port::ruin_spawners_state
