// The class states of tron_boss.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/loaded_data.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::tron_boss_state {

// The fight's block (in the actor's 4th bone block, +0x20..+0x7f): the hit boxes of the body and the arms, the
// collision box, the move yaw, the arms' hit points, the damage queued, the heavy-hit counter, the counters of the
// dust, the attacks, the bounces, the taunts and the big jump
struct TronFightBlock {
    HitBoxHead body;
    StateArray<uint8_t, 8> unread0{}; // cleared with the block (no reader)
    HitBoxHead leftArm;
    StateArray<uint8_t, 8> unread1{};
    HitBoxHead rightArm;
    StateArray<uint8_t, 8> unread2{};
    BoxExtents box;
    int16_t moveYaw = 0;
    int16_t prevMoveYaw = 0;
    int16_t leftArmHitPoints = 0;
    int16_t rightArmHitPoints = 0;
    int16_t queuedDamage = 0;
    int16_t heavyHits = 0;
    int16_t unread3 = 0;
    int16_t dustCount = 0;
    uint8_t lastAttack = 0;   // TronAttack
    uint8_t bouncesLeft = 0;
    uint16_t tauntsSaid = 0;  // a bit per taunt
    int16_t tauntTicks = 0;
    int16_t bigJumpTicks = 0; // the big jump's cooldown
    StateArray<uint8_t, 0xc> unread4{};

    // the block's bytes cleared one at a time (exe 0x40dc40)
    void clearBytes() { *this = TronFightBlock{}; }
};
// the tank's row of the stat table (the attack table it keeps)
struct TronStats : DataRecord {
    static constexpr uint32_t kBytes = 0xc;
    uint8_t hitPoints = 0; // +0x0
    uint8_t bodyAttack = 0; // +0x1
    uint8_t missileAttack = 0; // +0x2
    uint8_t leftArmHitPoints = 0; // +0x8
    uint8_t rightArmHitPoints = 0; // +0x9
    uint8_t bombAttack = 0; // +0xa
    uint8_t chargeAttack = 0; // +0xb
    template <class R> static TronStats load(const R& r) {
        TronStats v;
        v.hitPoints = r.template read<uint8_t>(0x0);
        v.bodyAttack = r.template read<uint8_t>(0x1);
        v.missileAttack = r.template read<uint8_t>(0x2);
        v.leftArmHitPoints = r.template read<uint8_t>(0x8);
        v.rightArmHitPoints = r.template read<uint8_t>(0x9);
        v.bombAttack = r.template read<uint8_t>(0xa);
        v.chargeAttack = r.template read<uint8_t>(0xb);
        return v;
    }
};

struct TronFields : ActorClassState {
    explicit TronFields(AnimatedActor& actor) : red(actor.params[0]), green(actor.params[1]), blue(actor.params[2]), animKey(actor.params[3]), missileSide(actor.rotation.spare), flags(actor.bossFlags) {}
    FieldAlias<int8_t, uint8_t> red; // the bones' colour (5:5:5)
    FieldAlias<int8_t, uint8_t> green;
    FieldAlias<int8_t, uint8_t> blue;
    FieldAlias<int8_t, uint8_t> animKey; // the key a new animation starts at
    int16_t& missileSide; // the missile's side offset (the rotation's spare lane)
    uint32_t& flags; // kTron*
    uint32_t stats = 0; // its stat row (TronStats)
    uint32_t work = 0; // its fight block's address (in its 4th bone block: the exe's pointer, kept as a value)
    TronFightBlock workBlock; // ... the block's bytes
    int16_t timer = 0;
    StateArray<uint8_t, 2> clearedPad0{}; // bytes the block clear covers (no reader)
    int16_t playerDistance = 0; // to the player's chest
    int16_t playerAngle = 0;
    int16_t playerPitch = 0;
    int16_t jumpSpeed = 0;
    StateArray<uint8_t, 4> clearedPad1{}; // bytes the block clear covers (no reader)
    void clearState() {
        timer = 0;
        clearedPad0 = {};
        playerDistance = 0;
        playerAngle = 0;
        playerPitch = 0;
        jumpSpeed = 0;
        clearedPad1 = {};
        flags = 0;
    }
    TronFightBlock& w() { return workBlock; }
    const TronFightBlock& w() const { return workBlock; }
    TronStats row() const { return dataRecord<TronStats>(stats, TronStats::kBytes); }

    void clearBlock() override {
        stats = 0;
        work = 0;
        timer = 0;
        clearedPad0 = {};
        playerDistance = 0;
        playerAngle = 0;
        playerPitch = 0;
        jumpSpeed = 0;
        clearedPad1 = {};
    }
};

} // namespace od::port::tron_boss_state
