// Netplay's shared world: the PvP hit's attack hurts its victim, the puppets' draw flags and the host's story travel
// over the wire, the story shown on a client keeps the player's own for its saves.
//   test_net_world <dash2.exe>
#include <filesystem>

#include "app/netplay_world_codec.hpp"
#include "check.hpp"
#include "data_source_binding.hpp"
#include "game/actor_state.hpp"
#include "game/game_state.hpp"
#include "game/hit_list.hpp"
#include "game/net_world.hpp"
#include "game/player/player_damage.hpp"
#include "game/player/player_state.hpp"
#include "game/story_flags.hpp"

namespace {

using namespace od::port;

// a buster shot's result on a stand-in body: the shot's kind (the one enemies take damage from) and 8 points
constexpr uint32_t kShotOnBody = kHitAttackByPlayer | 8;

int16_t hitPointsAfter(uint32_t result) {
    Player& p = player();
    p.hitPoints = 80;
    p.maxHitPoints = 80;
    p.solidHit.result = 0;
    p.hitBox.result = result;
    gameState().modeStep = uint8_t(FieldStep::Playing);
    applyHits(p);
    return p.hitPoints;
}

void pvpHitHurts() {
    CHECK(hitPointsAfter(kShotOnBody) == 80); // the shot's own kind: the player ignores it (the bug)
    CHECK(hitPointsAfter(netPlayerHitAttack(kShotOnBody, 100)) < 80);
    CHECK((netPlayerHitAttack(kShotOnBody, 50) & kHitDamageMask) == 4);
    CHECK((netPlayerHitAttack(kHitAttackByPlayer | 1, 25) & kHitDamageMask) == 1); // at least 1
    CHECK((netPlayerHitAttack(kHitAttackByPlayer, 100) & kHitDamageMask) == 0);
}

void puppetFlagsTravel() {
    od::app::WorldPuppet p;
    p.state.pool = 0x20;
    p.state.flags = kActorLiveDrawn | kActorPoseOnRoot;
    p.state.runFlags = kActorRun | kActorShown;
    const auto parts = od::app::encodeWorld({}, 0, 1, {p}, {}, od::app::kWorldPartBytes, nullptr);
    CHECK(parts.size() == 1);
    const auto back = od::app::decodeWorld(parts[0]);
    CHECK(back && back->puppets.size() == 1);
    if (back && back->puppets.size() == 1) {
        CHECK(back->puppets[0].state.flags == p.state.flags);
        CHECK(back->puppets[0].state.runFlags == p.state.runFlags);
    }
}

void travelCarriesStory() {
    od::app::NetEvent e;
    e.kind = od::app::EventKind::Travel;
    e.travel = {0x0b, 1, true, {}};
    e.travel.story.story = 7;
    e.travel.story.nextStory = 8;
    e.travel.story.flags.bytes[0x10] = 0x5a;
    const auto back = od::app::decodeEvent(od::app::encodeEvent(e));
    CHECK(back && back->travel.stage == 0x0b && back->travel.sub == 1 && back->travel.transition);
    if (back) {
        CHECK(back->travel.story.story == 7 && back->travel.story.nextStory == 8);
        CHECK(back->travel.story.flags.bytes[0x10] == 0x5a);
    }
}

void hostStoryShown() {
    gameState().story = 2;
    gameState().nextStory = 2;
    storyFlags() = {};
    setStoryFlag(0x40);
    setStoryFlag(kFlagTransitionArmed); // transient: stays the client's
    NetStory host;
    host.story = 9;
    host.nextStory = 9;
    host.flags.bytes[0x41 >> 3] = uint8_t(1u << (0x41 & 7));
    netHostStory() = host;
    netStoryLoaded(); // a game started (a new game, a load, F2)
    CHECK(gameState().story == 9);
    CHECK(storyFlag(0x41) && !storyFlag(0x40));
    CHECK(storyFlag(kFlagTransitionArmed));
    CHECK(netOwnStory() && netOwnStory()->story == 2);
    CHECK(netOwnStory() && (netOwnStory()->flags.bytes[0x40 >> 3] & (1u << (0x40 & 7))));
    netHostStory().reset();
    netOwnStory().reset();
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    od::bindTestDataSource(std::filesystem::path(argv[1]));
    pvpHitHurts();
    puppetFlagsTravel();
    travelCarriesStory();
    hostStoryShown();
    if (g_failures == 0) std::printf("test_net_world: ok\n");
    return g_failures == 0 ? 0 : 1;
}
