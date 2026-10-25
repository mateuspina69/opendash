// The class states of st03_attract_cast.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/cutscene_actor.hpp"
#include "game/field_alias.hpp"

namespace od::port::st03_attract_cast_state {

// ---- the piece carrier (exe 0x47eb30, type 0x1c, variants 0x891378: 0x47ec40 / 0x47f460)
// ---------------------------
// variant 0 walks in, carries two pieces it drops (static type 1 variants 2 / 3) on its animation's frames 0x20 / 0x2b,
// turns on its clock and talks; variant 1 talks. Its face tracks 0x891400 / 0x891570, its shadow. Variant 0 keeps the
// cast's block (game/cutscene_actor CastFields) and, past it, its turn clock and shadow
struct CarrierFields : CastFields {
    explicit CarrierFields(AnimatedActor& actor) : CastFields(actor) {}
    int8_t shadowLevel = 0;
    int16_t turnClock = 0;
    int16_t shadowWidth = 0;
    int16_t shadowLength = 0;

    void clearBlock() override {
        CastFields::clearBlock();
        shadowLevel = 0;
        turnClock = 0;
        shadowWidth = 0;
        shadowLength = 0;
    }
};

// ---- the Flutter (exe 0x49d8c0, type 0x30, variants 0x893944: 0x49d940 bobbing / 0x49d9e0 lifting off)
// -----------
// the bobbing one: y = its base + sin(clock / 4) * 32 >> 12; the lifting one rises from step tick 0x51, its upward speed
// growing by 0x40 a tick until it passes 0x4ff
struct FlutterFields : ActorClassState {
    explicit FlutterFields(AnimatedActor&) {}
    uint16_t bobClock = 0;
    int16_t bobBase = 0;

    void clearBlock() override {
        bobClock = 0;
        bobBase = 0;
    }
};

// the part blinker (exe 0x499310): the cast's block (game/cutscene_actor CastFields), the value its parts blink to, the
// blink sequence's index and the previous one
struct PartBlinkerFields : CastFields {
    explicit PartBlinkerFields(AnimatedActor& actor)
        : CastFields(actor), applyValue(actor.townBirdHitsHigh), index(actor.hopperTimer) {}
    uint8_t& applyValue;
    uint8_t& index;
    uint8_t previousIndex = 0;

    void clearBlock() override {
        CastFields::clearBlock();
        previousIndex = 0;
    }
};

// ---- the craft (exe 0x4baec0, type 0x3f, variants 0x894894: 0x4baf70 / 0x4bb000 / 0x4bb330; 0x8948a0 onwards is the
// next class's) -------------------------------------------------------------------------------------------------------
// variant 0 sinks; variants 1 / 2 fly with a body riding them (spawned at their state 0: the class 0x2f, variants 2 /
// 3), their roll ramping; variant 2 hums a loop sound. Its class block: the hold bit (restart at the held key), the
// start key, the roll's ramp, the loop sound's state, handle and parameter
struct CraftFields : ActorClassState {
    explicit CraftFields(AnimatedActor& actor) : humParam(actor.bossFlags), bodySlot(actor.params[0]) {}
    uint32_t& humParam;
    uint8_t& bodySlot; // the riding body's slot (its spawn byte 8, rewritten)
    uint8_t flags = 0; // kCraftHold
    uint8_t startKey = 0;
    uint8_t heldKey = 0;
    uint8_t rollRamp = 0;
    uint8_t hum = 0; // CraftHum
    uint32_t humHandle = 0;

    void clearBlock() override {
        flags = 0;
        startKey = 0;
        heldKey = 0;
        rollRamp = 0;
        hum = 0;
        humHandle = 0;
    }
};

} // namespace od::port::st03_attract_cast_state
