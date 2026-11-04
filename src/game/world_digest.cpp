// The digest of a world's state (see world_digest.hpp).
#include "game/world_digest.hpp"

#include <cstdio>

#include "game/actor_pools.hpp"
#include "game/game_state.hpp"
#include "game/player/player_state.hpp"
#include "game/random.hpp"

namespace od::port {

namespace {

struct Hasher {
    uint64_t h = 0xcbf29ce484222325ull;
    void (*detail)(const char*) = nullptr;
    void add(uint64_t v) {
        for (int i = 0; i < 8; ++i) h = (h ^ ((v >> (i * 8)) & 0xff)) * 0x100000001b3ull;
    }
};

void addHeader(Hasher& f, const ActorHeader& a) {
    f.add(a.at);
    f.add(a.flags);
    f.add(a.type);
    f.add(a.subtype);
    f.add(a.state);
    f.add(a.subState);
    f.add(a.step);
    if (f.detail) {
        char b[96];
        std::snprintf(b, sizeof b, "actor %08x flags %02x type %02x.%02x state %u.%u.%u", a.at, unsigned(a.flags),
                      unsigned(a.type), unsigned(a.subtype), unsigned(a.state), unsigned(a.subState), unsigned(a.step));
        f.detail(b);
    }
}

void addBody(Hasher& f, const Actor& a) {
    addHeader(f, a);
    f.add(uint32_t(int32_t(a.pos.x)));
    f.add(uint32_t(int32_t(a.pos.y)));
    f.add(uint32_t(int32_t(a.pos.z)));
    f.add(uint16_t(a.rotation.y));
    f.add(uint16_t(a.hitPoints));
    if (f.detail) {
        char b[96];
        std::snprintf(b, sizeof b, "  body %08x pos %08x %08x %08x yaw %d hp %d", a.at, uint32_t(int32_t(a.pos.x)),
                      uint32_t(int32_t(a.pos.y)), uint32_t(int32_t(a.pos.z)), int(a.rotation.y), int(a.hitPoints));
        f.detail(b);
    }
}

template <class Pool> uint32_t addBodies(Hasher& f, Pool& pool) {
    uint32_t live = 0;
    for (size_t i = 0; i < Pool::size(); ++i)
        if (pool[i].flags & kActorLive) {
            addBody(f, pool[i]);
            ++live;
        }
    return live;
}

template <class Pool> uint32_t addHeaders(Hasher& f, Pool& pool) {
    uint32_t live = 0;
    for (size_t i = 0; i < Pool::size(); ++i)
        if (pool[i].flags & kActorLive) {
            addHeader(f, pool[i]);
            ++live;
        }
    return live;
}

} // namespace

WorldDigest worldDigest(void (*detail)(const char* line)) {
    Hasher f;
    f.detail = detail;
    const GameState& game = gameState();
    f.add(game.mode);
    f.add(game.stage);
    f.add(game.room);
    f.add(game.story);
    f.add(randomState().rng);
    f.add(randomState().crtSeed);
    if (detail) {
        char b[96];
        std::snprintf(b, sizeof b, "game mode %u stage %02x room %u story %u rng %08x crt %08x", unsigned(game.mode),
                      unsigned(game.stage), unsigned(game.room), unsigned(game.story), randomState().rng,
                      randomState().crtSeed);
        detail(b);
    }
    for (size_t id = 0; id < kPlayerSlots; ++id)
        if (playerPresent(id)) addBody(f, playerById(id));
    WorldDigest d;
    d.liveActors += addBodies(f, shotPool());
    d.liveActors += addBodies(f, animatedPool());
    d.liveActors += addBodies(f, projectilePool());
    d.liveActors += addBodies(f, staticPool());
    d.liveActors += addHeaders(f, effectPool());
    d.liveActors += addHeaders(f, controllerPool());
    d.hash = f.h;
    return d;
}

} // namespace od::port
