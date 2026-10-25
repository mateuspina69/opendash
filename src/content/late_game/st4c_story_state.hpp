// The class states of st4c_story.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/field_alias.hpp"
#include "game/script_state.hpp"
#include "game/state_array.hpp"

namespace od::port::st4c_story_state {

// the trail's spawn bytes (static stage type 0x5f): its parent's slot, its bone, its life / 16 (then its word), its
// shape
struct TrailParams {
    explicit TrailParams(const StaticActor& actor) : TrailParams(const_cast<StaticActor&>(actor)) {}
    explicit TrailParams(StaticActor& actor) : parentSlot(actor.params[0]), bone(actor.params[1]), lifeSixteenths(actor.params[2]), life(actor.params, 2), shape(actor.params[3]) {}
    FieldAlias<int8_t, uint8_t> parentSlot;
    FieldAlias<int8_t, uint8_t> bone;
    FieldAlias<int8_t, uint8_t> lifeSixteenths;
    BytesField<uint16_t, 4> life;
    FieldAlias<int8_t, uint8_t> shape;
};
// the sprite effects' spawn bytes (pool 0xa0 stage types 0x22 / 0x23): the ticks held after the last frame, two bytes,
// its size, its parent's slot (then its pool index)
struct SpriteParams {
    explicit SpriteParams(const EffectActor& actor) : SpriteParams(const_cast<EffectActor&>(actor)) {}
    explicit SpriteParams(EffectActor& actor) : holdTicks(actor.params[0]), paramB(actor.params[1]), size(actor.params[2]), paramD(actor.params[3]), parent(actor.posSpare) {}
    uint8_t& holdTicks;
    uint8_t& paramB;
    FieldAlias<int8_t, uint8_t> size;
    uint8_t& paramD;
    int16_t& parent;
};
// the ray burst's spawn words (static stage type 0x63): the rays' angle bases
struct RayBurstParams {
    explicit RayBurstParams(const StaticActor& actor) : RayBurstParams(const_cast<StaticActor&>(actor)) {}
    explicit RayBurstParams(StaticActor& actor) : angleBase(actor.params, 0), yawBase(actor.params, 2) {}
    BytesField<uint16_t, 4> angleBase;
    BytesField<uint16_t, 4> yawBase;
};
// the motes' spawn bytes (static stage type 0x65): their phase (the ring's radius), speed, mask and life
struct MoteParams {
    explicit MoteParams(const StaticActor& actor) : MoteParams(const_cast<StaticActor&>(actor)) {}
    explicit MoteParams(StaticActor& actor) : phase(actor.params[0]), speed(actor.params[1]), mask(actor.params[2]), life(actor.params[3]) {}
    FieldAlias<int8_t, uint8_t> phase;
    FieldAlias<int8_t, uint8_t> speed;
    uint8_t& mask;
    uint8_t& life;
};

// The defeated boss's own bytes: its face tracks, its fade counter, its cast flags and key (after the counter), the
// pillars' counter, the bursts' and the trails' cue indices, its animation playing
struct DefeatedBossFields : ActorClassState {
    explicit DefeatedBossFields(AnimatedActor& actor) : animKey(actor.guardianBits), playing(actor.hopperBytes[5]) {}
    uint8_t& animKey;
    uint8_t& playing;
    KeyframeTracks faces;
    int16_t fadeCounter = 0;
    uint8_t castFlags = 0;
    uint16_t pillars = 0;
    int16_t burstCue = 0;
    int16_t trailCue = 0;

    void clearBlock() override {
        faces = {};
        fadeCounter = 0;
        castFlags = 0;
        pillars = 0;
        burstCue = 0;
        trailCue = 0;
    }
};

constexpr uint32_t kTrailPoints = 0x10;

struct Mote {
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;
    int16_t speed = 0;
};

struct BoneTrailFields : ActorClassState, TrailParams {
    explicit BoneTrailFields(StaticActor& actor) : TrailParams(actor), length(actor.rotation.x), head(actor.rotation.y), headLow(actor.rotation.y, 0), rise(actor.rotation.z), shapeAngle(actor.rotation.spare), width(actor.velocity.x) {}
    int16_t& length;
    int16_t& head;
    PartField<uint8_t, int16_t> headLow;
    int16_t& rise;
    FieldAlias<uint16_t, int16_t> shapeAngle;
    FieldAlias<uint16_t, int16_t> width;
    uint32_t drawMode = 0;
    uint32_t colourStep = 0;
    StateArray<int16_t, kTrailPoints> xs{};
    StateArray<int16_t, kTrailPoints> ys{};
    StateArray<int16_t, kTrailPoints> zs{};

    void clearBlock() override {
    }
};

struct RayBurstFields : ActorClassState, RayBurstParams {
    explicit RayBurstFields(StaticActor& actor) : RayBurstParams(actor) {}
    StateArray<uint8_t, 0x20> xAngles{};
    StateArray<uint8_t, 0x20> yAngles{};
    StateArray<uint8_t, 0x20> speeds{};

    void clearBlock() override {
        for (size_t i = 20; i < 32; ++i) speeds[i] = {};
    }
};

struct MotesFields : ActorClassState, MoteParams {
    explicit MotesFields(StaticActor& actor) : MoteParams(actor), count(actor.scaleBytes[0]) {}
    FieldAlias<int8_t, uint8_t> count;
    StateArray<Mote, 0x10> motes{};
    StateArray<int8_t, 0x10> sizes{};

    void clearBlock() override {
    }
};

// ---- the sprite effects (pool 0xa0 stage types 0x22 / 0x23, exe 0x43fc70 / 0x43fe30): a billboard (4 / 6 frames of
// 0x20 texels, its u offset), its hold ticks after the last frame, then freed (exe 0x5e1580)
struct SpriteFields : ActorClassState, SpriteParams {
    explicit SpriteFields(EffectActor& actor) : SpriteParams(actor), u(actor.trail[0]), rotation(actor.trail[1]) {}
    int16_t& u;
    FieldAlias<uint16_t, int16_t> rotation;
    uint8_t stillFrames = 0;
    uint8_t stillTicks = 0;
    uint8_t frames = 0;
    uint8_t frameTicks = 0;
    StateArray<uint8_t, 10> clearedPad0{}; // bytes the block clear covers (no reader)
    void clearBlockBytes() {
        stillFrames = 0;
        stillTicks = 0;
        frames = 0;
        frameTicks = 0;
        clearedPad0 = {};
        u = 0;
        rotation = 0;
    }
};

// The scripts' own bytes: the intro's saved RNG, two bytes they clear, the rumble's level
struct St4cScriptFields : ScriptPart<St4cScriptFields> {
    uint32_t savedRng = 0; // +0xc
    uint8_t clearedA = 0; // +0x10
    uint8_t clearedB = 0; // +0x12
    int16_t rumble = 0; // +0x14
};

} // namespace od::port::st4c_story_state
