// Save / load / new-game port (game/save) vs. the game (tests/data/save_golden.bin -- not in git (*.bin); regenerate
// Each case holds
// the recorded ranges before and after the exe's code (Maybe_GameModeInit's state part for load / new game, the
// save menu's image build, the title builder); the test runs the port on the "before" bytes and compares every byte.
// Also: the user's saves pass the checksum verify (read-only), and a built image loads back to the same image.
//   test_save <dash2.exe> <game dir (DASH2-0*.DAT)> <tests/data>
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
#include "engine/dat.hpp"
#include "exetables.hpp"
#include "translator/gamemem.hpp"
#include "translator/game_memory.hpp"
#include "game/game_init.hpp"
#include "game/game_state.hpp"
#include "game/save_image.hpp"

namespace fs = std::filesystem;

namespace {

struct Range {
    uint32_t addr, size;
};

int compare(const od::GameMem& m, const std::vector<Range>& ranges, const uint8_t* post, const char* name) {
    int bad = 0;
    for (const Range& r : ranges) {
        for (uint32_t i = 0; i < r.size; ++i) {
            const uint8_t got = m.u8(r.addr + i);
            if (got != post[i]) {
                if (bad < 8)
                    std::fprintf(stderr, "%s: 0x%08x = %02x, game %02x\n", name, r.addr + i, got, post[i]);
                ++bad;
            }
        }
        post += r.size;
    }
    return bad;
}

} // namespace

int main(int argc, char** argv) {
    od::content::registerAll(); // the game code dispatches into the content through its registry
    if (argc < 4) {
        std::fprintf(stderr, "usage: test_save <dash2.exe> <game dir> <tests/data>\n");
        return 2;
    }
    try {
        const fs::path exe = argv[1], game = argv[2], data = argv[3];
        od::bindTestDataSource(exe);
        // the user's saves: read-only, checksums as the game verifies them (FUN_00549550)
        for (int n = 0; n < 3; ++n) {
            char name[16];
            std::snprintf(name, sizeof name, "DASH2-%02d.DAT", n);
            if (!fs::exists(game / name)) continue;
            const auto f = od::readFile(game / name);
            CHECK(f.size() == od::port::kSaveSize);
            CHECK(!od::port::saveChecksumBad(f));
            auto g = f;
            g[0x210] ^= 1; // zenny (block 2)
            CHECK(od::port::saveChecksumBad(g)); // the second block changed: caught by the full check only
            CHECK(!od::port::saveChecksumBad(g, true));
            od::port::saveChecksumWrite(g);
            CHECK(!od::port::saveChecksumBad(g));
            std::printf("%s ok (stage %02X sub %d)\n", name, f[0x128], f[0x129]);
        }

        {
          // FUN_0040e290 apply them after loading DASH2-00 / 02 (0xa6bd48.. = a0 c0 0x800 0x50 0)
            const od::ExeTables tables(od::readFile(exe));
            const uint32_t t1d = tables.cameraTable(0x1d), t24 = tables.cameraTable(0x24);
            CHECK(t1d == 0x8e6c78 && t24 == 0x8ea230);
            od::GameMem g;
            g.mapExeData(exe);
            // ST1D sub 1's camera record is ST24 sub 0's
            CHECK(t1d && t24 && std::memcmp(g.view(t1d + 12, 12).data(), g.view(t24, 12).data(), 12) == 0);
            int found = 0;
            for (int n = 0; n < od::ExeTables::kStages; ++n) found += tables.cameraTable(n) != 0;
            CHECK(found == od::ExeTables::kStages); // every stage's setup has the call (scan of the 0x8d397c table)
        }
        const fs::path golden = data / "save_golden.bin";
        if (!fs::exists(golden)) {
            std::fprintf(stderr, "missing %s\n", golden.string().c_str());
            return 1;
        }
        const auto f = od::readFile(golden);
        CHECK(f.size() > 8 && std::memcmp(f.data(), "SAV1", 4) == 0);
        size_t q = 4;
        std::vector<Range> ranges(od::rd32(f.data() + q));
        q += 4;
        uint32_t total = 0;
        for (Range& r : ranges) {
            r = {od::rd32(f.data() + q), od::rd32(f.data() + q + 4)};
            total += r.size;
            q += 8;
        }
        const uint32_t cases = od::rd32(f.data() + q);
        q += 4;
        od::GameMem base;
        base.mapExeData(exe);
        for (uint32_t c = 0; c < cases; ++c) {
            const char* name = reinterpret_cast<const char*>(f.data() + q);
            const uint32_t kind = od::rd32(f.data() + q + 24), a0 = od::rd32(f.data() + q + 28),
                           a1 = od::rd32(f.data() + q + 32), a2 = od::rd32(f.data() + q + 36);
            const uint8_t* pre = f.data() + q + 40;
            const uint8_t* post = pre + total;
            q += 40 + size_t(total) * 2;
            od::GameMem m = base;
            const od::ScopedGameMemory bound(m);
            const uint8_t* p = pre;
            for (const Range& r : ranges) {
                m.map(r.addr, {p, r.size});
                p += r.size;
            }
            od::test::loadNativeState(m);
            switch (kind) {
            case 0: od::port::gameModeInitState(); break;
            case 1: od::port::saveBuildImage(uint8_t(a0)); break;
            case 2: od::port::saveTitle(uint8_t(a0), od::port::kSaveBuf, a1, a2); break;
            default: CHECK(!"unknown case kind");
            }
            od::test::storeNativeState(m);
            const int bad = compare(m, ranges, post, name);
            CHECK(bad == 0);
            uint32_t changed = 0; // the game's own change (a case that changes nothing would test nothing)
            for (uint32_t i = 0; i < total; ++i) changed += pre[i] != post[i];
            CHECK(changed > 0);
            std::printf("%-14s %s (%u bytes changed by the game, %d differ)\n", name, bad ? "FAILED" : "ok", changed,
                        bad);
            if (kind == 1) { // the built image: checksums good, and it loads back to the same state
                CHECK(!od::port::saveChecksumBad(od::port::kSaveBuf));
                const auto img = m.view(od::port::kSaveBuf, od::port::kSaveSize);
                const std::vector<uint8_t> first(img.begin(), img.end());
                od::test::loadNativeState(m);
                od::port::gameState().loadSave = 1; // the game mode's init applies the save image
                od::port::gameModeInitState();
                od::port::saveSerialise(od::port::kSaveBuf);
                od::test::storeNativeState(m);
                const auto again = m.view(od::port::kSaveBuf, od::port::kSaveSize);
                CHECK(std::memcmp(first.data(), again.data(), first.size()) == 0);
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "exception: %s\n", e.what());
        ++g_failures;
    }
    return testResult("test_save");
}
