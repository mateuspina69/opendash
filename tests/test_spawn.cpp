// The stage / room setup and the room scripts (game/stage_setup, game/room_scripts, game/spawn) vs. the game
// below). Each golden holds the game state around the stage setup
// (points 0 / 1), the room setup FUN_005d5b30 (2 / 3, aux = its parameter) and Maybe_TickRoomScripts (4 / 5) from
// the stage load on. For every pair the test loads the state before, runs the port and compares:
//   stage setup:  the per-stage pointers the stage function writes (class tables, routes, kinds, voices, callbacks,
//                 FUN_0050f280's screen-effect slots)
//   room setup:   killAllActors (param >= 0) + roomSetupActors: the six actor pools and their masks, the bone blocks,
//                 the room-script state 0xa714e0.., the block 0xa71740, the event flags, 0xa71808 / 0xa7180c, the
//                 screen-effect slots and the exe data the room function patches (0x8ea0c0..). Not the player (the
//                 spawn record places it, then FUN_005d5b30 re-initialises it: game/transition), not the camera.
//   room scripts: the per-stage handler (roomScriptHandler) + game/transition's flag-0x700 door test, same ranges
//                 plus the script block 0xa725a0.
// Every pair must be covered by the port (spawnCoverage) -- the goldens are the stages the port claims.
//   test_spawn <dat dir> <dash2.exe> <tests/data> [name filter]
// Recipes:
//   spawn_ST09_s00.bin      --stage 09 --script "30 -"                    (town, user save 00: story 1)
//   spawn_ST09_new.bin      --stage 09 --new --script "30 -"              (town, new game: story 0)
//   spawn_ST13_s00.bin      --stage 13 --script "30 -"                    (enemies: spawner type 0xa, door actor)
//   spawn_ST20_s02.bin      --stage 20 --save ".../DASH2-02.DAT" --script "30 -" (Calbania town's room function at
//                           the save's story; ST1F / ST23 the same way stop on a GameMem read of 0x300: open)
//   spawn_ST30_0_f21.bin    --stage 30 --sub 0 --save ".../DASH2-02.DAT" --flag 0x21 --extra 0x7bdf4c 0x400
//                           --script "30 -"; spawn_ST30_2.bin the same with --sub 2, no flag (Glyde's base, room function
//                           0x5db600 = template + helper 0x5852f0; --extra = ST30's segment table *0x6d008c: the cells)
//   spawn_ST24_s02.bin      --stage 24 --save "C:/Program Files (x86)/Capcom/Dash2/DASH2-02.DAT" --script "30 -"
//                           (story 3: the room script spawns the 6 actors of 0x8c8e80, among them the type-0x30
//                           solid the player stops at)
//   spawn_ST0F_passage.bin  --stage 0F --sub 1 --teleport 0 0 1408 --yaw 2048 --warmup 59
//                           --script "5 -; 4 circle; 90 -"   (exit 1 -> sub 2: the destination room's actors)
//   spawn_ST04_s01.bin      --stage 04 --save "C:/Program Files (x86)/Capcom/Dash2/DASH2-01.DAT" --script "30 -"
//                           (save 01's room: helpers FUN_0057bbc0 / FUN_005d6a80, room script FUN_0057be10 + 0057bc40)
//   spawn_ST1D_s00.bin      --stage 1D --sub 1 --script "30 -"   (save 00's room: helper FUN_005803a0, the byte
//                           0x8e6b8f, room script FUN_005805f0 spawning 6 actors)
//   spawn_ST10_1_s01.bin   --stage 10 --sub 1 --save ".../DASH2-01.DAT" --script "30 -" (the
//                           landing field: helper FUN_0057dd60, room script FUN_0057dfb0; sub 0 (FUN_0057dec0) reads
//                           the encounter group table, which this harness does not carry: unmapped read, not listed)
//   spawn_ST4D_layA.bin     --stage 4D --seed 0x414c343c --script "10 -"   (the encounter layouts, actors.md 4b:
//                           ST4D B 0xd8f16adf, C 0x91b7584a; ST4E A 0x414c343c, B 0xd8f16adf, C 0x2265b1f5;
//                           ST4F A 0xa6cecc1b, B 0xcd613e30, C 0x7ed4d57b, Crare 0x18072e8c; ST50 A 0xd8f16adf,
//                           B 0x2265b1f5, C 0x414c343c, Crare 0x414c343c --flag 0xb6)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "check.hpp"
#include "translator/state_translator.hpp"
#include "data_source_binding.hpp"
#include "content/registry.hpp"
#include "engine/dat.hpp"
#include "translator/gamemem.hpp"
#include "engine/room.hpp"
#include "translator/game_memory.hpp"
#include "game/room_scripts.hpp"
#include "game/spawn.hpp"
#include "game/stage_setup.hpp"
#include "game/actor_sweeps.hpp"
#include "game/script_engine.hpp"

namespace fs = std::filesystem;

namespace {

struct Range {
    uint32_t addr, size;
};
struct Rec {
    uint32_t tick = 0, aux = 0;
    uint8_t point = 0;
    std::vector<uint8_t> blob;
};
struct Golden {
    std::vector<Range> ranges;
    std::vector<Rec> recs;
};

bool readGolden(const fs::path& path, Golden& g) {
    const auto f = od::readFile(path);
    if (f.size() < 12 || std::memcmp(f.data(), "SPN1", 4) != 0) return false;
    size_t p = 4, total = 0;
    const uint32_t nr = od::rd32(f.data() + p);
    p += 4;
    for (uint32_t i = 0; i < nr; ++i, p += 8) {
        g.ranges.push_back({od::rd32(f.data() + p), od::rd32(f.data() + p + 4)});
        total += g.ranges.back().size;
    }
    const uint32_t count = od::rd32(f.data() + p);
    p += 4;
    std::vector<uint8_t> prev(total, 0);
    for (uint32_t i = 0; i < count; ++i) {
        Rec r;
        r.tick = od::rd32(f.data() + p);
        r.point = f[p + 4];
        r.aux = od::rd32(f.data() + p + 8);
        const uint32_t enc = od::rd32(f.data() + p + 12);
        p += 16;
        r.blob = prev;
        size_t o = 0;
        const size_t end = p + enc;
        while (p < end) {
            const uint32_t z = od::rd16(f.data() + p), l = od::rd16(f.data() + p + 2);
            p += 4;
            o += z;
            for (uint32_t k = 0; k < l; ++k) r.blob[o + k] ^= f[p + k];
            o += l;
            p += l;
        }
        prev = r.blob;
        g.recs.push_back(std::move(r));
    }
    return true;
}

const std::vector<Range> kStageCompare = {
    {0xa6d300, 4}, {0xa6bb8c, 4}, {0xa6bb94, 4}, {0xa6bc58, 4}, {0xa72588, 4}, {0xa6c650, 4}, {0xa6bf78, 4},
    {0xa714c8, 4}, {0xa7258c, 4}, {0x6d00ac, 4}, {0xa6c420, 4}, {0xa6c424, 4}, {0xa6c42c, 4}, {0xa72244, 4},
    {0xa5f268, 4}, {0xa6ab60, 4}, {0xa72240, 4}, {0xa72584, 4}, {0xa72230, 4}, {0xa6bb80, 4}, {0xa6d100, 0x60},
    {0xa6d1dc, 0x14},
    {0x6d0000, 4}, {0xa73cd6, 2}}; // the RNG and the layout flags 0xb0..0xbb of the sub-ruin setups (game/encounter)
const std::vector<Range> kRoomCompare = {
    {0xa67860, 0x40 * 0xcc}, {0xa6e3a0, 0x18 * 0x16c}, {0xa6ab80, 0x10 * 0x100}, {0xa78420, 0x20 * 0xdc},
    {0xa6c6e0, 0x40 * 0x28}, {0xa6bfa0, 0x20 * 0x20},
    {0xa6bd84, 4}, {0xa6ab64, 4}, {0xa743ec, 4}, {0xa78400, 4}, {0xa711a4, 4}, {0xa79fa0, 4}, {0xa79fa8, 8},
    {0xa6c418, 8}, {0xa71120, 8}, {0xa79ff0, 8}, {0xa71754, 4}, {0xa6d174, 4},
    {0xa72660, 0x1600}, {0xa6bb90, 4},
    {0xa714e0, 0x88}, {0xa71740, 0x14}, {0xa73cc0, 0x720}, {0xa71808, 8}, {0x8ea0c0, 0xe0}, {0x8e6b80, 0x20}, {0x8d6830, 0x20}, {0xa6d100, 0x60},
    {0xa6d1ec, 4}, {0xa72584, 4}};

struct Runner {
    od::GameMem& m;
    const Golden& g;
    fs::path datDir;
    int reported = 0;
    int compared = 0;
    uint32_t roomKey = 0xffffffff;

    void load(const Rec& r) {
        od::test::storeNativeState(m);
        size_t o = 0;
        for (const Range& rg : g.ranges) {
            m.write(rg.addr, r.blob.data() + o, rg.size);
            o += rg.size;
        }
        bindRoom();
        od::test::loadNativeState(m);
    }
    // the active grid's room block where the game had it in PS1 RAM (FUN_0058d5f0 switches its segments)
    void bindRoom() {
        const uint32_t pool = m.u32(0x6d0084);
        if (pool < 0x630000 || pool >= 0x830000) return;
        const int stage = m.u8(0xa6d190);
        const uint32_t key = uint32_t(stage) << 24 ^ pool ^ (m.u32(0x6d0088) * 0x9e3779b1u);
        if (key == roomKey) return;
        char name[16];
        std::snprintf(name, sizeof name, "ST%02X.DAT", stage);
        const od::GameFile file = od::pcGameFile(name, od::readFile(datDir / name));
        const auto block = od::roomBlock(file);
        for (const od::RoomGrid& gr : od::roomGrids(block)) {
            if (gr.x0 != m.u8(0x6d0094) || gr.z0 != m.u8(0x6d0095)) continue;
            const od::CollisionRoom probe = od::collisionRoom(block, gr);
            const uint32_t base = pool - uint32_t(probe.pool);
            if (base + probe.segments != m.u32(0x6d008c) || base + probe.cells != m.u32(0x6d0088)) continue;
            m.map(base, block);
            break;
        }
        roomKey = key;
    }
    bool golden(const Rec& r, uint32_t a, uint8_t& v) const {
        size_t o = 0;
        for (const Range& rg : g.ranges) {
            if (a >= rg.addr && a < rg.addr + rg.size) {
                v = r.blob[o + (a - rg.addr)];
                return true;
            }
            o += rg.size;
        }
        return false; // not captured by this golden
    }
    int compare(const Rec& after, const std::vector<Range>& what, const char* phase) {
        od::test::storeNativeState(m);
        int bad = 0;
        for (const Range& rg : what)
            for (uint32_t i = 0; i < rg.size; ++i) {
                const uint32_t a = rg.addr + i;
                uint8_t want = 0;
                if (!golden(after, a, want)) continue;
                const uint8_t got = m.u8(a);
                if (got == want) continue;
                if (reported < 12)
                    std::fprintf(stderr, "  tick %u %s: 0x%08x = %02x, game %02x\n", after.tick, phase, a, got, want);
                ++reported;
                ++bad;
            }
        ++compared;
        return bad;
    }
};

int runGolden(const fs::path& path, const fs::path& datDir, const fs::path& exe) {
    Golden g;
    if (!readGolden(path, g)) {
        std::printf("%s: missing (regenerate, see the recipes) -- skipped\n", path.filename().string().c_str());
        return 0;
    }
    od::GameMem m;
    const od::ScopedGameMemory bound(m);
    m.mapExeData(exe);
    m.mapDatPsx(od::readFile(datDir / "PL00P000.DAT")); // 0x740871 (the player spawn record reads it)
    for (const Range& rg : g.ranges)
        if (!m.mapped(rg.addr, rg.size)) m.mapZero(rg.addr, rg.size);
    for (auto [a, n] : {std::pair<uint32_t, uint32_t>{0xa7b088, 8}, {0xa67844, 4}})
        if (!m.mapped(a, n)) m.mapZero(a, n);
    od::test::loadNativeState(m); // the native state from this golden's image (nothing of the previous one)
    Runner r{m, g, datDir};
    int bad = 0, stages = 0, rooms = 0, scripts = 0, spawned = 0;
    for (size_t i = 0; i + 1 < g.recs.size(); ++i) {
        const Rec& a = g.recs[i];
        const Rec& b = g.recs[i + 1];
        if (std::getenv("SPAWN_TRACE")) std::fprintf(stderr, "rec %zu tick %u point %u\n", i, a.tick, a.point);
        if (a.point == 0 && b.point == 1) {
            r.load(a);
            const od::port::SpawnCoverage c = od::port::spawnCoverage();
            if (!c.stage) {
                std::fprintf(stderr, "  tick %u: stage setup not covered: %s\n", a.tick, c.why.c_str());
                ++bad;
                continue;
            }
            od::port::stageSetup();
            bad += r.compare(b, kStageCompare, "stage setup");
            ++stages;
        } else if (a.point == 2 && b.point == 3) {
            r.load(a);
            // the grid header 0x6d0084..0x6d009b is written by the room's geometry load (FUN_005f59d0, the template's
            // first call; the app's renderer side, done before game/stage_setup): taken from the record after the setup
            if (m.u32(0x6d0088) == 0) {
                od::test::storeNativeState(m);
                for (uint32_t ad = 0x6d0084; ad < 0x6d009c; ++ad) {
                    uint8_t v = 0;
                    if (r.golden(b, ad, v)) m.set8(ad, v);
                }
                r.bindRoom();
                od::test::loadNativeState(m);
            }
            const int32_t param = int32_t(a.aux);
            if (param >= 0) od::port::killAllActors();
            const od::port::SpawnCoverage c = od::port::spawnCoverage();
            if (!c.room) {
                std::fprintf(stderr, "  tick %u: room setup not covered: %s\n", a.tick, c.why.c_str());
                ++bad;
                continue;
            }
            od::port::roomSetupActors(param);
            std::vector<Range> what = kRoomCompare;
            for (const Range& rg : g.ranges) // --extra ranges in PS1 RAM (the room block: switched cells)
                if (rg.addr >= 0x700000 && rg.addr < 0x830000) what.push_back(rg);
            bad += r.compare(b, what, "room setup");
            ++rooms;
        } else if (a.point == 4 && b.point == 5) {
            r.load(a);
            if (m.u8(0xa725a0) == 0) {
                const od::port::SpawnCoverage c = od::port::spawnCoverage();
                if (!c.roomScript) {
                    std::fprintf(stderr, "  tick %u: room script not covered: %s\n", a.tick, c.why.c_str());
                    ++bad;
                    continue;
                }
                const uint32_t before = m.u32(0xa71564);
                od::port::roomScriptHandler();
                od::test::storeNativeState(m);
                // game/transition roomScripts' door test (every stage of these goldens has one but ST13)
                if (m.u8(0xa6d190) != 0x13 && m.u8(0xa725a0) == 0 && (m.u8(0xa73cc0 + (0x700 >> 3)) & 1))
                    od::port::scriptRun(0);
                od::test::storeNativeState(m);
                spawned += m.u32(0xa71564) != before;
            }
            std::vector<Range> what = kRoomCompare;
            what.push_back({0xa725a0, 0xc0});
            bad += r.compare(b, what, "room scripts");
            ++scripts;
        }
    }
    std::printf("%s: %d stage setups, %d room setups, %d room-script ticks (%d spawning), %d byte(s) differ\n",
                path.filename().string().c_str(), stages, rooms, scripts, spawned, bad);
    CHECK(rooms > 0);
    return bad;
}

} // namespace

int main(int argc, char** argv) {
    od::content::registerAll(); // the game code dispatches into the content through its registry
    if (argc < 4) {
        std::fprintf(stderr, "usage: test_spawn <dat dir> <dash2.exe> <tests/data>\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        const fs::path datDir = argv[1], exe = argv[2], data = argv[3];
        od::bindTestDataSource(exe);
        for (const char* name : {"spawn_ST09_s00.bin", "spawn_ST09_new.bin", "spawn_ST13_s00.bin", "spawn_ST24_s02.bin",
                                 "spawn_ST0F_passage.bin", "spawn_ST04_s01.bin", "spawn_ST1D_s00.bin",
                                 "spawn_ST10_1_s01.bin",
                                 // Calbania (content/calbania/calbania_rooms helpers 0x580ce0 / 0x5812f0 / 0x581650, ST1F weapon / callback)
                                 "spawn_ST20_s02.bin",
                                 // Glyde's base (helper 0x5852f0: flag 0x7d3 in sub 0, the cells switched with flag 0x21)
                                 "spawn_ST30_0_f21.bin", "spawn_ST30_2.bin",
                                 // the encounter layouts the sub-ruin setups draw (game/encounter, --seed)
                                 "spawn_ST4D_layA.bin", "spawn_ST4D_layB.bin", "spawn_ST4D_layC.bin", "spawn_ST4E_layA.bin",
                                 "spawn_ST4E_layB.bin", "spawn_ST4E_layC.bin", "spawn_ST4F_layA.bin", "spawn_ST4F_layB.bin",
                                 "spawn_ST4F_layC.bin", "spawn_ST4F_layCrare.bin", "spawn_ST50_layA.bin",
                                 "spawn_ST50_layB.bin", "spawn_ST50_layC.bin", "spawn_ST50_layCrare.bin"})
            if (argc < 5 || std::strstr(name, argv[4])) CHECK(runGolden(data / name, datDir, exe) == 0);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "test_spawn: %s\n", e.what());
        return 1;
    }
    return testResult("test_spawn");
}
