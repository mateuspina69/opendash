// Player logic port vs. the game, tick by tick (tests/data/player_ST09.bin -- not in git (*.bin), regenerate:
//   "20 -; 30 up; 10 -; 15 down; 10 -; 20 left; 10 right; 10 -; 1 cross; 40 -; 20 up; 5 up+cross; 40 up; 30 -;
//    20 right+cross; 40 -; 30 down; 20 left+cross;
// ranges listed in the file header). Compares the logic part of the struct (+0x00..+0x2a3; the bone
// pose +0x2a4.. is animation output, ported with the model).
//   test_player <dat dir> <dash2.exe> <tests/data> [max reported failures]
// Extra scenario (diagonals, wall runs, l1/r1 turn in place = sub-state 4), 914 ticks, also 0 failing:
//   --script "10 -; 60 up+left; 40 up; 3 up+cross; 30 up; 50 left; 5 left+cross; 30 left; 60 down+right;
//   2 cross; 30 -; 40 right; 1 right+cross; 20 right; 30 down; 3 down+cross; 30 -; 80 up; 40 up+right;
//   20 l1; 20 r1; 30 -; 120 up; 60 left; 100 down"  (write it to another dir and pass that as <tests/data>)
// --script ... --out ../opendash/tests/data/player_ST<S>_<name>.bin), see docs/player.md:
//   ST09_walk      "10 -; 30 up+circle; 10 circle; 20 down+circle; 10 -; 20 up+circle+left; 15 up+circle+right;
//                   10 up+circle; 5 down; 10 -; 20 down+circle; 5 up+circle; 10 -"
//   ST09_sidestep  "10 -; 20 left+circle; 20 right+circle; 10 left+right+circle; 10 -; 10 circle; 15 circle+right;
//                   10 circle+right+up; 10 -"
//   ST09_quickturn "10 -; 30 up; 1 up+circle; 10 right; 1 right+circle; 20 right; 10 -; 10 down; 1 down+circle;
//                   12 down; 10 -"
//   ST09_look      "10 -; 30 l2; 20 l2+left; 10 l2+up; 15 l2+down; 10 l2+right; 20 -"
//   ST09_talk      --teleport -204 0 -1117 "5 -; 1 circle; 60 -; 1 circle; 30 -; 1 circle; 30 -; 1 circle; 30 -;
//                   1 circle; 30 -; 1 circle; 60 -"
//   ST09_skate     --poke 0xa71ec3 1 "10 -; 20 circle; 30 circle+up; 10 circle+up+left; 10 circle; 10 -; 30 circle;
//                   5 circle+cross; 30 circle; 15 circle+right; 10 circle+left+down; 30 -"
//   ST09_skate_fall  --poke 0xa71ec3 1 --teleport -2816 -384 -7100 "5 -; 60 circle; 30 -"
//   ST09_skate_fall2 (same) "5 -; 25 circle; 10 up; 40 -"
//   ST13_hurt      --stage 13 --teleport -2688 -192 -1124 "5 -; 80 -; 20 up; 60 -"   (enemies hit the player)
//   ST13_hurt2     --stage 13 --teleport -640 -192 3612 (same script)
//   ST13_recover   --stage 13 --teleport -2688 -192 -1124 --poke 0xa71ec2 2 "9 -; 30 left; 30 -; 30 right; 60 -"
//   ST13_death     --stage 13 --teleport -2688 -192 -1124 --poke 0xa71d90 0 --poke 0xa71d91 0 "5 -; 140 -"
// Actor collision (FUN_004140e0, game/actor_collision.cpp; door actor slot 0 of the 0xa67860 pool at
// (-2560, -192, -4608), yaw 0x800, a 512x512 step slab + the door leaf as its collision mesh):
//   ST13_ledge     --stage 13 --teleport -2560 -192 -4500 "20 -"    (standing on the door step: +0x52 = 1)
//   ST13_door_push --stage 13 --teleport -2560 -192 -4500 "5 -; 40 down; 30 -"   (pushed back by the leaf)
//   ST13_door_step --stage 13 --teleport -2560 -192 -4500 "5 -; 20 up; 10 -; 40 down; 10 -"
//                  (walks off the step, tick 12 +0x52 1 -> 0, back onto it, tick 50 0 -> 1, then into the leaf)
// PLG3 goldens also carry the pass inputs captured at the
// entry of FUN_004140e0; the pass is run on them and compared with "player before 0x574d30". All goldens
// above with a recipe are PLG3; ST08_exit and ST09_ledge_* (no recorded recipe) are still PLG2 (no pass
// check). ST09_quickturn ticks 55/56 hit a walking NPC (type 0 solid, FUN_00414690) in its rotated frame.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "check.hpp"
#include "translator/state_translator.hpp"
#include "data_source_binding.hpp"
#include "content/registry.hpp"
#include "game/actor_collision.hpp"
#include "engine/dat.hpp"
#include "translator/gamemem.hpp"
#include "translator/game_memory.hpp"
#include "game/actor_movement.hpp"
#include "game/modern_play.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_update.hpp"
#include "engine/room.hpp"

namespace fs = std::filesystem;
using od::port::kPlayer;

namespace {
constexpr uint32_t kLogic = 0x2a4;
// +0x86/+0x87: pad short of the actor matrix (+0x74); Maybe_MulMatrix3x3 copies stack garbage into it
bool ignored(uint32_t o) { return o == 0x86 || o == 0x87; }

struct Range {
    uint32_t addr, size;
};

int compare(const char* step, uint32_t tick, const uint8_t* before, const uint8_t* want, const od::GameMem& m,
            int report) {
    int diffs = 0;
    for (uint32_t o = 0; o < kLogic; ++o) {
        const uint8_t got = m.u8(kPlayer + o);
        if (got == want[o] || ignored(o)) continue;
        if (diffs < report)
            std::fprintf(stderr, "  %s tick %u +0x%03x: before %02x want %02x got %02x\n", step, tick, o, before[o],
                         want[o], got);
        ++diffs;
    }
    return diffs;
}
} // namespace

int main(int argc, char** argv) {
    od::content::registerAll(); // the game code dispatches into the content through its registry
    if (argc < 4) return 2;
    const fs::path datDir = argv[1], exe = argv[2], data = argv[3];
    od::bindTestDataSource(exe);
    od::port::setModernPlay(false); // the goldens hold the exe's gameplay (game/modern_play)
    const int maxReports = argc > 4 ? std::stoi(argv[4]) : 3;
    // every golden scenario player_ST<stage>[_name].bin (locomotion, ledge grab, exits ...); the room file is
    // ST<stage>.DAT, the request block 0xa6d2e0 is compared with the one captured at the next tick
    std::vector<fs::path> files;
    for (const auto& de : fs::directory_iterator(data))
        if (de.path().extension() == ".bin" && de.path().filename().string().rfind("player_ST", 0) == 0)
            files.push_back(de.path());
    std::sort(files.begin(), files.end());
    CHECK(!files.empty());
    auto runFile = [&](const fs::path& path) -> bool {
        const std::string name = path.filename().string();
        const auto file = od::readFile(path);
        const bool plg3 = file.size() > 16 && std::memcmp(file.data(), "PLG3", 4) == 0;
        if (file.size() <= 16 || (!plg3 && std::memcmp(file.data(), "PLG2", 4) != 0)) {
            std::fprintf(stderr, "%s: not a PLG2/PLG3 golden\n", name.c_str());
            return false;
        }
        const uint32_t count = od::rd32(file.data() + 4), size = od::rd32(file.data() + 8), nr = od::rd32(file.data() + 12);
        std::vector<Range> ranges, passRanges;
        size_t p = 16;
        for (uint32_t i = 0; i < nr; ++i, p += 8) ranges.push_back({od::rd32(file.data() + p), od::rd32(file.data() + p + 4)});

        od::GameMem m;
        const od::ScopedGameMemory bound(m);
        m.mapExeData(exe);
        m.mapDatPsx(od::readFile(datDir / "PL00P000.DAT"));
        m.mapZero(kPlayer, size);
        for (const Range& r : ranges) m.mapZero(r.addr, r.size);
        if (plg3) { // the actor collision pass inputs (FUN_004140e0 entry) + the solids' collision meshes
            const uint32_t np = od::rd32(file.data() + p);
            p += 4;
            for (uint32_t i = 0; i < np; ++i, p += 8)
                passRanges.push_back({od::rd32(file.data() + p), od::rd32(file.data() + p + 4)});
            for (const Range& r : passRanges) m.mapZero(r.addr, r.size);
            const uint32_t ns = od::rd32(file.data() + p);
            p += 4;
            for (uint32_t i = 0; i < ns; ++i) {
                const uint32_t a = od::rd32(file.data() + p), n = od::rd32(file.data() + p + 4);
                m.mapZero(a, n);
                m.write(a, file.data() + p + 8, n);
                p += 8 + n;
            }
        }
        size_t rec = 6 + 4 * size_t(size);
        for (const Range& r : ranges) rec += r.size;
        for (const Range& r : passRanges) rec += r.size;
        const auto st = od::pcGameFile(name.substr(7, 4) + ".DAT", od::readFile(datDir / (name.substr(7, 4) + ".DAT")));
        const auto block = od::roomBlock(st);
        od::port::PlayerEnv env;
        od::test::loadNativeState(m); // the native state from this golden's image (nothing of the previous one)

        int failed = 0, passed = 0, reported = 0;
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* r = file.data() + p + size_t(i) * rec;
            const uint32_t tick = od::rd32(r);
            const uint8_t* snap[4];
            for (int k = 0; k < 4; ++k) snap[k] = r + 6 + size_t(k) * size;
            const uint8_t* q = r + 6 + 4 * size_t(size);
            od::test::storeNativeState(m);
            for (const Range& rg : ranges) {
                m.write(rg.addr, q, rg.size);
                q += rg.size;
            }
            if (i == 0) { // the active grid from the runtime state (0x6d0094/95 origin)
                const int x0 = m.u8(0x6d0094), z0 = m.u8(0x6d0095);
                for (const od::RoomGrid& g : od::roomGrids(block))
                    if (g.x0 == x0 && g.z0 == z0) env.room = od::collisionRoom(block, g);
                CHECK(env.room.w == m.u8(0x6d0098));
            }
            bool ok = true;
            const int rep = reported < maxReports ? 12 : 0;
            try {
                m.write(kPlayer, snap[0], size);
                od::test::loadNativeState(m);
                od::port::actorCollisionReset();
                od::port::playerInputAndDispatch(env);
                od::test::storeNativeState(m);
                ok = compare("dispatch", tick, snap[0], snap[1], m, rep) == 0 && ok;
                if (plg3) { // FUN_004140e0 on the game's queues / actors / player as captured at its entry
                    auto queued = [&]() {
                        const od::port::CollisionQueues& cq = od::port::actorCollisionQueues();
                        for (uint32_t k = 0; k < cq.moverCount; ++k)
                            if (cq.movers[k].get() == &od::port::player()) return true;
                        return false;
                    };
                    const bool portQueued = queued(); // FUN_004140b0 from the player's state handler
                    for (const Range& rg : passRanges) {
                        m.write(rg.addr, q, rg.size);
                        q += rg.size;
                    }
                    od::test::loadNativeState(m);
                    if (portQueued != queued()) {
                        if (rep)
                            std::fprintf(stderr, "  tick %u: player %s the movers queue\n", tick,
                                         portQueued ? "wrongly in" : "missing from");
                        ok = false;
                    }
                    const std::vector<uint8_t> atPass(m.view(kPlayer, size).begin(), m.view(kPlayer, size).end());
                    od::port::actorCollision();
                    od::test::storeNativeState(m);
                    ok = compare("pass", tick, atPass.data(), snap[2], m, rep) == 0 && ok;
                }
                m.write(kPlayer, snap[2], size);
                const bool requestIdle = m.u8(0xa6d2e0) == 0; // afterwards the game-mode code drives it
                od::test::loadNativeState(m);
                od::port::playerUpdate(env);
                od::test::storeNativeState(m);
                ok = compare("update", tick, snap[2], snap[3], m, rep) == 0 && ok;
                if (requestIdle && i + 1 < count) { // arming of the transition request vs the next tick's state
                    const uint8_t* nq = file.data() + p + size_t(i + 1) * rec + 6 + 4 * size_t(size);
                    for (const Range& rg : ranges) {
                        if (rg.addr == 0xa6d2e0)
                            for (uint32_t o = 0; o < rg.size; ++o)
                                if (m.u8(rg.addr + o) != nq[o]) {
                                    if (rep)
                                        std::fprintf(stderr, "  tick %u request +0x%02x: want %02x got %02x\n", tick, o,
                                                     nq[o], m.u8(rg.addr + o));
                                    ok = false;
                                }
                        nq += rg.size;
                    }
                }
            } catch (const std::exception& e) {
                if (reported < maxReports) std::fprintf(stderr, "  tick %u: %s\n", tick, e.what());
                ok = false;
            }
            if (!ok) {
                ++failed;
                ++reported;
            } else {
                ++passed;
            }
        }
        std::printf("%s: %u ticks: %d ok, %d failing\n", name.c_str(), count, passed, failed);
        return failed == 0;
    };
    for (const fs::path& f : files) CHECK(runFile(f));
    return testResult("test_player");
}
