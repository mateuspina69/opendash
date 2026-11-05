// The coordinator's worlds with real processes (app/world_host, app/world_link, app/world_process): a player enters a
// room and its world starts; a second player shares it; a player moves to another room and a second world starts; the
// last player out stops a world; a world whose coordinator goes silent ends by itself. No window, no sound device.
//   test_world_host <opendash.exe> [<save image>]
#include <SDL3/SDL.h>

#include <cstdio>
#include <functional>
#include <vector>

#include "app/world_host.hpp"
#include "app/world_link.hpp"
#include "check.hpp"
#include "game/player/player_snapshot.hpp"

using namespace od::app;

namespace {

constexpr WorldRoom kRoomA{0x1E, 0}, kRoomB{0x08, 0};

std::vector<uint8_t> stateAt(int x) {
    od::port::RemotePlayerState s;
    s.room = {0x1E, 0, true};
    s.snapshot.pos[0] = x * 0x10000;
    s.snapshot.drawFlags[0] = 3;
    s.snapshot.drawFlags[1] = 3;
    return od::port::encodePlayerState(s);
}

// polls until `done` or `seconds` have passed
bool waitFor(WorldHost& host, double seconds, const std::function<bool()>& done) {
    const uint64_t until = SDL_GetTicksNS() + uint64_t(seconds * 1e9);
    while (SDL_GetTicksNS() < until) {
        host.poll(SDL_GetTicksNS());
        if (done()) return true;
        SDL_DelayNS(5'000'000);
    }
    return false;
}

void testCodecAndLink() {
    WorldMessage m{WorldMessageType::Report};
    m.tick = 123456789012ull;
    m.liveActors = 42;
    m.players = 3;
    m.digest = 0xfeedfacecafebeefull;
    const auto back = decodeWorldMessage(encodeWorldMessage(m));
    CHECK(back && back->type == m.type && back->tick == m.tick && back->liveActors == 42 && back->players == 3 &&
          back->digest == m.digest);
    const std::vector<uint8_t> bad = {0x7f};
    CHECK(!decodeWorldMessage(bad));

    // what must arrive arrives once and in order; what the next one replaces goes through too
    WorldLink coordinator;
    WorldLink world(coordinator.port());
    world.sendReliable({WorldMessageType::Hello});
    WorldMessage join{WorldMessageType::Join};
    join.playerId = 5;
    join.playerState = {1, 2, 3};
    uint64_t now = 1'000'000;
    std::vector<WorldMessage> atCoordinator, atWorld;
    bool joined = false;
    for (int i = 0; i < 400 && (atWorld.size() < 2 || !coordinator.settled() || !world.settled()); ++i) {
        now += 5'000'000;
        for (auto& x : coordinator.poll(now)) atCoordinator.push_back(std::move(x));
        if (coordinator.hasPeer() && !joined) {
            joined = true;
            coordinator.sendReliable(join);
            coordinator.sendLatest({WorldMessageType::KeepAlive});
        }
        for (auto& x : world.poll(now)) atWorld.push_back(std::move(x));
        SDL_DelayNS(1'000'000);
    }
    CHECK(atCoordinator.size() == 1 && atCoordinator[0].type == WorldMessageType::Hello);
    CHECK(atWorld.size() == 2);
    bool sawJoin = false;
    for (const WorldMessage& x : atWorld)
        if (x.type == WorldMessageType::Join) sawJoin = x.playerId == 5 && x.playerState == join.playerState;
    CHECK(sawJoin);
    CHECK(coordinator.settled() && world.settled());
    CHECK(!world.silent(now));
    CHECK(world.silent(now + WorldLink::kSilenceNs + 1));
}

void testWorlds(const char* exe, const char* save) {
    WorldHost host(exe, save ? save : "");
    CHECK(host.worldCount() == 0);

    host.enter(1, kRoomA, stateAt(100));
    CHECK(host.worldCount() == 1);
    CHECK(waitFor(host, 30, [&] { const auto r = host.report(kRoomA); return r && r->players == 1 && r->tick > 5; }));

    // a second player shares the world; the tick goes on
    host.enter(2, kRoomA, stateAt(300));
    CHECK(host.worldCount() == 1);
    CHECK(waitFor(host, 10, [&] { const auto r = host.report(kRoomA); return r && r->players == 2; }));
    const uint64_t tick = host.report(kRoomA) ? host.report(kRoomA)->tick : 0;
    host.state(1, stateAt(150));
    CHECK(waitFor(host, 10, [&] { const auto r = host.report(kRoomA); return r && r->tick > tick + 10; }));

    // one moves to another room: a second world, the first keeps its other player
    host.enter(1, kRoomB, stateAt(0));
    CHECK(host.worldCount() == 2);
    CHECK(host.roomOf(1) == kRoomB && host.roomOf(2) == kRoomA);
    CHECK(waitFor(host, 30, [&] {
        const auto a = host.report(kRoomA), b = host.report(kRoomB);
        return a && b && a->players == 1 && b->players == 1;
    }));

    // the last out of a room stops its world
    host.leave(2);
    CHECK(host.worldCount() == 1);
    CHECK(!host.report(kRoomA));
    CHECK(host.report(kRoomB).has_value());
    CHECK(!host.roomOf(2));

    // back into the emptied room: a world of its own again
    host.enter(2, kRoomA, stateAt(100));
    CHECK(host.worldCount() == 2);
    CHECK(waitFor(host, 30, [&] { const auto r = host.report(kRoomA); return r && r->players == 1; }));
}

// a world whose coordinator goes silent ends by itself: nothing outlives the session
void testNoOrphan(const char* exe, const char* save) {
    WorldLink link;
    std::vector<const char*> args = {exe, "--world", "1E", "0", "--world-link"};
    char port[8];
    std::snprintf(port, sizeof port, "%u", unsigned(link.port()));
    args.push_back(port);
    if (save) {
        args.push_back("--story-save");
        args.push_back(save);
    }
    args.push_back(nullptr);
    SDL_Process* world = SDL_CreateProcess(args.data(), false);
    CHECK(world != nullptr);
    if (!world) return;
    // heard once, so that the world knows it had a coordinator; then silence
    bool heard = false;
    for (uint64_t until = SDL_GetTicksNS() + 30'000'000'000ull; !heard && SDL_GetTicksNS() < until;) {
        heard = !link.poll(SDL_GetTicksNS()).empty();
        SDL_DelayNS(5'000'000);
    }
    CHECK(heard);
    bool ended = false;
    int code = -1;
    for (uint64_t until = SDL_GetTicksNS() + 15'000'000'000ull; !ended && SDL_GetTicksNS() < until; SDL_DelayNS(50'000'000))
        ended = SDL_WaitProcess(world, false, &code);
    CHECK(ended);
    if (!ended) SDL_KillProcess(world, true);
    SDL_DestroyProcess(world);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "test_world_host <opendash.exe> [<save image>]\n");
        return 2;
    }
    SDL_Init(0);
    const char* save = argc > 2 ? argv[2] : nullptr;
    testCodecAndLink();
    testWorlds(argv[1], save);
    testNoOrphan(argv[1], save);
    SDL_Quit();
    return testResult("test_world_host");
}
