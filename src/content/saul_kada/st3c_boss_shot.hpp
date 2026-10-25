// The gold-statue boss's shots (ST3C pool 0x40 type 0x1f, exe 0x5cb200), see content/saul_kada/st3c_boss_shot.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kBossShotType = 0x1f;
constexpr uint8_t kHomingShotSubtype = 0, kThrownPieceSubtype = 1;
// The shots' spawn parameters: their attack; the homing shot's blast attack / the thrown piece's side (0: the boss's
// bone 3, else 2); the piece released (the boss sets it)
struct BossShotParams {
    explicit BossShotParams(const ProjectileActor& actor) : BossShotParams(const_cast<ProjectileActor&>(actor)) {}
    explicit BossShotParams(ProjectileActor& actor) : attack(actor.params[0]), blastAttack(actor.params[1]), side(actor.params[1]), released(actor.params[2]) {}
    FieldAlias<int8_t, uint8_t> attack;
    FieldAlias<int8_t, uint8_t> blastAttack;
    uint8_t& side;
    uint8_t& released;
};
// The thrown piece's boss (the boss writes it at the spawn)
struct ThrownPieceBoss {
    explicit ThrownPieceBoss(const ProjectileActor& actor) : ThrownPieceBoss(const_cast<ProjectileActor&>(actor)) {}
    explicit ThrownPieceBoss(ProjectileActor& actor) : boss(actor.hitBox.result) {}
    uint32_t& boss; // the boss's identity word (the hit box's first word)
};

// exe 0x5cb200: sub 0 attack 3's homing shot; sub 1 phase one's thrown pieces (held at the boss's bones 2 / 3, released
// by the boss, then homing)
void bossShotUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
