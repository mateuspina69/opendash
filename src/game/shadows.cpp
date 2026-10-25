// The actors' ground shadows (see shadows.hpp).
#include "game/shadows.hpp"

namespace od::port {

void shadowGroundProbe(const PlayerEnv& env, const Actor& actor) {
    if (shadowList().cursor < kShadowListEnd && actor.onPlatform == 0) actorGround(env, actor);
}

} // namespace od::port
