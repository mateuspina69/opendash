// Player pose port vs. the game: for every tick of tests/data/player_ST09.bin (see test_player.cpp for how
// to regenerate it), run playerPose on the struct the game ended the tick with and compare the actor matrix
// (+0x74) and the bone matrices / points (+0x2b0..+0x4ff). Matrix pad shorts are skipped: the game's
// Maybe_MulMatrix3x3 copies 5 dwords and fills them with stack garbage.
//   test_player_pose <dat dir> <dash2.exe> <tests/data>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "check.hpp"
#include "translator/state_translator.hpp"
#include "data_source_binding.hpp"
#include "plg.hpp"
#include "content/registry.hpp"
#include "engine/dat.hpp"
#include "translator/gamemem.hpp"
#include "translator/game_memory.hpp"
#include "game/player/player_pose.hpp"
#include "game/player/player_state.hpp"

namespace fs = std::filesystem;

namespace {
constexpr uint32_t kPlayer = 0xa71d20;

bool isPad(uint32_t o) {
    if (o == 0x86 || o == 0x87) return true;
    if (o >= 0x2b0 && o < 0x2b0 + 15 * 0x20) {
        const uint32_t k = (o - 0x2b0) % 0x20;
        return k == 0x12 || k == 0x13;
    }
    if (o >= 0x490) return (o - 0x490) % 8 >= 6; // bone point pad (never written)
    return false;
}
} // namespace

int main(int argc, char** argv) {
    od::content::registerAll(); // the game code dispatches into the content through its registry
    if (argc < 4) return 2;
    const fs::path datDir = argv[1], exe = argv[2], data = argv[3];
    od::bindTestDataSource(exe);
    const auto file = od::readFile(data / "player_ST09.bin");
    CHECK(plgMagic(file));
    if (g_failures) return testResult("test_player_pose");
    const uint32_t count = od::rd32(file.data() + 4), size = od::rd32(file.data() + 8), nr = od::rd32(file.data() + 12);
    struct Range { uint32_t addr, size; };
    std::vector<Range> ranges;
    size_t p = 16;
    for (uint32_t i = 0; i < nr; ++i, p += 8) ranges.push_back({od::rd32(file.data() + p), od::rd32(file.data() + p + 4)});
    size_t rec = 6 + 4 * size_t(size);
    for (const Range& r : ranges) rec += r.size;
    rec += plgSkipPass(file, p);

    od::GameMem m;
    const od::ScopedGameMemory bound(m);
    m.mapExeData(exe);
    m.mapDatPsx(od::readFile(datDir / "PL00P000.DAT"));
    m.mapZero(kPlayer, size);
    for (const Range& r : ranges) m.mapZero(r.addr, r.size);
    od::test::loadNativeState(m);

    int bad = 0, reported = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* r = file.data() + p + size_t(i) * rec;
        const uint8_t* after = r + 6 + 3 * size_t(size);
        const uint8_t* q = r + 6 + 4 * size_t(size);
        od::test::storeNativeState(m);
        for (const Range& rg : ranges) {
            m.write(rg.addr, q, rg.size);
            q += rg.size;
        }
        m.write(kPlayer, after, size);
        // scramble the outputs so a missing write cannot pass by accident
        for (uint32_t o = 0x2b0; o < size; ++o) m.set8(kPlayer + o, 0xcd);
        for (uint32_t o = 0x74; o < 0x94; ++o) m.set8(kPlayer + o, 0xcd);
        od::test::loadNativeState(m);
        try {
            od::port::playerPose(od::port::player());
        } catch (const std::exception& e) {
            std::fprintf(stderr, "tick %u: %s\n", od::rd32(r), e.what());
            ++bad;
            continue;
        }
        od::test::storeNativeState(m);
        int diffs = 0;
        auto check = [&](uint32_t lo, uint32_t hi) {
            for (uint32_t o = lo; o < hi; ++o) {
                if (isPad(o) || m.u8(kPlayer + o) == after[o]) continue;
                if (reported < 16) {
                    std::fprintf(stderr, "tick %u +0x%03x: want %02x got %02x\n", od::rd32(r), o, after[o], m.u8(kPlayer + o));
                    ++reported;
                }
                ++diffs;
            }
        };
        check(0x74, 0x94);
        check(0x2b0, size);
        if (diffs) ++bad;
    }
    std::printf("%u ticks, %d with pose differences\n", count, bad);
    CHECK(bad == 0);
    return testResult("test_player_pose");
}
