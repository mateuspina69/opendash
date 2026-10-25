// The class states of field_spawners.cpp's spawners (pool 0xe0 type 0xa of ST15..ST18): the fields each variant keeps
// in its slot (game/actor_state ActorClassState). The variants of one spawner lay out the slot's class bytes (+0x10 ..
// +0x1f: the record point, the rider word and the last word) each its own way; each variant is its own state. Their
// init clears +0x18 .. +0x1f (exe 0x40dc40), the rider word with it.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::field_spawners_state {

// The record point's eight bytes read one by one (a variant's bytes kept there: x's low / high byte, y's ...)
struct RecordBytes {
    ControllerActor& c;
    PartField<uint8_t, int16_t> operator[](size_t i) const {
        int16_t* const lanes[4] = {&c.recordPoint.x, &c.recordPoint.y, &c.recordPoint.z, &c.recordPoint.spare};
        if (i >= 8) stateIndexError(i, 8);
        return {*lanes[i / 2], unsigned(i % 2)};
    }
};

// ---- ST15 ----
// variant 0: the group's members, its holder and group
struct St15GroupFields : ActorClassState {
    explicit St15GroupFields(ControllerActor& actor) : slot_(actor) {}
    StateArray<uint8_t, 3> members{};
    uint8_t holder = 0;
    uint8_t group = 0;
    void clearTail() {
        members = {};
        holder = 0;
        group = 0;
        slot_.rider = nullptr;
    }

private:
    ControllerActor& slot_;
};
// variants 1 / 2: the gunship wave: the ships' slots by id (the record point's first bytes), its timer (its high byte:
// variant 2's row of targets), its wave, the one following the player (index + 1)
struct St15WaveFields : ActorClassState {
    explicit St15WaveFields(ControllerActor& actor) : shipSlots{actor}, slot_(actor) {}
    RecordBytes shipSlots;
    uint16_t waveTimer = 0;
    PartField<uint8_t, uint16_t> targetRow{waveTimer, 1};
    uint8_t wave = 0;
    uint8_t follower = 0;
    void clearTail() {
        waveTimer = 0;
        wave = 0;
        follower = 0;
        slot_.rider = nullptr;
    }

private:
    ControllerActor& slot_;
};
// variant 3: a timer
struct St15ClockFields : ActorClassState {
    explicit St15ClockFields(ControllerActor& actor) : slot_(actor) {}
    uint32_t timer = 0;
    void clearTail() {
        timer = 0;
        slot_.rider = nullptr;
    }

private:
    ControllerActor& slot_;
};

// ---- ST16 ----
// The carrier spawner's spawn bytes (variant 1): the box spawner's slot, the holder's (variant 0 sets it)
struct St16CarrierParams {
    explicit St16CarrierParams(const ControllerActor& actor) : St16CarrierParams(const_cast<ControllerActor&>(actor)) {}
    explicit St16CarrierParams(ControllerActor& actor) : boxSpawner(actor.params[0]), carrierHolder(actor.params[1]) {}
    uint8_t& boxSpawner;
    FieldAlias<int8_t, uint8_t> carrierHolder;
};
// variant 0: the group's members (two used; the third byte is the holder), its holder and group
struct St16GroupFields : ActorClassState {
    explicit St16GroupFields(ControllerActor& actor) : slot_(actor) {}
    StateArray<uint8_t, 3> members{};
    uint8_t& holder = members[2];
    uint8_t group = 0;
    void clearTail() {
        members = {};
        group = 0;
        slot_.rider = nullptr;
    }

private:
    ControllerActor& slot_;
};
// variant 1: the carriers' point / target indices, their slots by entry, the last carrier spawned (and the spawn bytes)
struct St16CarrierFields : ActorClassState, St16CarrierParams {
    explicit St16CarrierFields(ControllerActor& actor) : St16CarrierParams(actor), slot_(actor) {}
    uint8_t pointIndex = 0;
    uint8_t targetIndex = 0;
    StateArray<uint8_t, 4> carriers{};
    uint8_t lastCarrier = 0;
    void clearTail() {
        pointIndex = 0;
        targetIndex = 0;
        carriers = {};
        lastCarrier = 0;
        slot_.rider = nullptr;
    }

private:
    ControllerActor& slot_;
};
// variant 3: a tick counter
struct St16ClockFields : ActorClassState {
    explicit St16ClockFields(ControllerActor& actor) : slot_(actor) {}
    int32_t ticks = 0;
    void clearTail() {
        ticks = 0;
        slot_.rider = nullptr;
    }

private:
    ControllerActor& slot_;
};

// ---- ST17 ----
// A walker entry of the spawner (4 bytes at +0x10 + 4k): its state, route, step and slot
struct WalkerEntry {
    uint8_t state = 0; // WalkerEntryState
    uint8_t route = 0;
    uint8_t step = 0;
    uint8_t slot = 0;
};
// The walkers' spawn byte (variants 0 / 1): their owner, the boat's slot
struct St17WalkerParams {
    explicit St17WalkerParams(const ControllerActor& actor) : St17WalkerParams(const_cast<ControllerActor&>(actor)) {}
    explicit St17WalkerParams(ControllerActor& actor) : boat(actor.params[0]) {}
    FieldAlias<int8_t, uint8_t> boat;
};
// variants 0 / 1: the walkers' entries (the fourth's first two bytes: the routes switched, the retreat sent)
struct St17WalkerFields : ActorClassState, St17WalkerParams {
    explicit St17WalkerFields(ControllerActor& actor) : St17WalkerParams(actor), slot_(actor) {}
    StateArray<WalkerEntry, 4> entries{};
    uint8_t& routesSwitched = entries[3].state;
    uint8_t& retreatSent = entries[3].route;
    // exe 0x40dc40(+0x18, +0x1c)
    void clearTail() {
        entries[2] = {};
        entries[3] = {};
        slot_.rider = nullptr;
    }
    // exe 0x40dc40(+0x10, +0x1c)
    void clearEntriesAndTail() {
        entries = {};
        slot_.rider = nullptr;
    }

private:
    ControllerActor& slot_;
};
// variant 2's spawn byte: the group
struct St17CellParams {
    explicit St17CellParams(const ControllerActor& actor) : St17CellParams(const_cast<ControllerActor&>(actor)) {}
    explicit St17CellParams(ControllerActor& actor) : group(actor.params[0]) {}
    FieldAlias<int8_t, uint8_t> group;
};
// variant 3: a timer and a countdown
struct St17ClockFields : ActorClassState {
    explicit St17ClockFields(ControllerActor& actor) : slot_(actor) {}
    uint32_t timer = 0;
    uint32_t countdown = 0;
    void clearTail() {
        timer = 0;
        countdown = 0;
        slot_.rider = nullptr;
    }

private:
    ControllerActor& slot_;
};

// ---- ST18 ----
// variant 0: the fight gunships' slots, the Birdbots to keep alive, the calls stopped, the second wave sent
struct St18FightFields : ActorClassState {
    explicit St18FightFields(ControllerActor& actor) : slot_(actor) {}
    StateArray<uint8_t, 2> gunships{};
    int8_t birdbots = 0;
    uint8_t callsStopped = 0;
    uint8_t secondWave = 0;
    void clearTail() {
        gunships = {};
        birdbots = 0;
        callsStopped = 0;
        secondWave = 0;
        slot_.rider = nullptr;
    }

private:
    ControllerActor& slot_;
};
// variant 1: a timer
struct St18TimerFields : ActorClassState {
    explicit St18TimerFields(ControllerActor& actor) : slot_(actor) {}
    uint32_t timer = 0;
    void clearTail() {
        timer = 0;
        slot_.rider = nullptr;
    }

private:
    ControllerActor& slot_;
};
// variant 2: a timer, the next point, the gunship's slot
struct St18ReturnFields : ActorClassState {
    explicit St18ReturnFields(ControllerActor& actor) : slot_(actor) {}
    uint16_t shortTimer = 0;
    uint8_t pointIndex = 0;
    uint8_t gunship = 0;
    void clearTail() {
        shortTimer = 0;
        pointIndex = 0;
        gunship = 0;
        slot_.rider = nullptr;
    }

private:
    ControllerActor& slot_;
};
// variant 3: the machine (pool 0x20; its +0xd: 1 / 2 while it is hit) and the repair gauge
struct St18RepairFields : ActorClassState {
    explicit St18RepairFields(ControllerActor& actor) : slot_(actor) {}
    ActorRef<AnimatedActor> machine;
    int32_t gauge = 0;
    void clearTail() {
        machine = nullptr;
        gauge = 0;
        slot_.rider = nullptr;
    }

private:
    ControllerActor& slot_;
};

} // namespace od::port::field_spawners_state
