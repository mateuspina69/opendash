// The class states of st15_cls.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "content/nino/st15_cls.hpp"
#include "game/field_alias.hpp"

namespace od::port::st15_cls_state {

// The member's fields: its parameters (st15_cls.hpp); part 1's tilt (the rotation's spare lane: the decoded frame's y),
// the yaw it flies along / its box's yaw offset (the velocity's spare lane), running (the acceleration's spare lane);
// its class block: the hit box (contact: x the hit's direction, y its strength), the target, the node and the previous
// one, its behaviour bits, the sub-state after the walk, the point (a point's index, the box's slot, a tick), its contact
// bits, the animation's start key, a timer, the spray's slot, the down knock done, the off-screen ticks, the stagger
// The member's fields: its parameters (st15_cls.hpp); part 1's tilt (the rotation's spare lane: the decoded frame's y),
// the yaw it flies along / its box's yaw offset (the velocity's spare lane), running (the acceleration's spare lane);
// its class block: the hit box (contact: x the hit's direction, y its strength), the target, the node and the previous
// one, its behaviour bits, the sub-state after the walk, the point (a point's index, the box's slot, a tick), its contact
// bits, the animation's start key, a timer, the spray's slot, the down knock done, the off-screen ticks, the stagger
struct MemberFields : ActorClassState, GroupMemberParams {
    explicit MemberFields(AnimatedActor& actor)
        : GroupMemberParams(actor), tiltY(actor.rotation.spare), flightYaw(actor.velocity.spare),
          boxYaw(actor.velocity.spare), running(actor.acceleration.spare), behaviour(actor.hopperBytes[0]),
          afterWalk(actor.hopperBytes[1]), point(actor.hopperBytes[2]), contact(actor.hopperBytes[3]),
          animKeyStart(actor.hopperBytes[4]), timer(actor.hopperBytes[5]), spraySlot(actor.townBirdHits),
          downKnocked(actor.townBirdHitsHigh), offScreenTicks(actor.hopperTimer) {}
    int16_t& tiltY;
    int16_t& flightYaw;
    int16_t& boxYaw;
    FieldAlias<uint16_t, int16_t> running;
    HitBox hitBox;
    int16_t targetX = 0;
    int16_t targetZ = 0;
    int8_t node = 0;
    uint8_t prevNode = 0;
    uint8_t& behaviour; // kMember*
    uint8_t& afterWalk;
    uint8_t& point;
    uint8_t& contact;   // kMemberHit*
    uint8_t& animKeyStart;
    uint8_t& timer;
    uint8_t& spraySlot;
    uint8_t& downKnocked;
    uint8_t& offScreenTicks;
    uint8_t stagger = 0;

    void clearBlock() override {
        hitBox = {};
        targetX = 0;
        targetZ = 0;
        node = 0;
        prevNode = 0;
        stagger = 0;
    }
};

} // namespace od::port::st15_cls_state
