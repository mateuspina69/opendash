// The class state of bomb.cpp's class: the fields the bomb keeps in its slot (game/actor_state ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::bomb_state {

// The bomb's own fields
struct BombFields : ActorClassState {
    explicit BombFields(StaticActor& actor) : carrierCommand(actor.params[0]), hitBox(actor.hitBox) {}
    uint8_t& carrierCommand;    // what its carrier tells it (kCarrierLetGo / kCarrierBlowUp)
    HitBoxHead& hitBox;
    uint8_t collisionBits = 0;  // kBombHitWall | kBombBounced | kBombOffGrid
    uint8_t behaviour = 0;      // kBombBounces | kBombThrownEntry | kBombNoHitEntry | kBombHurtsPlayer
    int16_t fuse = 0;           // the fuse's ticks left
    uint8_t fuseLit = 0;

    void clearBlock() override {
        collisionBits = 0;
        behaviour = 0;
        fuse = 0;
        fuseLit = 0;
    }
};

} // namespace od::port::bomb_state
