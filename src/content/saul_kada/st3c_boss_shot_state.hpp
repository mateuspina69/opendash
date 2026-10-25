// The class states of st3c_boss_shot.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"

namespace od::port::st3c_boss_shot_state {

// The homing shot's own bytes: its hit box (its x / z where the box's previous ones are), its timer; launched (its last
// spawn parameter)
struct HomingFields : ActorClassState {
    explicit HomingFields(ProjectileActor& actor) : hitBox(actor.hitBox), launched(actor.params[3]) {}
    HitBoxHead& hitBox;
    uint8_t& launched;
    StateArray<uint8_t, 4> clearedPad0{}; // bytes the block clear covers (no reader)
    uint16_t timer = 0;
    StateArray<uint8_t, 18> clearedPad1{}; // bytes the block clear covers (no reader)
    void clearBytes() {
        clearedPad0 = {};
        timer = 0;
        clearedPad1 = {};
        hitBox = {};
    }
};

// The thrown piece's own bytes (its boss before them): its hit box, its ring, the ring's angles, its timer, the ring's
// size; its root matrix's padding short and its root words (the ring copies them)
struct PieceFields : ActorClassState {
    explicit PieceFields(ProjectileActor& actor) : boss(actor.hitBox.result), rootPad(actor.rootPad) {}
    uint32_t& boss;
    int16_t& rootPad;
    StateArray<uint32_t, 8> rootWords{};
    HitBoxHead hitBox;
    uint32_t ring = 0;
    ShortVec4 ringAngles;
    uint16_t timer = 0;
    uint16_t ringSize = 0;
    StateArray<uint8_t, 4> clearedPad0{}; // bytes the block clear covers (no reader)
    void clearBytes() {
        hitBox = {};
        ring = 0;
        ringAngles = {};
        timer = 0;
        ringSize = 0;
        clearedPad0 = {};
    }
};

// The exe globals holding the running shot's / piece's class block (its exe address: a value, never read through)
struct BossShotGlobals {
    uint32_t homingBlock = 0; // 0xa5a938
    uint32_t pieceBlock = 0;  // 0xa5a934
};
inline BossShotGlobals& bossShotGlobals() {
    static BossShotGlobals s;
    return s;
}

} // namespace od::port::st3c_boss_shot_state
