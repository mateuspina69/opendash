// The table of players (game/player/player_state): each id's player has its own object,
// address and slot id, the pools' resolvers reach it, and with nobody else in the session the slot ids answer as the
// exe's do.
//   test_player_table
#include <stdexcept>

#include "check.hpp"
#include "game/actor_pools.hpp"
#include "game/player/player_state.hpp"
#include "game/random.hpp"
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

void testRefusals() {
    bool threw = false;
    try {
        joinPlayer(0);
    } catch (const std::logic_error&) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
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
    testWorldDigest();
    testRefusals();
    return testResult("test_player_table");
}
