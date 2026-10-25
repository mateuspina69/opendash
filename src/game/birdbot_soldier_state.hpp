// The class states of birdbot_soldier.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/birdbot.hpp"
#include "game/field_alias.hpp"

namespace od::port::birdbot_soldier_state {

constexpr uint8_t kSoldierRangeMask = 0x0f, kSoldierGuards = 0x40, kSoldierAlone = 0x80;

// The soldier's own fields: its spawn parameters, the target point it keeps in its velocity's / acceleration's spare
// lanes, the boat's yaw offset in its rotation's spare lane, and its class block
struct SoldierFields : BirdbotBody {
    explicit SoldierFields(const AnimatedActor& actor) : SoldierFields(const_cast<AnimatedActor&>(actor)) {}
    explicit SoldierFields(AnimatedActor& actor) : BirdbotBody(actor), range(actor.params[0]), callBit(actor.params[1]), boatSlot(actor.params[2]), trackId(actor.params[3]), boatYaw(actor.rotation.spare), targetX(actor.velocity.spare), targetZ(actor.acceleration.spare), timer(actor.guardianBits), parts(actor.shownParts), animKeyStart(actor.hopperBytes[1]), hitKind(actor.hopperBytes[2]), remainsVariant(actor.hopperBytes[3]), hurt(actor.townBirdHits), chase(actor.townBirdHitsHigh), sideOffset(actor.hopperTimer) {}
    uint8_t& range; // & 0xf the range class (kSoldierRanges); kSoldierGuards / 0x80 options
    uint8_t& callBit; // its group's call bit (0x100 << it)
    FieldAlias<int8_t, uint8_t> boatSlot; // the ST17 boat's static slot
    FieldAlias<int8_t, uint8_t> trackId; // the encounter id the remains take over (0: none)
    int16_t& boatYaw; // its yaw on the boat's gangway
    FieldAlias<uint16_t, int16_t> targetX; // the remembered target (its integer parts)
    FieldAlias<uint16_t, int16_t> targetZ;
    uint8_t& timer;
    uint8_t& parts; // the body part set shown (0..3)
    int8_t group = 0; // its enemy-group entry (exe 0x5e6cd0)
    uint8_t& animKeyStart;
    uint8_t& hitKind; // the last hit: 1 strong, 2 a push
    uint8_t& remainsVariant;
    uint8_t& hurt; // the hurts summed (a knock past 0x38)
    uint8_t& chase; // ticks it chases the player instead of its target
    uint8_t& sideOffset; // the aim's offset while not chasing
    HitBox hitBox; // its contact: where the hit came from
    uint16_t neighbour = 0; // game/enemy_neighbours
    int16_t groundHeight = 0;

    void clearBlock() override {
        group = 0;
        BirdbotBody::clearBlock();
        hitBox = {};
        neighbour = 0;
        groundHeight = 0;
    }
};

} // namespace od::port::birdbot_soldier_state
