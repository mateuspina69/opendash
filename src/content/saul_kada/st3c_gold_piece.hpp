// The gold-statue boss's gold pieces (ST3C static type 0x53, exe 0x5b8440): the thrown gold and the electric
// ring around a thrown piece, see content/saul_kada/st3c_gold_piece.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kGoldPieceType = 0x53;
constexpr uint8_t kThrownGoldSubtype = 0, kElectricRingSubtype = 1;
// The thrown gold's spawn parameter: its attack
struct ThrownGoldParams {
    explicit ThrownGoldParams(const StaticActor& actor) : ThrownGoldParams(const_cast<StaticActor&>(actor)) {}
    explicit ThrownGoldParams(StaticActor& actor) : attack(actor.params[0]) {}
    uint8_t& attack;
};
// The electric ring's size (its thrown piece sets it every tick)
struct ElectricRingParams {
    explicit ElectricRingParams(const StaticActor& actor) : ElectricRingParams(const_cast<StaticActor&>(actor)) {}
    explicit ElectricRingParams(StaticActor& actor) : size(actor.params, 0) {}
    BytesField<int16_t, 4> size;
};

// exe 0x5b8440: sub 0 the thrown gold (flies, bounces off walls, a hit entry); sub 1 the electric ring and its
// lightning around a thrown piece (content/saul_kada/st3c_boss_shot moves it)
void goldPieceUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
