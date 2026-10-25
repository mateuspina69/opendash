// Town walker sim vs. the game. Single-link routes (the mailman) are fully deterministic; on
// branching routes the test replays the game's waypoint choices, which come from the RNG shared with
// every other system of the game.
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "check.hpp"
#include "data_source_binding.hpp"
#include "content/registry.hpp"
#include "game/walker.hpp"

int main(int argc, char** argv) {
    od::content::registerAll(); // the game code dispatches into the content through its registry
    if (argc < 3) return 2;
    od::bindTestDataSource(argv[2]); // the walkers' arc tangent scale
    std::ifstream in(std::filesystem::path(argv[1]) / "walker_ST09.txt");
    CHECK(bool(in));
    std::map<int, od::Route> routes;
    std::map<int, od::Walker> walkers;
    std::map<int, od::Rng> rngs;
    int ticks = 0, mismatches = 0;
    for (std::string line; std::getline(in, line);) {
        std::istringstream ss(line);
        char tag;
        ss >> tag;
        if (tag == 'R') {
            int route, index, n[4], x, z;
            ss >> route >> index >> n[0] >> n[1] >> n[2] >> n[3] >> x >> z;
            od::Waypoint& wp = routes[route][index];
            for (int k = 0; k < 4; ++k) wp.next[size_t(k)] = uint8_t(n[k]);
            wp.x = int16_t(x);
            wp.z = int16_t(z);
        } else if (tag == 'W') {
            int slot, route, cur, prev, flags, speed, turn, yaw, sub, tx, tz;
            od::Walker w;
            ss >> slot >> route >> cur >> prev >> flags >> speed >> turn >> w.x >> w.y >> w.z >> yaw >> sub >> tx >> tz;
            w.route = route;
            w.cur = uint8_t(cur);
            w.prev = uint8_t(prev);
            w.flags = uint8_t(flags);
            w.speed = int16_t(speed);
            w.turn = int16_t(turn);
            w.yaw = int16_t(yaw);
            w.sub = uint8_t(sub);
            w.tx = int16_t(tx);
            w.tz = int16_t(tz);
            walkers[slot] = w;
        } else if (tag == 'T') {
            int tick, slot, yaw, cur;
            int32_t x, z;
            ss >> tick >> slot >> x >> z >> yaw >> cur;
            od::Walker& w = walkers[slot];
            const od::Route& route = routes[w.route];
            bool branching = false;
            for (const auto& [i, wp] : route) branching |= wp.next[1] < 0x80;
            od::stepWalker(w, route, rngs[slot], branching ? cur : -1);
            ticks = tick;
            if (w.x != x || w.z != z || w.yaw != yaw || w.cur != cur) {
                if (++mismatches < 5)
                    std::fprintf(stderr, "tick %d slot %d: sim %d %d %d %d game %d %d %d %d\n", tick, slot, w.x, w.z,
                                 w.yaw, w.cur, x, z, yaw, cur);
            }
        }
    }
    CHECK(walkers.size() == 2);
    CHECK(ticks >= 1000);
    CHECK(mismatches == 0);
    std::printf("%zu walkers, %d ticks, %d mismatches\n", walkers.size(), ticks, mismatches);
    return testResult("test_walker");
}
