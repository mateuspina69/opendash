// The gunships of ST18 (static type 0x1b subs 2 / 0): the scripted one of the cutscenes (exe 0x5a2cc0) and those of the
// ship fight (exe 0x59e630), content/nino/st18_gunships.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/stage_state.hpp"

namespace od::port {

// ST18's stage block during the ship fight: the track ids still free for the dropped Birdbots (a mask, bit k - 1: id
// k), the Birdbots dropped, a third word the fight's spawner clears (content/nino/field_spawners)
struct ShipFightBlock : StagePart<ShipFightBlock> {
    uint32_t freeIds = 0; // +0x0
    uint32_t dropped = 0; // +0x4
    uint32_t spare = 0; // +0x8
};
inline ShipFightBlock& shipFightBlock() {
    return stageBlock().part<ShipFightBlock>();
}

constexpr uint8_t kShipFightGunshipType = 0x1b; // the ST18 table's static type (sub-type 0)
// The fight gunship's parameters (its spawner writes them: content/nino/field_spawners): its orders (1: to the square
// when near it, 2: at once) and its shield (its hit entry off the lock-on while set: the Birdbots it dropped alive)
// The fight gunship's parameters (its spawner writes them: content/nino/field_spawners): its orders (1: to the square
// when near it, 2: at once) and its shield (its hit entry off the lock-on while set: the Birdbots it dropped alive)
struct ShipFightGunshipParams {
    explicit ShipFightGunshipParams(const StaticActor& actor) : ShipFightGunshipParams(const_cast<StaticActor&>(actor)) {}
    explicit ShipFightGunshipParams(StaticActor& actor) : orders(actor.params[0]), shielded(actor.params[1]) {}
    FieldAlias<int8_t, uint8_t> orders;
    uint8_t& shielded;
};

// exe 0x5a2cc0 (ST18 static type 0x1b sub 2, records 0x8a6764 / 0x8a67dc / 0x8a69d4 by variant 0 / 1 / 2): the
// gunship of the ST18 cutscenes -- hovering, pitching down / up toward yaw 0xe00, or weaving between 0xe00 and 0x600
// trailing smoke
void cutsceneGunshipUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x59e630 (ST18 static type 0x1b sub 0): the gunships of the ST18 ship fight -- circling and shooting, climbing
// in from the edge, circling the square's centre dropping Birdbots, flying off
void shipFightGunshipUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
