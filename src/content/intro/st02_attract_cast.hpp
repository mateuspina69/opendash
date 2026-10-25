// The attract intro's cast in ST02 (script 3; content/intro/st02_attract_cast.cpp): the airship's crew and props that
// key their animations, faces and motion on the cast's copy of the script's timeline (game/script_state
// CastTimeline: the step sub and tick), each picking its variant by its spawn byte 8 -- the byte the script switches.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

struct PlayerEnv;

// the variant the ST02 cast plays (its spawn byte 8: the index in its class's variant table, signed)
struct AttractCastVariant {
    explicit AttractCastVariant(const ActorHeader& actor) : AttractCastVariant(const_cast<ActorHeader&>(actor)) {}
    explicit AttractCastVariant(ActorHeader& actor) : index(actor.params[0]) {}
    FieldAlias<int8_t, uint8_t> index;
};

// The classes (registered in content/intro/register.cpp)
void st02PropUpdate(const PlayerEnv& env, uint32_t actor);          // exe 0x48a0f0 (pool 0x20 type 0x27)
void st02DriftingStaticUpdate(const PlayerEnv& env, uint32_t actor); // exe 0x59d000 (static type 0x1a)
void st02RollUpdate(const PlayerEnv& env, uint32_t actor);          // exe 0x475c80 (pool 0x20 type 0x16)
void st02BarrelUpdate(const PlayerEnv& env, uint32_t actor);        // exe 0x47bf40 (pool 0x20 type 0x1a)
void st02PlacedStaticUpdate(const PlayerEnv& env, uint32_t actor);  // exe 0x59ada0 (static type 0x14)
void st02WatcherUpdate(const PlayerEnv& env, uint32_t actor);       // exe 0x47f800 (pool 0x20 type 0x1d)
void st02TalkerUpdate(const PlayerEnv& env, uint32_t actor);        // exe 0x480580 (pool 0x20 type 0x1e)
void st02FaceUpdate(const PlayerEnv& env, uint32_t actor);          // exe 0x481da0 (pool 0x20 type 0x21)
void st02SpeakerUpdate(const PlayerEnv& env, uint32_t actor);       // exe 0x48aef0 (pool 0x20 type 0x28)
void st02ScreenUpdate(const PlayerEnv& env, uint32_t actor);        // exe 0x4b5c40 (pool 0x20 type 0x3c)
void st02BlinkerUpdate(const PlayerEnv& env, uint32_t actor);       // exe 0x4848c0 (pool 0x20 type 0x23)
void st02StanderUpdate(const PlayerEnv& env, uint32_t actor);       // exe 0x488f60 (pool 0x20 type 0x25)
void st02StillModelUpdate(const PlayerEnv& env, uint32_t actor);    // exe 0x5ad6f0 (static type 0x34)
void st02SceneryUpdate(const PlayerEnv& env, uint32_t actor);       // exe 0x59b630 (static type 0x16)

} // namespace od::port
