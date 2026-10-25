// The class states of st4c_boss.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st4c_boss_state {

// the bytes the shots share: their life and their attack
struct ShotCommon : ActorClassState {
    explicit ShotCommon(ProjectileActor& actor) : spawnBoss(actor.params, 0) {}
    BytesField<uint32_t, 4> spawnBoss;
    int16_t life = 0;
    uint16_t attack = 0;
};

// the orbiting shot: its timer, travel rotation (pitch / yaw / roll), hit box, the boss
struct OrbitShotFields : ShotCommon {
    explicit OrbitShotFields(const ProjectileActor& actor) : OrbitShotFields(const_cast<ProjectileActor&>(actor)) {}
    explicit OrbitShotFields(ProjectileActor& actor) : ShotCommon(actor) {}
    int16_t timer = 0;
    ShortVec4 travel;
    HitBoxHead hitBox;
    ActorRef<AnimatedActor> boss;
};
// the homing shot: its travel rotation, hit box, the boss
struct HomingShotFields : ShotCommon {
    explicit HomingShotFields(const ProjectileActor& actor) : HomingShotFields(const_cast<ProjectileActor&>(actor)) {}
    explicit HomingShotFields(ProjectileActor& actor) : ShotCommon(actor) {}
    ShortVec4 travel;
    HitBoxHead hitBox;
    ActorRef<AnimatedActor> boss;
};

struct BeamShotFields : ActorClassState {
    explicit BeamShotFields(ProjectileActor& actor) : spawnBoss(actor.params, 0) {}
    BytesField<uint32_t, 4> spawnBoss;
    int16_t level = 0;
    uint16_t word = 0;
    HitBoxHead hitBox;
    ActorRef<AnimatedActor> boss;
};

constexpr uint32_t kChargeRays = 0x20;
constexpr int kSparkCount = 0x10;
constexpr int kSparkleSlots = 8;

struct Sparkle {
    int16_t phase = 0;
    int16_t angle = 0;
    int16_t size = 0;
};

struct Spark {
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;
    int16_t angle = 0;
};

// The boss's bytes: its tint bytes, then its class block: its hit box, two timers, its flags, the ground where it
// vanished, its vertical speed, the teleports left in the combo, the thresholds passed, the damage still to apply
struct BossFields : ActorClassState {
    explicit BossFields(AnimatedActor&) {}
    int8_t thresholdsPassed = 0;
    HitBoxHead hitBox;
    int16_t timer = 0;
    int16_t subTimer = 0;
    uint16_t flagsWord_ = 0;
    int16_t savedGround = 0;
    int16_t verticalSpeed = 0;
    int8_t comboLeft = 0;
    uint16_t queuedDamage = 0;
    PartField<uint8_t, uint16_t> flags{flagsWord_, 0};
    void clearFlags() {
        flagsWord_ = 0;
    }

    void clearBlock() override {
        thresholdsPassed = 0;
        hitBox = {};
        timer = 0;
        subTimer = 0;
        flagsWord_ = 0;
        savedGround = 0;
        verticalSpeed = 0;
        comboLeft = 0;
        queuedDamage = 0;
    }
};

struct BoltFields : ShotCommon {
    explicit BoltFields(const ProjectileActor& actor) : BoltFields(const_cast<ProjectileActor&>(actor)) {}
    explicit BoltFields(ProjectileActor& actor) : ShotCommon(actor), timer(actor.hitBox.previousX) {}
    int16_t& timer;
    int16_t rise = 0;
    ShortVec4 travel;
    HitBoxHead hitBox;
    ActorRef<AnimatedActor> boss;
};

struct SparksFields : ActorClassState {
    explicit SparksFields(StaticActor& actor) : dead(actor.rotation.x), live(actor.rotation.y) {}
    int16_t& dead;
    int16_t& live;
    StateArray<Spark, kSparkCount> sparks{};
    StateArray<uint8_t, kSparkCount> sizes{};
    StateArray<uint8_t, kSparkCount> lives{};

    void clearBlock() override {
        lives = {};
    }
};

struct ChargeFields : ActorClassState {
    explicit ChargeFields(StaticActor& actor) : spawnBoss(actor.params, 0), timer(actor.rotation.x), word(actor.rotation.y) {}
    BytesField<uint32_t, 4> spawnBoss;
    int16_t& timer;
    int16_t& word;
    StateArray<uint16_t, kChargeRays> angles{};
    StateArray<uint8_t, kChargeRays> lengths{};
    StateArray<int8_t, kChargeRays> spins{};
    ActorRef<AnimatedActor> boss;
};

struct SparklesFields : ActorClassState {
    explicit SparklesFields(StaticActor& actor) : spawnBoss(actor.params, 0), level(actor.rotation.x), count(actor.rotation.y), next(actor.rotation.z) {}
    BytesField<uint32_t, 4> spawnBoss;
    int16_t& level;
    int16_t& count;
    int16_t& next;
    StateArray<Sparkle, kSparkleSlots> sparkles{};
    ActorRef<AnimatedActor> boss;
};

// the fog controller's bytes: its level, the boss (the exe keeps the boss's address past the slot, in the next
// controller slot's first four bytes; the game relies on that layout)
struct FogFields : ActorClassState {
    explicit FogFields(ControllerActor& actor) : spawnBoss(actor.params, 0), slot_(actor) {}
    BytesField<uint32_t, 4> spawnBoss;
    int16_t level = 0;

    void setBossWord(uint32_t word) { next().setHeadWord(word); }
    AnimatedActor& boss() const { return *viewAt(std::type_identity<AnimatedActor>{}, next().headWord()); }

private:
    ControllerActor& next() const { return controllerPool().following(slot_, "the fog controller's boss"); }
    ControllerActor& slot_;
};

} // namespace od::port::st4c_boss_state
