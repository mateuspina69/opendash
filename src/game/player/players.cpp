// The players of a session (see players.hpp).
#include "game/event.hpp"
#include "game/player/players.hpp"

#include "game/distances.hpp"

namespace od::port {

Player& nearestPlayer(const Fixed16Vec3& point) {
    Player* nearest = &player();
    int best = -1;
    bool standing = false; // one who stands comes before anyone who is down
    forEachPlayer([&](Player& p) {
        const int d = planarDistance(point, p.pos);
        const bool up = p.hitPoints >= 0;
        if (best < 0 || (up && !standing) || (up == standing && d < best)) {
            best = d;
            nearest = &p;
            standing = up;
        }
    });
    return *nearest;
}

Player& nearestPlayer(const int32_t point[3]) {
    Fixed16Vec3 v;
    v.x = point[0];
    v.y = point[1];
    v.z = point[2];
    return nearestPlayer(v);
}

Player& nearestPlayer(int16_t x, int16_t z) {
    Fixed16Vec3 v;
    v.x.whole = x;
    v.z.whole = z;
    return nearestPlayer(v);
}

bool roomPlayerMeets(bool (*cond)(const Player& p)) {
    const Player* who = findPlayer([&](const Player& p) { return p.hitPoints >= 0 && cond(p); });
    if (!who) return false;
    setEventOwner(playerId(*who));
    return true;
}

bool roomPlayerStands() {
    return anyPlayer([](const Player& p) { return p.hitPoints >= 0; });
}

bool anyPlayerTalksTo(const ActorHeader& actor) {
    bool talks = false;
    forEachPlayer([&](const Player& p) { talks = talks || p.talksTo(actor); });
    return talks;
}

Player& carrierOrNearest(const Actor& actor) {
    Player& owner = playerById(actor.ownerPlayer);
    return owner.interactionTarget.is(actor, kInteractLiftMode) ? owner : nearestPlayer(actor.pos);
}

} // namespace od::port
