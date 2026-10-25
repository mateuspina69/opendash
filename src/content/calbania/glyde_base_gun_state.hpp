// The class states of glyde_base_gun.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::glyde_base_gun_state {

struct GunFields : ActorClassState {
    explicit GunFields(AnimatedActor& actor) : arc(actor.params[0]), appoSlot(actor.params[2]), dahSlot(actor.params[3]), barrelPitch(actor.rotation.spare), ignored(actor.guardianBits), unused160(actor.shownParts) {}
    uint8_t& arc; // & kArcMask: its yaw's arc either side (<< 6; 0: it turns freely)
    FieldAlias<int8_t, uint8_t> appoSlot; // the slots of Appo / Dah: its targets besides the player
    FieldAlias<int8_t, uint8_t> dahSlot;
    int16_t& barrelPitch; // the rotation's spare lane
    uint8_t& ignored; // kIgnore*: a downed Appo / Dah given up until it is out of reach
    uint8_t& unused160; // cleared at its init (no reader)
    int8_t fireMode = 0; // FireMode asked this tick
    HitBoxHead hitBox;
    uint16_t timer = 0; // the next lob / the shots' cycle / the dying ticks
    int16_t arcMin = 0; // the yaw's arc
    int16_t arcMax = 0;
    uint8_t downCountdown = 0; // ticks a downed Appo / Dah stays the target (kCountdownFull: reset)
    uint8_t targetKind = 0; // kTarget* of this tick ...
    uint8_t prevTargetKind = 0; // ... and of the last
    int8_t bits = 0; // kGun*
    uint32_t target = 0; // the target's address (the player or a slot's actor)
    int16_t shotPitch = 0; // the shots' pitch toward the target's predicted place

    void clearBlock() override {
        fireMode = 0;
        hitBox = {};
        timer = 0;
        arcMin = 0;
        arcMax = 0;
        downCountdown = 0;
        targetKind = 0;
        prevTargetKind = 0;
        bits = 0;
        target = 0;
        shotPitch = 0;
    }
};

} // namespace od::port::glyde_base_gun_state
