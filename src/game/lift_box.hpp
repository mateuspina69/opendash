// The box you lift, class 0x5a89b0 (static type 0x25 of the ST16 / ST31 / ST36 / ST37 tables): it rests (a solid
// you stand on; a mover too while on a platform), falls off edges, is pushed by hits (hops away), lifted, carried and
// thrown (bounces, lands; a box landing on a stacked box breaks), held by something else (a holder sets +0xc),
// destroyed by strong hits (a blast, freed).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"

namespace od::port {

constexpr uint8_t kLiftBoxType = 0x25; // its static type in the stages' tables

// The box's bytes an outside holder writes (the ST16 box carrier, content/nino/field_enemies; its spawner): the hold
// (kLiftBoxHolds / kLiftBoxLetGo, | kLiftBoxHolderMover) and the box's own "lifted / thrown / pushed" byte it reads
struct LiftBoxHolding {
    explicit LiftBoxHolding(const ActorHeader& actor) : LiftBoxHolding(const_cast<ActorHeader&>(actor)) {}
    explicit LiftBoxHolding(ActorHeader& actor) : holder(actor.params[0]), carried(actor.params[1]) {}
    uint8_t& holder;
    uint8_t& carried;
    bool unheld() const { return get_holdWord_() == 0; }

private:
    int16_t get_holdWord_() const { return int16_t(uint32_t(std::make_unsigned_t<uint8_t>(holder)) << 0 | uint32_t(std::make_unsigned_t<uint8_t>(carried)) << 8); }
    void set_holdWord_(uint32_t v) { holder = uint8_t(std::make_unsigned_t<uint8_t>(v >> 0)); carried = uint8_t(std::make_unsigned_t<uint8_t>(v >> 8)); }
};
constexpr uint8_t kLiftBoxHolderMask = 0x7f, kLiftBoxHolds = 1, kLiftBoxLetGo = 2, kLiftBoxHolderMover = 0x80;

// exe 0x5a89b0: the box's update (ActorUpdateFn)
void liftBoxUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
