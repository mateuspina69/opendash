// The table of players (game/player/player_state): each id's player has its own object,
// address and slot id, the pools' resolvers reach it, and with nobody else in the session the slot ids answer as the
// exe's do.
//   test_player_table
#include <stdexcept>

#include "check.hpp"
#include "game/actor_pools.hpp"
#include "game/loaded_data.hpp"
#include "game/player/player_state.hpp"
#include "game/player/players.hpp"
#include "game/player/remote_player.hpp"
#include "game/random.hpp"
#include "game/visibility.hpp"
#include "game/world_view.hpp"
#include "game/world_digest.hpp"

using namespace od::port;

namespace {

void testIdentity() {
    CHECK(&player() == &playerById(0));
    CHECK(player().at == kPlayer && player().slot == 0);
    CHECK(playerPresent(0));
    for (size_t id = 0; id < kPlayerSlots; ++id) {
        Player& p = playerById(id);
        CHECK(p.at == playerAddress(id));
        CHECK(p.slot == id);
        CHECK(playerId(p) == id);
        CHECK(actorSlotClass(p.slot) == kPlayerClass);
        // the resolvers reach this player's own object
        CHECK(&actorAt(p.at) == &p);
        CHECK(&actorHeaderAt(p.at) == &p);
        CHECK(&actorLayoutAt(p.at) == &p);
        CHECK(&asPlayer(static_cast<ActorHeader&>(p)) == &p);
        CHECK(&asPlayer(static_cast<const ActorHeader&>(p)) == &p);
        CHECK(slotByIdentityWord<Actor>(p.at) == &p);
        // no pool claims a player's address
        CHECK(!ShotPool::contains(p.at) && !AnimatedPool::contains(p.at) && !ProjectilePool::contains(p.at));
        CHECK(!StaticPool::contains(p.at) && !EffectPool::contains(p.at) && !ControllerPool::contains(p.at));
    }
    CHECK(playerIdAtAddress(kPlayer + 1) == -1);
    CHECK(playerIdAtAddress(kOtherPlayers + 1) == -1);
    CHECK(playerIdAtAddress(playerAddress(kPlayerSlots)) == -1);
    CHECK(playerIdAtAddress(kShotPool) == -1);
}

// nobody else in the session: every slot id of the class is the one player, as in the exe (exe 0x5e0fa0)
void testSlotsAlone() {
    for (uint32_t slot = 0; slot < kShotClass; ++slot) CHECK(actorBySlot(slot) == kPlayer);
    CHECK(actorBySlot(0x100) == kPlayer); // past a byte: class 0 by the exe's mask
}

void testJoinAndLeave() {
    Player& other = playerById(3);
    other.hitPoints = 77;
    joinPlayer(3);
    CHECK(playerPresent(3));
    CHECK(other.hitPoints == 0); // constructed anew
    CHECK(other.at == playerAddress(3) && other.slot == 3);
    CHECK(actorBySlot(3) == playerAddress(3));
    CHECK(actorBySlot(2) == kPlayer);
    CHECK(actorBySlot(8) == kPlayer); // class 0, past the table
    CHECK(actorBySlot(0) == kPlayer);

    // the session's start constructs every player anew and keeps who is in the session
    player().hitPoints = 5;
    other.hitPoints = 6;
    resetPlayer();
    CHECK(player().hitPoints == 0 && other.hitPoints == 0);
    CHECK(player().at == kPlayer && player().slot == 0);
    CHECK(other.at == playerAddress(3) && other.slot == 3);
    CHECK(playerPresent(3));

    leavePlayer(3);
    CHECK(!playerPresent(3));
    CHECK(actorBySlot(3) == kPlayer);
}

void testNearest() {
    resetPlayer();
    const auto at = [](Player& p, int x, int z) {
        p.pos.x = int32_t(uint32_t(x) << 16);
        p.pos.z = int32_t(uint32_t(z) << 16);
    };
    Fixed16Vec3 point;
    point.x = 100 << 16;
    point.z = 0;
    at(player(), 0, 0);

    // alone: the one player, however far
    at(playerById(2), 100, 0);
    CHECK(&nearestPlayer(point) == &player());

    joinPlayer(2);
    joinPlayer(5);
    at(playerById(2), 100, 10);
    at(playerById(5), 300, 0);
    CHECK(&nearestPlayer(point) == &playerById(2));
    point.x = -50 << 16;
    CHECK(&nearestPlayer(point) == &player());
    point.x = 400 << 16;
    CHECK(&nearestPlayer(point) == &playerById(5));

    // as near: the lower id
    at(playerById(5), 100, 10);
    point.x = 100 << 16;
    CHECK(&nearestPlayer(point) == &playerById(2));

    int seen = 0;
    size_t sum = 0;
    forEachPlayer([&](Player& p) {
        ++seen;
        sum += playerId(p);
    });
    CHECK(seen == 3 && sum == 7);

    leavePlayer(2);
    leavePlayer(5);
    CHECK(&nearestPlayer(point) == &player());
    resetPlayer();
}

void testActingPlayer() {
    CHECK(actingPlayerId() == 0 && &actingPlayer() == &player());
    const uint32_t mine = allocActor10();
    CHECK(shotActorAt(mine).ownerPlayer == 0);
    {
        const ScopedActingPlayer acting(4);
        CHECK(&actingPlayer() == &playerById(4));
        const uint32_t theirs = allocActor10();
        CHECK(shotActorAt(theirs).ownerPlayer == 4);
        {
            const ScopedActingPlayer nested(shotActorAt(mine).ownerPlayer);
            CHECK(actingPlayerId() == 0);
        }
        CHECK(actingPlayerId() == 4);
    }
    CHECK(actingPlayerId() == 0);
    resetActorPools();
    actorSystem().shotsLive = 0;
}

// the digest two runs of a room are compared by: the same state the same hash, a change in what it covers another
void testWorldDigest() {
    resetPlayer();
    const uint64_t base = worldDigest().hash;
    CHECK(worldDigest().hash == base);

    player().hitPoints = 5;
    CHECK(worldDigest().hash != base);
    player().hitPoints = 0;
    CHECK(worldDigest().hash == base);

    randomState().rng ^= 1;
    CHECK(worldDigest().hash != base);
    randomState().rng ^= 1;

    joinPlayer(2);
    CHECK(worldDigest().hash != base);
    leavePlayer(2);
    CHECK(worldDigest().hash == base);

    shotPool()[0].flags |= kActorLive;
    CHECK(worldDigest().liveActors == 1);
    CHECK(worldDigest().hash != base);
    shotPool()[0].flags &= uint8_t(~kActorLive);
    CHECK(worldDigest().hash == base);
}

// a world where player 0 is elsewhere: the others answer, and the slot ids of the class still resolve
void testPlayerZeroAway() {
    resetPlayer();
    joinPlayer(3);
    leavePlayer(0);
    CHECK(!playerPresent(0));
    Fixed16Vec3 point;
    CHECK(&nearestPlayer(point) == &playerById(3));
    int seen = 0;
    forEachPlayer([&](Player&) { ++seen; });
    CHECK(seen == 1);
    joinPlayer(0);
    leavePlayer(3);
    CHECK(playerPresent(0));
    resetPlayer();
}

// a player who leaves: what they owned goes to one still in the world
void testOwnerLeaves() {
    od::LoadedFileData files; // a remote player's animation frames are mapped there
    const od::ScopedLoadedFiles bound(files);
    resetActorPools();
    actorSystem().shotsLive = 0;
    PlayerSnapshot s;
    remotePlayerJoin(2, s);
    remotePlayerJoin(5, s);
    uint32_t theirs = 0;
    {
        const ScopedActingPlayer acting(5);
        theirs = allocActor10();
    }
    CHECK(shotActorAt(theirs).ownerPlayer == 5);
    remotePlayerLeave(5);
    CHECK(!playerIsRemote(5) && !playerPresent(5));
    CHECK(shotActorAt(theirs).ownerPlayer == 0); // player 0 is in this world
    remotePlayerLeave(2);
    resetActorPools();
    actorSystem().shotsLive = 0;
}

// a world without a screen: seen is near any player, and the view's direction is the player's
void testNoView() {
    resetPlayer();
    player().pos.x = 0;
    player().pos.z = 0;
    player().rotation.y = 0x123;
    const int16_t near[3] = {0, 0, 0x100};
    const int16_t far[3] = {0, 0, int16_t(kWorldViewRange + 0x100)};
    setWorldHasNoView(true);
    CHECK(worldHasNoView());
    CHECK(pointVisibleOrNear(near, 0x10) == 0); // within the view's range, whatever the caller's reach
    CHECK(pointVisibleOrNear(far, 0x10) == -1);
    CHECK(viewYawNear(player()) == 0x123);
    setWorldHasNoView(false);
    resetPlayer();
}

void testRefusals() {
    bool threw = false;
    try {
        playerById(kPlayerSlots);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    CHECK(threw);
}

} // namespace

int main() {
    testIdentity();
    testSlotsAlone();
    testJoinAndLeave();
    testSlotsAlone();
    testNearest();
    testActingPlayer();
    testWorldDigest();
    testPlayerZeroAway();
    testOwnerLeaves();
    testNoView();
    testRefusals();
    return testResult("test_player_table");
}
