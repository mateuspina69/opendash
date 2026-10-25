// The class states of late_cast.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::late_cast_state {

// The idle cast's bytes (0x4660d0 / 0x4dadc0): a word, their stepping
struct IdleCastFields : ActorClassState {
    explicit IdleCastFields(AnimatedActor&) {}
    uint32_t word = 0;
    uint8_t stepping = 0;

    void clearBlock() override {
        word = 0;
        stepping = 0;
    }
};

// Its bytes: dimmed (its first parameter byte), then its block: a counter, the waypoint row, its stepping, the
// animation's key held (the key's frame copied every tick)
struct FlyerFields : ActorClassState {
    explicit FlyerFields(AnimatedActor& actor) : dimmed(actor.params[0]) {}
    uint8_t& dimmed;
    uint32_t counter = 0;
    uint32_t row = 0;
    uint8_t stepping = 0;
    int8_t heldKey = 0;

    void clearBlock() override {
        counter = 0;
        row = 0;
        stepping = 0;
        heldKey = 0;
    }
};

// Its bytes: the mouth's mode (the step counter), its flap counter (the last parameter byte), the talk rows' index
struct MouthTalkerFields : ActorClassState {
    explicit MouthTalkerFields(AnimatedActor& actor) : mode(actor.stepCounter), flaps(actor.params[3]) {}
    uint8_t& mode;
    uint8_t& flaps;
    uint8_t row = 0;

    void clearBlock() override {
        row = 0;
    }
};

// The bytes of the ST4B cast: their tracks, a word, the face rows' indices, their stepping
struct St4bCastFields : ActorClassState {
    explicit St4bCastFields(AnimatedActor& actor) : stepping(actor.hopperBytes[0]) {}
    uint8_t& stepping;
    KeyframeTracks tracks;
    uint32_t word = 0;
    StateArray<uint8_t, 2> rows{};
    void clearRows() {
        rows = {};
    }

    void clearBlock() override {
        tracks = {};
        word = 0;
        rows = {};
    }
};

// ---- the scene walker (exe 0x4efe50, ST4B type 0x68) ---------------------------------------------------------------
// Its bytes: its tracks, a word, its stepping
struct SceneWalkerFields : ActorClassState {
    explicit SceneWalkerFields(AnimatedActor& actor) : stepping(actor.shownParts) {}
    uint8_t& stepping;
    KeyframeTracks tracks;
    uint32_t word = 0;

    void clearBlock() override {
        tracks = {};
        word = 0;
    }
};

// The bytes of most of the ST54 cast: their tracks, the rows' indices
struct St54CastFields : ActorClassState {
    explicit St54CastFields(AnimatedActor&) {}
    KeyframeTracks tracks;
    StateArray<uint8_t, 2> rows{};

    void clearBlock() override {
        tracks = {};
        rows = {};
    }
};

// The holding talker: the St54 cast's bytes and its hold byte after them
struct HoldingTalkerFields : St54CastFields {
    explicit HoldingTalkerFields(const AnimatedActor& actor) : HoldingTalkerFields(const_cast<AnimatedActor&>(actor)) {}
    explicit HoldingTalkerFields(AnimatedActor& actor) : St54CastFields(actor) {}
    uint8_t hold = 0;

    void clearBlock() override {
        St54CastFields::clearBlock();
        hold = 0;
    }
};

// Its bytes: its first action, its turn timer (parameter bytes 0 / 2), its tracks, its row index
struct PacerFields : ActorClassState {
    explicit PacerFields(AnimatedActor& actor) : firstAction(actor.params[0]), turnTimer(actor.params[2]) {}
    uint8_t& firstAction;
    uint8_t& turnTimer;
    KeyframeTracks tracks;
    uint8_t row = 0;

    void clearBlock() override {
        tracks = {};
        row = 0;
    }
};

// Its bytes: the circle's angle (its first two parameter bytes), a timer and the rows' index (its last two), the
// countdown of the circle's direction (the velocity's x); its block: the circle's centre, its radius, the animation's
// entry, its tracks (one table)
struct CirclerFields : ActorClassState {
    explicit CirclerFields(AnimatedActor& actor) : angle(actor.params, 0), angleLow(actor.params[0]), angleHigh(actor.params[1]), timer(actor.params[2]), row(actor.params[3]), countdown(actor.velocity.x) {}
    BytesField<uint16_t, 4> angle;
    uint8_t& angleLow;
    uint8_t& angleHigh;
    uint8_t& timer;
    FieldAlias<int8_t, uint8_t> row;
    int16_t& countdown;
    int32_t animEntry = 0;
    uint32_t centreXWord = 0;
    int16_t centreZ = 0;
    int32_t radius = 0;
    KeyframeTracks tracks;
    PartField<int16_t, uint32_t> centreX{centreXWord, 0};

    void clearBlock() override {
        animEntry = 0;
        centreXWord = 0;
        centreZ = 0;
        radius = 0;
        tracks = {};
    }
};

} // namespace od::port::late_cast_state
