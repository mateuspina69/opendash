// The players of a session (see players.hpp).
#include "game/player/players.hpp"

#include "game/distances.hpp"

namespace od::port {

Player& nearestPlayer(const Fixed16Vec3& point) {
    Player* nearest = &player();
    int best = -1;
    forEachPlayer([&](Player& p) {
        const int d = planarDistance(point, p.pos);
        if (best < 0 || d < best) {
            best = d;
            nearest = &p;
        }
    });
    return *nearest;
}

Player& carrierOrNearest(const Actor& actor) {
    Player& owner = playerById(actor.ownerPlayer);
    return owner.interactionTarget.is(actor, kInteractLiftMode) ? owner : nearestPlayer(actor.pos);
}

} // namespace od::port
