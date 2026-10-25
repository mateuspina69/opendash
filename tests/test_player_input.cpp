// Pad pipeline port vs. the game (tests/data/player_ST09.bin, see test_player.cpp for how to regenerate it):
// the pad is simulated from the idle warm-up state through the recorded buttons of every tick and the
// first 0x40 bytes of pad 0 (captured each tick) must match. Bytes 0x40.. (double-tap / repeat counters)
// are not captured but feed back into 0x30/0x32, so they are checked indirectly.
//   test_player_input <tests/data>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#include "check.hpp"
#include "translator/state_translator.hpp"
#include "data_source_binding.hpp"
#include "plg.hpp"
#include "content/registry.hpp"
#include "engine/dat.hpp"
#include "translator/gamemem.hpp"
#include "translator/game_memory.hpp"
#include "game/player/player_input.hpp"

// pad 0's block in the exe's layout (the translator's game/player/player_input layout)
constexpr uint32_t kPad0 = 0xa6bec0;

int main(int argc, char** argv) {
    od::content::registerAll(); // the game code dispatches into the content through its registry
    if (argc < 3) return 2;
    od::bindTestDataSource(argv[2]);
    const auto file = od::readFile(std::filesystem::path(argv[1]) / "player_ST09.bin");
    CHECK(plgMagic(file));
    if (g_failures) return testResult("test_player_input");
    const uint32_t count = od::rd32(file.data() + 4), size = od::rd32(file.data() + 8), nr = od::rd32(file.data() + 12);
    size_t p = 16, off = 6 + 4 * size_t(size), padOff = 0;
    for (uint32_t i = 0; i < nr; ++i, p += 8) {
        if (od::rd32(file.data() + p) == kPad0) padOff = off;
        off += od::rd32(file.data() + p + 4);
    }
    const size_t rec = off + plgSkipPass(file, p);
    CHECK(padOff != 0);

    od::GameMem m;
    const od::ScopedGameMemory bound(m);
    m.mapZero(kPad0, 0x60);
    const uint8_t* first = file.data() + p + padOff;
    m.set8(kPad0 + 0x36, first[0x36]); // repeat delay / rate: set by the game's pad init
    m.set8(kPad0 + 0x37, first[0x37]);
    od::test::loadNativeState(m);
    int bad = 0, reported = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* r = file.data() + p + size_t(i) * rec;
        const uint16_t buttons = uint16_t(r[4] | r[5] << 8);
        od::port::padUpdate(od::port::pad0(), buttons);
        od::test::storeNativeState(m);
        int diffs = 0;
        for (uint32_t o = 0; o < 0x40; ++o) {
            const uint8_t want = r[padOff + o], got = m.u8(kPad0 + o);
            if (want == got) continue;
            if (reported++ < 12)
                std::fprintf(stderr, "tick %u buttons %04x pad +0x%02x: want %02x got %02x\n", od::rd32(r), buttons, o,
                             want, got);
            ++diffs;
        }
        if (diffs) ++bad;
    }
    std::printf("%u ticks, %d with pad differences\n", count, bad);
    CHECK(bad == 0);
    return testResult("test_player_input");
}
