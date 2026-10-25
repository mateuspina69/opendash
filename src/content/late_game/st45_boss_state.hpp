// The class states of st45_boss.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st45_boss_state {

// The boss's bytes: in its header the set-up of the step it takes (the step after it, its animation, its start key /
// turn side, its distance); the head's pitch (the rotation's spare lane), a sound timer (the spin's x); its class block:
// the arm's hit box and the body's, the arm's hit-box holder (a pool-0xe0 slot), a timer, the head's turn, its status,
// the start key of the animation asked, the anchor point, a counter, the mouth's tick, the blobs out, the hurt
// cooldown
struct BossFields : ActorClassState {
    explicit BossFields(AnimatedActor& actor) : nextStep(actor.params[0]), stepAnim(actor.params[1]), stepKey(actor.params[2]), stepDistance(actor.params[3]), headPitch(actor.rotation.spare), soundTimer(actor.spin.x), status(actor.hopperBytes[3]), startKey(actor.hopperBytes[4]), anchor(actor.hopperBytes[5]), counter(actor.townBirdHits), mouthTick(actor.townBirdHitsHigh), blobs(actor.hopperTimer) {}
    uint8_t& nextStep;
    uint8_t& stepAnim;
    uint8_t& stepKey;
    uint8_t& stepDistance;
    int16_t& headPitch;
    FieldAlias<uint16_t, int16_t> soundTimer;
    uint8_t& status;
    uint8_t& startKey;
    uint8_t& anchor;
    uint8_t& counter;
    uint8_t& mouthTick;
    uint8_t& blobs;
    HitBoxHead armBox;
    HitBoxHead bodyBox;
    ActorRef<ControllerActor> armHolder;
    uint16_t timer = 0;
    int16_t headTurn = 0;
    uint8_t hurtCooldown = 0;

    void clearBlock() override {
        armBox = {};
        bodyBox = {};
        armHolder = nullptr;
        timer = 0;
        headTurn = 0;
        hurtCooldown = 0;
    }
};

// Its bytes: the billboard's frame (its header's last two parameter bytes), then its block: its hit box, the distance
// to the chest, its life, the burst's ticks, the yaw / pitch to the chest, its speed, its wobble's phase, the glow's
// size, the burst's step, the wobble's speed, its alpha, its flags
struct BlobFields : ActorClassState {
    explicit BlobFields(ProjectileActor& actor) : frame(actor.params, 2), hitBox(actor.hitBox), distance(actor.debrisFlags) {}
    BytesField<int16_t, 4> frame;
    HitBoxHead& hitBox;
    uint32_t& distance;
    uint16_t life = 0;
    uint16_t burstTicks = 0;
    int16_t targetYaw = 0;
    int16_t targetPitch = 0;
    uint16_t speed = 0;
    uint16_t wobblePhase = 0;
    int16_t glow = 0;
    uint8_t burstStep = 0;
    int8_t wobbleSpeed = 0;
    uint8_t alpha = 0;
    uint8_t blobFlags = 0;
};

// The shot's block: its hit box, a word, two bytes, the trail's texel offset
struct MouthShotFields : ActorClassState {
    explicit MouthShotFields(ProjectileActor& actor) : hitBox(actor.hitBox) {}
    HitBoxHead& hitBox;
    uint16_t word = 0;
    uint8_t byte = 0;
    uint8_t trailU = 0;
};

constexpr uint32_t kSplashDrops = 8;

// One arm end of the wave: its point and its yaw
struct WaveArm {
    ShortVec3 point;
    int16_t yaw = 0;
};

// One droplet of the splash: its point and its yaw
struct SplashDrop {
    ShortVec3 point;
    int16_t yaw = 0;
};

// The splash's bytes: the droplets, their fall and forward speeds, the size's floor, the size, the texel offset, the
// ticks, its life
struct SplashFields : ActorClassState {
    explicit SplashFields(ProjectileActor&) {}
    StateArray<SplashDrop, kSplashDrops> drops{};
    StateArray<int16_t, kSplashDrops> fall{};
    StateArray<int16_t, kSplashDrops> forward{};
    uint16_t sizeFloor = 0;
    uint8_t size = 0;
    uint8_t texelU = 0;
    int16_t ticks = 0;
    uint16_t life = 0;
};

// The wave's bytes: its hit box, the two arm ends, the centre ray's point and the height the hit test allows, the
// origin, stopped, the reach and its growth, the shrink, the inner / outer ribbon heights, the arms' spread, a word,
// the yaw at the spawn
struct WaveFields : ActorClassState {
    explicit WaveFields(StaticActor& actor) : rayHeight(actor.scale.y), shrink(actor.hitBox.previousX), innerHeight(actor.hitBox.previousZ) {}
    int16_t& rayHeight;
    FieldAlias<uint16_t, int16_t> shrink;
    int16_t& innerHeight;
    HitBoxHead hitBox;
    StateArray<WaveArm, 2> arms{};
    ShortVec3 rayPoint;
    ShortVec3 origin;
    uint16_t stopped = 0;
    int16_t reach = 0;
    uint16_t growth = 0;
    int16_t outerHeight = 0;
    int16_t spread = 0;
    int16_t word = 0;
    int16_t startYaw = 0;

    void clearBlock() override {
        reach = 0;
        growth = 0;
        outerHeight = 0;
        spread = 0;
        word = 0;
        startYaw = 0;
    }
};

// Its block: its hit box, its life, its alpha, its size, its billboard's frame
struct PuffFields : ActorClassState {
    explicit PuffFields(ProjectileActor& actor) : hitBox(actor.hitBox) {}
    HitBoxHead& hitBox;
    StateArray<uint8_t, 4> clearedPad0{}; // bytes the block clear covers (no reader)
    uint16_t life = 0;
    int16_t alpha = 0;
    int16_t size = 0;
    int16_t frame = 0;
    StateArray<uint8_t, 12> clearedPad1{}; // bytes the block clear covers (no reader)
    void clearBlockBytes() {
        clearedPad0 = {};
        life = 0;
        alpha = 0;
        size = 0;
        frame = 0;
        clearedPad1 = {};
        hitBox = {};
    }
};

} // namespace od::port::st45_boss_state
