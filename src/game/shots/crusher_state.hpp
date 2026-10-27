// The class states of crusher.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::crusher_state {

// ---- the crusher ------------------------------------------------------------------------------------------------------
// Its block +0xbc: the hold's ticks, the throw's yaw, the orb
struct CrusherFields : ActorClassState {
    explicit CrusherFields(ShotActor&) {}
    uint16_t holdTicks = 0;
    uint16_t throwYaw = 0;
    ActorRef<StaticActor> orb; // the energy orb
};

constexpr uint32_t kOrbRings = 3;

// The orb's fields: burst (set by the shot that carries it), its hit box, the attack / level of the weapon in use, its
// ticks left and its life, the sound's ticks, its full size and its size, the three rings' angles
struct EnergyOrbFields : ActorClassState {
    explicit EnergyOrbFields(StaticActor& actor) : burst(actor.params[0]), attack(actor.rootMatrix.e[8]), level(actor.rootPad), ticks(actor.rootTranslation[0], 0), life(actor.rootTranslation[0], 2), soundTicks(actor.rootTranslation[1], 0), fullSize(actor.rootTranslation[2], 0), size(actor.rootTranslation[2], 2), paramsWord_(actor.params, 0) {}
    uint8_t& burst;
    FieldAlias<uint16_t, int16_t> attack;
    int16_t& level;
    PartField<int16_t, int32_t> ticks;
    PartField<int16_t, int32_t> life;
    PartField<int16_t, int32_t> soundTicks;
    PartField<int16_t, int32_t> fullSize;
    PartField<int16_t, int32_t> size;
    BytesField<uint32_t, 4> paramsWord_;
    HitBox hitBox; // its shape asks for the contact output: the hit list writes it after the head
    StateArray<ShortVec3, kOrbRings> rings{};
    void clearParams() {
        burst = 0;
        paramsWord_ = 0;
    }
};

} // namespace od::port::crusher_state
