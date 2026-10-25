// The explosions every stage spawns (the global static table 0x8b1ff4): the death explosion of a defeated enemy (static
// type 8: flashes around the body) and the blast ring of a bomb / a shell / a breaking wall (static type 7: a growing
// sphere that hits what is in it and breaks the walls it reaches), and the spawn parameters their spawners write.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"
#include "game/state_array.hpp"

namespace od::port {

// The spawn parameters of a blast (static type 7): the ring's size (its radius scale) and its attack (the hit entry's
// damage; the breakable walls take attack >> 6)
struct BlastParams {
    explicit BlastParams(const ActorHeader& actor) : BlastParams(const_cast<ActorHeader&>(actor)) {}
    explicit BlastParams(ActorHeader& actor) : size(actor.params, 0), attack(actor.params, 2) {}
    BytesField<uint16_t, 4> size;
    BytesField<uint16_t, 4> attack;
};
// The spawn parameters of a death explosion (static type 8): the spread of its flashes around the position and their
// number (one every other tick)
struct ExplosionParams {
    explicit ExplosionParams(const ActorHeader& actor) : ExplosionParams(const_cast<ActorHeader&>(actor)) {}
    explicit ExplosionParams(ActorHeader& actor) : spread(actor.params, 0), flashes(actor.params, 2) {}
    BytesField<uint16_t, 4> spread;
    BytesField<uint16_t, 4> flashes;
};

// ---- the death explosion's state (its logic and the effects' draw, game/effects_draw, read it)
// One of its four flashes: the point and the sprite size (s16 each)
struct ExplosionFlash {
    ShortVec3 point;
    int16_t size = 0;
};
// The explosion's fields (its class block reuses the slot's model fields: it has no model): the flashes still to
// come, the flashes alive, their lives (ticks of the 8-frame sprite) and points; the phase in the header's sub-state
struct ExplosionFields : ActorClassState {
    explicit ExplosionFields(StaticActor& actor) : flashesLeft(actor.model, 0), flashesAlive(actor.model, 2) {}
    PartField<uint16_t, uint32_t> flashesLeft;
    PartField<uint16_t, uint32_t> flashesAlive;
    StateArray<int8_t, 4> lives{};
    StateArray<ExplosionFlash, 4> flashes{};
    void clearLives() {
        lives = {};
    }
};
enum class ExplosionState : uint8_t { Start = 0, Run = 1 };

// a blast (static type 7, variant 0) at the s16 point, its size and attack (a global static slot, exe 0x5e1300; none
// when the pool is full)
void spawnBlastAt(int16_t x, int16_t y, int16_t z, uint16_t size, uint16_t attack);
// a death explosion (static type 8, variant 0) at the s16 point, the spread and the number of its flashes
void spawnDeathExplosionAt(int16_t x, int16_t y, int16_t z, uint16_t spread, uint16_t flashes);

// exe 0x564370 (static type 8): the death explosion -- every other tick a flash (sound 0x89) around the position
// (spread), then (variant & 1) a blast; freed when the last flash is gone
void explosionUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x563b10 (static type 7): the blast ring -- the shake / vibration / sound, then 12 ticks of a growing sphere
// (draw commands) whose box hits the walls (breakable walls: attack >> 6) and whose hit entry hurts by the sweep group
void blastUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
