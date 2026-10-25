// The class states of travel.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/script_state.hpp"
#include "game/transition_state.hpp"

namespace od::port::travel_state {

// ---- the map icons (ST01 static type 0x33, exe 0x5ad1c0; variants [0x8cf660]) --------------------------------------
// The icons' class fields (one class, both kinds): +0xf the kind (0 the shadow, 1 the ship), +0xb the flight's phase,
// +0xbc the altitude; the shadow's heading it turns toward (the script sets it) and the ship's slot of the shadow it
// follows, both in the spawn parameters' first bytes
struct TravelIconFields : ActorClassState {
    explicit TravelIconFields(const StaticActor& actor) : TravelIconFields(const_cast<StaticActor&>(actor)) {}
    explicit TravelIconFields(StaticActor& actor)
        : flightPhase(actor.stepCounter), startsFlying(actor.params[2]), kind(actor.params[3]), heading(actor.params, 0),
          parentSlot(actor.params[0]) {}
    uint8_t& flightPhase; // TravelFlight
    uint8_t& startsFlying; // the shadow starts in the air
    uint8_t& kind;
    BytesField<uint16_t, 4> heading; // the shadow's
    uint8_t& parentSlot;             // the ship's
    uint16_t altitude = 0;

    void clearBlock() override {
        altitude = 0;
    }
};

// The script's bytes in the script block (game/script_state's block; from +0x34 in the actor records it does not use:
// it spawns two)
struct TravelMap : ScriptPart<TravelMap> {
    uint8_t flags = 0; // +0xc: kTravel*
    uint8_t destination = 0; // +0xd: the destination under the icon (its index in the map's list)
    uint8_t destStage = 0; // +0xe: its request: the stage, the room, the place
    uint8_t destRoom = 0; // +0xf
    RoomPlace destPlace; // +0x10
    int16_t cameraX = 0; // +0x18: the map camera's target
    int16_t cameraZ = 0; // +0x1a
    uint8_t hintShown = 0; // +0x34: the hint's timers: shown once, its ticks, the idle ticks before again
    uint8_t hintTicks = 0; // +0x35
    uint8_t idleTicks = 0; // +0x36
    uint8_t picked = 0; // +0x37
    uint8_t map = 0; // +0x38: the map of the story
    uint8_t storyMarker = 0; // +0x39: the story's marker on the map (its reader is the map's draw)
    uint8_t island = 0; // +0x3a: the island the Flutter starts at (0..6)
    uint8_t ready = 0; // +0x3b: a pick is taken (after the hint's first ticks)
    uint8_t arrivalFade = 0; // +0x3c: the fade in at the destination (kNoFade: none)
    int16_t iconX = 0; // +0x4c: the Flutter icon on the map and its previous place
    int16_t iconZ = 0; // +0x4e
    int16_t prevIconX = 0; // +0x50
    int16_t prevIconZ = 0; // +0x52
    int16_t leadX = 0; // +0x54: the camera's lead toward the icon
    int16_t leadZ = 0; // +0x56
    int16_t secondX = 0; // +0x5c: the second icon (kTravelSecondIcon)
    int16_t secondZ = 0; // +0x5e
    int16_t prevSecondX = 0; // +0x60
    int16_t prevSecondZ = 0; // +0x62
    int16_t secondLeadX = 0; // +0x64
    int16_t secondLeadZ = 0; // +0x66
    // exe 0x40dc40 over +0xc..+0x1b: the members there zeroed (past it: kept)
    void clearSetUpRun() override {
        flags = {};
        destination = {};
        destStage = {};
        destRoom = {};
        destPlace = {};
        cameraX = {};
        cameraZ = {};
    }
    // the hint's bytes and the pick cleared (the exe's dword store at +0x34)
    void clearHints() {
        hintShown = 0;
        hintTicks = 0;
        idleTicks = 0;
        picked = 0;
    }
};

} // namespace od::port::travel_state
