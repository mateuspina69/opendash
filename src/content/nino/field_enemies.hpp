// The field enemies of Nino island: the harmless critter 0x4a0f70 (pool 0x20 type 0x32 sub 0 of ST15 / ST16) and the
// ST16 box carrier 0x4d1700 (pool 0x20 type 0x49 sub 0), content/nino/field_enemies.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kBoxCarrierType = 0x49; // its animated type in ST16's table

// The box carrier's spawn parameters (its spawner writes them: content/nino/field_spawners): its Birdbots' row (of the
// table 0x895308), sent home, the player's grip pose while it holds the player (1 / 2), the ST16 box spawner's slot
// (pool e0), its target point
struct BoxCarrierParams {
    explicit BoxCarrierParams(const AnimatedActor& actor) : BoxCarrierParams(const_cast<AnimatedActor&>(actor)) {}
    explicit BoxCarrierParams(AnimatedActor& actor) : riderRow(actor.params[0]), goHome(actor.params[1]), gripPose(actor.params[2]), boxSpawner(actor.params[3]), targetX(actor.velocity.spare), targetZ(actor.acceleration.spare) {}
    FieldAlias<int8_t, uint8_t> riderRow;
    uint8_t& goHome;
    FieldAlias<int8_t, uint8_t> gripPose;
    uint8_t& boxSpawner;
    int16_t& targetX;
    int16_t& targetZ;
};
// ... and its carry bits (what script 0x33 reads while it holds the player: content/nino/st16_script33)
struct BoxCarrierGrip : ActorClassState, BoxCarrierParams {
    explicit BoxCarrierGrip(AnimatedActor& actor) : BoxCarrierParams(actor), carry(actor.shownParts) {}
    uint8_t& carry; // kCarrierRiders | kCarrierBox | kCarrierBusy | kCarrierHoldsPlayer

    void clearBlock() override {
    }
};
constexpr uint8_t kCarrierRiders = 0x01;      // its two Birdbots ride it
constexpr uint8_t kCarrierBox = 0x02;         // it carries a lift box
constexpr uint8_t kCarrierBusy = 0x04;        // flinching / dying: no hurt reaction
constexpr uint8_t kCarrierHoldsPlayer = 0x08; // it holds the player

// exe 0x4a0f70: the harmless Nino-island critter -- drops in, wanders, turns off walls, is knocked about by hits
// (hitting it costs the balance), liftable / throwable; leaves when far / off screen (carried away: the balance drops
// and a message trigger (e0 type 0x1b) is queued)
void critterUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4d1700: the ST16 box carrier -- walks to its target point carrying two Birdbots (pool 0x20 type 0x2d sub 1),
// fetches the lift boxes (static type 0x25) the e0 spawner lists, stacks / throws them, grabs and throws the player,
// walks home and leaves; dies in a spin (blast + a knocked-down Birdbot, type 0x2d sub 0)
void boxCarrierUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
