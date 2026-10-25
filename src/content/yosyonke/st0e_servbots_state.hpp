// The class states of st0e_servbots.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"

namespace od::port::st0e_servbots_state {

// ==== the crate's Servbot (exe 0x464a30) ==============================================================================
struct CrateServbotFields : ActorClassState {
    explicit CrateServbotFields(AnimatedActor&) {}
    int16_t lieTicks = 0; // jumped out: the ticks it lies off screen before it is freed
    uint16_t voiceDelay = 0;
    uint8_t hits = 0; // kHop*
    uint8_t voiceSaid = 0;
    uint8_t face = 0;

    void clearBlock() override {
        lieTicks = 0;
        voiceDelay = 0;
        hits = 0;
        voiceSaid = 0;
        face = 0;
    }
};

// ==== the riders (exe 0x464dc0) =======================================================================================
struct RiderFields : ActorClassState {
    explicit RiderFields(AnimatedActor& actor) : side(actor.stepCounter), bomb(actor.params, 0), hits(actor.hopperBytes[2]), flags(actor.hopperBytes[3]) {}
    uint8_t& side; // toggled by each walk: the turn back's side
    BytesField<uint32_t, 4> bomb; // the bomb it holds (kNoBomb: none)
    uint8_t& hits; // kHop*
    uint8_t& flags; // kRider*
    HitBoxHead box;
    ShortVec3 local; // its point on the boss vehicle (from the anchor)
    int16_t lieTicks = 0; // knocked off: the ticks it lies off screen
    uint16_t walkTicks = 0;

    void clearBlock() override {
        box = {};
        local = {};
        lieTicks = 0;
        walkTicks = 0;
    }
};

// ==== the flung Servbot (exe 0x492f20) ================================================================================
struct FlungFields : ActorClassState {
    explicit FlungFields(AnimatedActor& actor) : model(actor.params[0]), timer(actor.guardianBits), turnTimer(actor.shownParts), offscreenTicks(actor.hopperBytes[0]), startKey(actor.hopperBytes[1]), hitKind(actor.hopperBytes[4]), smoking(actor.townBirdHits) {}
    uint8_t& model; // the model variant (the turret spawns it 0)
    uint8_t& timer;
    uint8_t& turnTimer;
    uint8_t& offscreenTicks;
    uint8_t& startKey; // the key the next animation starts at
    uint8_t& hitKind; // 1 a hit, 2 a hit from far below
    uint8_t& smoking;
    HitBox box;
    int16_t flightYaw = 0; // the yaw it flies along
    uint8_t behaviour = 0; // kFlung*
    uint8_t contacts = 0; // kContact*

    void clearBlock() override {
        box = {};
        flightYaw = 0;
        behaviour = 0;
        contacts = 0;
    }
};

} // namespace od::port::st0e_servbots_state
