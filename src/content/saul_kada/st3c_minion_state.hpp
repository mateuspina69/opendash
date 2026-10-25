// The class states of st3c_minion.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st3c_minion_state {

// The minion's own bytes (its block, 0x20 bytes cleared at the bind): its hit box, its flags, its timer, the player's
// angle and planar distance seen from it, its escape yaw, its Servbot gone
struct MinionFields : ActorClassState {
    explicit MinionFields(AnimatedActor& actor) : servbotGone(actor.shownParts) {}
    uint8_t& servbotGone;
    // its spawner's slot byte it clears when it ends (its spawn word holds the byte's exe address)
    uint8_t* spawnerSlot = nullptr;
    HitBoxHead hitBox;
    uint16_t flags = 0;
    uint16_t timer = 0;
    int16_t playerYaw = 0;
    uint16_t playerDistance = 0;
    int16_t escapeYaw = 0;
    PartField<uint8_t, uint16_t> flagsLow{flags, 0};

    void clearBlock() override {
        hitBox = {};
        flags = 0;
        timer = 0;
        playerYaw = 0;
        playerDistance = 0;
        escapeYaw = 0;
    }
};

// The exe globals st3c_minion.cpp keeps (their exe addresses in the comments)
struct St3cMinionGlobals {
    uint32_t minionBlock = 0; // 0xa59e98
};
inline St3cMinionGlobals& st3cMinionGlobals() {
    static St3cMinionGlobals s;
    return s;
}

} // namespace od::port::st3c_minion_state
