// The lava mission's partners (ST27 sub 8, story 13): Tron (pool 0x20 type 0x48 sub 1, content/saul_kada/st27_tron)
// and Bon (type 0x51 sub 1, content/saul_kada/st27_bon) share one layout of their own bytes, which the mission's
// controller (content/saul_kada/st27_mission) links and drives.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port {

// A partner's own bytes: the other partner (linked by the controller), its hit boxes (the body's, the hand's), the
// carry heading, the yaw reflected off a wall, its flags, the key its animation starts at, the target (a pool-0x20
// slot index) and its entry of the enemy groups (whose marks say which Reaverbots are taken)
struct PartnerFields : ActorClassState {
    explicit PartnerFields(AnimatedActor& actor) : flags(actor.shownParts), target(actor.hopperBytes[0]), group(actor.hopperBytes[1]) {}
    uint8_t& flags; // kPartner*
    uint8_t& target;
    uint8_t& group;
    ActorRef<AnimatedActor> partner;
    HitBoxHead bodyBox;
    HitBoxHead handBox;
    int16_t carryYaw = 0;
    int16_t wallYaw = 0;
    uint8_t animKey = 0;

    void clearBlock() override {
        bodyBox = {};
        handBox = {};
        carryYaw = 0;
        wallYaw = 0;
        animKey = 0;
    }
};
constexpr uint8_t kPartnerHoldPose = 0x01;    // the pose held (the current key decoded, not stepped)
constexpr uint8_t kPartnerKnocked = 0x02;     // knocked back / down (no hits taken)
constexpr uint8_t kPartnerReadyTarget = 0x04; // its target is a stunned one (the other partner leaves it)
constexpr uint8_t kPartnerCarrying = 0x08;
constexpr uint8_t kPartnerThrowAtPlayer = 0x10; // out of hit points, or told by the controller: throw at the player
constexpr uint8_t kPartnerLowFloor = 0x20;      // on the low floor (y below -0x200)
constexpr uint8_t kPartnerNoTarget = 0xff;

// the partners' types (pool 0x20, sub-type 1)
constexpr uint8_t kPartnerTronType = 0x48, kPartnerBonType = 0x51;
// the pool-0x20 slots a partner looks for Reaverbots in
constexpr uint32_t kPartnerScanSlots = 0x10;
// the lava they throw the Reaverbots into (16.16)
constexpr int32_t kLavaX = int32_t(0xfc000000u), kLavaY = int32_t(0xff000000u), kLavaZ = int32_t(0xfa000000u);

} // namespace od::port
