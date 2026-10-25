// The class states of st3c_gold_boss.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st3c_gold_boss_state {

// The boss's bytes: its defeat's tick count, its status (its parameter bytes 1 / 2: taunting, dying), the statue part's
// signals (byte 3), the speed it steps to (the velocity's spare lane), its block's address
// Its block (in its bone block 4, +0x20): its 4 hit boxes, the player's planar distance, its flags (their low byte
// tested alone), a timer (its low byte), the phase two timer, the two thrown pieces it holds (their slots' identity
// words), the angles, the hit mode, the script's cursor and tick, rising, the height offset, the ring (0..3, kNoRing)
// and the radius pick, the target, the step sound's timer, the ring's moves
struct BossBlock {
    StateArray<HitBoxHead, 4> boxes{};
    uint32_t distance = 0;
    uint32_t flags = 0;
    PartField<uint8_t, uint32_t> flagsLow{flags, 0};
    uint16_t timer = 0;
    PartField<uint8_t, uint16_t> timerLow{timer, 0};
    uint16_t phaseTwoTimer = 0;
    StateArray<uint32_t, 2> pieces{};
    uint16_t playerAngle = 0;
    uint16_t targetAngle = 0;
    uint8_t hitMode = 0;
    uint8_t scriptCursor = 0;
    uint8_t scriptTick = 0;
    uint8_t rising = 0;
    int16_t heightOffset = 0;
    uint8_t ring = 0;
    uint8_t radiusPick = 0;
    int16_t targetX = 0;
    int16_t targetZ = 0;
    uint16_t unread0 = 0; // cleared with the block (no reader)
    uint16_t stepSoundTimer = 0;
    uint8_t ringMoves = 0;
    StateArray<uint8_t, 0x17> unread1{};

    void clearBytes() {
        boxes = {};
        distance = 0;
        flags = 0;
        timer = 0;
        phaseTwoTimer = 0;
        pieces = {};
        playerAngle = 0;
        targetAngle = 0;
        hitMode = 0;
        scriptCursor = 0;
        scriptTick = 0;
        rising = 0;
        heightOffset = 0;
        ring = 0;
        radiusPick = 0;
        targetX = 0;
        targetZ = 0;
        unread0 = 0;
        stepSoundTimer = 0;
        ringMoves = 0;
        unread1 = {};
    }
};

struct BossFields : ActorClassState {
    explicit BossFields(AnimatedActor& actor) : defeatTicks(actor.params[1]), status(actor.params[2]), statueSignals(actor.params[3]), speedTarget(actor.velocity.spare) {}
    uint8_t& defeatTicks;
    uint8_t& status;
    uint8_t& statueSignals;
    int16_t& speedTarget;
    BoneBlockArea<BossBlock> block;

    void clearBlock() override {
        block = nullptr;
    }
};

// The exe globals st3c_gold_boss.cpp keeps (their exe addresses in the comments)
struct St3cGoldBossGlobals {
    uint32_t bossBlock = 0; // 0xa59e90
};
inline St3cGoldBossGlobals& st3cGoldBossGlobals() {
    static St3cGoldBossGlobals s;
    return s;
}

} // namespace od::port::st3c_gold_boss_state
