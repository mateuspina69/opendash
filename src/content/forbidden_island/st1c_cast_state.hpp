// The class states of st1c_cast.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st1c_cast_state {

// The class block of the cast (+0x14c): its two texture tracks, the cursors of its track keys, the key its next
// animation starts at and its held pose
struct CueBlock : ActorClassState {
    explicit CueBlock(AnimatedActor& actor) : holdPose(actor.guardianBits) {}
    uint8_t& holdPose; // non-zero: the pose held (decoded without stepping)
    KeyframeTracks tracks;
    StateArray<uint8_t, 2> keyCursors{};
    uint8_t startKey = 0;
    void clearKeyCursors() {
        keyCursors = {};
    }

    void clearBlock() override {
        tracks = {};
        keyCursors = {};
        startKey = 0;
    }
};

// ---- exe 0x48a910 (type 0x27; states [0x892414]: bind 0x48a980, run 0x48aa20; cues [0x892440]): a prop that rises,
// glides, is thrown up and bobs, sparking --------------------------------------------------------------------------
// its class block: the bob's phase and its base height
struct RisingPropBlock : ActorClassState {
    explicit RisingPropBlock(AnimatedActor&) {}
    uint16_t bobPhase = 0;
    int16_t baseY = 0;

    void clearBlock() override {
        bobPhase = 0;
        baseY = 0;
    }
};

// ---- exe 0x4e3a80 (type 0x5d; states [0x895f18]: bind 0x4e3af0, run 0x4e3bc0; cues [0x895f4c]) ------------------
// Its class block: the two texture tracks, its glow (an effect), the keys' cursors and its start key
struct GlowBlock : ActorClassState {
    explicit GlowBlock(AnimatedActor& actor) : startKey(actor.hopperBytes[0]) {}
    uint8_t& startKey;
    KeyframeTracks tracks;
    ActorRef<EffectActor> glow;
    StateArray<uint8_t, 2> keyCursors{};
    void clearKeyCursors() {
        keyCursors = {};
    }

    void clearBlock() override {
        tracks = {};
        glow = nullptr;
        keyCursors = {};
    }
};

// The ship's class block: its flare (an effect) and its base height
struct ShipFields : ActorClassState {
    explicit ShipFields(StaticActor& actor) : baseY(actor.hitBox.previousX) {}
    int16_t& baseY;
    ActorRef<EffectActor> flare;

    void clearBlock() override {
        flare = nullptr;
    }
};

// The exe's globals the statics publish their class block in (+0xbc)
struct St1cPropLinks {
    uint32_t ship = 0; // 0xa5a900
    uint32_t stillModel = 0; // 0xa5a920
    uint32_t secondStillModel = 0; // 0xa5a924
};
inline St1cPropLinks& propLinks() {
    static St1cPropLinks s;
    return s;
}

// The exe's globals the casts keep their class block's exe address in (each class rewrites its own every tick)
struct St1cCastBlocks {
    uint32_t faceFigure = 0; // 0xa59e28
    uint32_t meshFlipFigure = 0; // 0xa59e2c
    uint32_t cuedFigure = 0; // 0xa59e30
    uint32_t walkingFigure = 0; // 0xa59e34
    uint32_t posingFigure = 0; // 0xa59e38
    uint32_t risingProp = 0; // 0xa59e3c
    uint32_t fidgetingFigure = 0; // 0xa59e40
    uint32_t hoppingFlyer = 0; // 0xa59e48
    uint32_t vanishingWalker = 0; // 0xa59e60
    uint32_t flyer = 0; // 0xa59e64
    uint32_t groundedHopper = 0; // 0xa59e70
    uint32_t fallingFigure = 0; // 0xa59e74
    uint32_t glowingFigure = 0; // 0xa59e78
    uint32_t hopper = 0; // 0xa59e88
    uint32_t idleFigure = 0; // 0xa59e8c
};
inline St1cCastBlocks& castBlocks() {
    static St1cCastBlocks s;
    return s;
}

} // namespace od::port::st1c_cast_state
