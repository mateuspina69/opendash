// The actors' ground shadows (exe 0x5f20f0: one entry of the shadow list 0x6d0070 per actor, drawn later). The entry
// is render output (not ported); its ground probe is game logic: it leaves the collision
// state that the class code after it may read.
#pragma once

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"

namespace od::port {

// The shadow list's cursor (the next free entry; the list ends at kShadowListEnd)
struct ShadowListState {
    uint32_t cursor = 0; // the address in the game state's shadow list
};
inline ShadowListState& shadowList() {
    static ShadowListState s;
    return s;
}
constexpr uint32_t kShadowListEnd = 0xa714c0;

// exe 0x5f20f0's probe: while the list has room, the ground under an actor not standing on another one (exe 0x40f190,
// game/actor_movement actorGround) -- the shadow's height, whose collision state stays behind
void shadowGroundProbe(const PlayerEnv& env, const Actor& actor);

} // namespace od::port
