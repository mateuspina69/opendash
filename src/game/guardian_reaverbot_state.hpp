// The class state of guardian_reaverbot.cpp's class: the fields the guardian keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"

namespace od::port::guardian_reaverbot_state {

// The guardian's own fields: its line-of-sight probe's byte, the invulnerability ticks it keeps in its rotation's
// spare lane, and its class block (its bits the slot keeps for the next occupant: read map 3.1.3)
struct GuardianFields : ActorClassState {
    explicit GuardianFields(AnimatedActor& actor)
        : sight(actor.params[0]), invulnerable(actor.rotation.spare), bits(actor.guardianBits), slot_(actor) {}
    uint8_t& sight;           // the probe's result (kSight*)
    int16_t& invulnerable;    // ticks without damage after a strong hit
    uint8_t& bits;            // kGuardian*
    HitBox hitBox;            // the body's
    int16_t counter = 0;      // a sub-state's ticks / loops
    uint8_t animKeyStart = 0; // the key the next animation starts at
    int16_t target = 0;       // the turn's target yaw / the attack's sound once
    uint8_t patience = 0;     // the ticks it keeps looking without seeing the player
    int8_t headYaw = 0;       // the head's yaw offset (<< 4 into its bone's angle)
    uint8_t headMode = 0;     // GuardianHead
    uint8_t shakePhase = 0;
    uint8_t cooldown = 0;     // ticks until the next attack
    uint8_t wallBits = 0;     // kGuardianWallHit
    StateArray<uint8_t, 4> glowSlots{}; // the four glow trails' slots (0: none)

    // the head's mode, its shake phase cleared (the exe's word store)
    void setHeadMode(uint8_t mode) {
        headMode = mode;
        shakePhase = 0;
    }
    // the four glow slots as the exe reads / clears them, one dword
    uint32_t glowSlotsWord() const {
        return uint32_t(glowSlots[0]) | uint32_t(glowSlots[1]) << 8 | uint32_t(glowSlots[2]) << 16 |
               uint32_t(glowSlots[3]) << 24;
    }
    void clearGlowSlots() { glowSlots = {}; }
    // the invulnerability's two bytes cleared (the exe's byte stores)
    void clearInvulnerableBytes() { invulnerable = 0; }
    // the class block cleared as the exe's transcription stores it, dword by dword
    void clearBlockWords() { slot_.clearClassBlock(); }

    void clearBlock() override {
        hitBox = {};
        counter = 0;
        animKeyStart = 0;
        target = 0;
        patience = 0;
        headYaw = 0;
        headMode = 0;
        shakePhase = 0;
        cooldown = 0;
        wallBits = 0;
        glowSlots = {};
    }

private:
    AnimatedActor& slot_;
};

} // namespace od::port::guardian_reaverbot_state
