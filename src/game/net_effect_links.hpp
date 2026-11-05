// An effect runs on a client from its event, but some classes hold actors their spawner gave them: a client needs the same actors
// there. Each such class says what it holds on the world and takes it on the client, here; the content's classes register theirs.
#pragma once

#include <array>

#include "engine/routine_ids.hpp"
#include "game/actor_state.hpp"

namespace od::port {

struct NetEffectLinks {
    // the world: the actors it holds; false: the client's own copy of its spawner makes it there, no event goes
    bool (*read)(EffectActor& effect, std::array<const ActorHeader*, 2>& held);
    // the client: the same actors, as this machine holds them. Null: not here
    void (*write)(EffectActor& effect, const std::array<ActorHeader*, 2>& held);
};
void registerNetEffectLinks(RoutineId cls, const NetEffectLinks& links);
const NetEffectLinks* netEffectLinksOf(RoutineId cls);

} // namespace od::port
