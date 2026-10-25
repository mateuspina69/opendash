// Boot / title port (game/title, the load menu of game/save) vs. the game: tests/data/title_*.bin (not in git;
// Every frame: the recorded state before the
// frame is loaded, the pad polled (game/player/player_input), one frame of the port's task, and the port's bytes compared
// with the game's after the frame on the ranges the title flow owns (the fade task, the DAT loader, the sound
// update and the message windows run elsewhere in the game's frame: their bytes are inputs, not compared), plus the
// task entry after the frame (FUN_00604b06).
// 1 start; 40 -; 1 down; 10 -; 1 down;
//       10 -; 1 up; 10 -; 1 up; 10 -; 1 circle" --out ../opendash/tests/data/title_newgame.bin
// 1 start; 60 -; 1 cross; 60 -; 1 start;
//       60 -; 1 down; 10 -; 1 down; 10 -; 1 circle" --out ../opendash/tests/data/title_tutorial.bin
// 1 start; 60 -;
//       1 circle; 30 -; 1 down; 10 -; 1 down; 10 -; 1 up; 10 -; 1 cross; 30 -; 1 circle; 30 -; 1 down; 10 -;
//       1 circle" --out ../opendash/tests/data/title_difficulty.bin
//       1 start; 80 -; 1 circle; 200 -; 1 circle; 120 -; 1 circle; 120 -; 1 circle; 120 -; 1 circle; 120 -;
//       1 circle" --out ../opendash/tests/data/title_continue.bin   (reads the user's DASH2-00.DAT, read only)
//       (the idle demo: ST51 sub 1 replaying ST51T.DAT's pad recording until it ends -> the soft reset -> the title
//       again -> the attract's start)
//   test_title <dash2.exe> <game dir (DASH2-0*.DAT, read only)> <tests/data>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iterator>
#include <span>
#include <filesystem>
#include <string>
#include <vector>

#include "check.hpp"
#include "translator/state_translator.hpp"
#include "data_source_binding.hpp"
#include "content/registry.hpp"
#include "engine/dat.hpp"
#include "translator/gamemem.hpp"
#include "translator/game_memory.hpp"
#include "game/player/player_input.hpp"
#include "game/demo_playback.hpp"
#include "game/title.hpp"
#include "game/fade.hpp"

namespace fs = std::filesystem;

namespace {

struct Range {
    uint32_t addr, size;
};

// the bytes the title flow writes (compared); everything else of the record is input
const Range kChecked[] = {
    {0xa7a000, 0x10}, {0xa6bf80, 0x20}, {0xa6d18c, 0x14}, {0x6d0005, 1}, {0x6d000b, 3},
    {0xa5a240, 0x10}, {0xa7b040, 0x28}, {0xa59de4, 5},     {0xa6d300, 4}, {0x8afaa4, 1}, {0xa7172c, 1},
    {0xa743e0, 1},    {0xa73cc0, 0x120},
};

// game-loop frames: the demo's pad replay and the soft reset
const Range kGameChecked[] = {
    {0xa6bee4, 1}, {0xa6bee8, 2}, {0xa6beec, 4}, {0xa6bef8, 4}, {0xa676a4, 8}, {0xa6d18a, 2}, {0xa7a000, 4},
    {0xa6d1cc, 6},
};

int runGolden(const fs::path& exe, const fs::path& game, const fs::path& file) {
    const auto d = od::readFile(file);
    if (d.size() < 12 || std::memcmp(d.data(), "TTL1", 4) != 0) throw std::runtime_error("not a TTL1 file");
    size_t o = 4;
    auto rd = [&](size_t n) {
        uint32_t v = 0;
        std::memcpy(&v, d.data() + o, n);
        o += n;
        return v;
    };
    std::vector<Range> ranges(rd(4));
    uint32_t total = 0;
    for (Range& r : ranges) {
        r.addr = rd(4);
        r.size = rd(4);
        total += r.size;
    }
    const uint32_t frames = rd(4);

    od::GameMem m;
    const od::ScopedGameMemory bound(m);
    m.mapExeData(exe);
    m.mapZero(0x6c0000, 0xc00);
    m.mapZero(0x6d0000, 0x200);
    od::test::loadNativeState(m); // the native state from this golden's image (nothing of the previous one)
    auto load = [&](const uint8_t* p) {
        od::test::storeNativeState(m);
        for (const Range& r : ranges) {
            if (!m.mapped(r.addr, r.size)) m.mapZero(r.addr, r.size);
            m.write(r.addr, p, r.size);
            p += r.size;
        }
        od::test::loadNativeState(m);
    };
    auto find = [&](const uint8_t* p, uint32_t a) -> const uint8_t* {
        for (const Range& r : ranges) {
            if (a >= r.addr && a < r.addr + r.size) return p + (a - r.addr);
            p += r.size;
        }
        return nullptr;
    };
    od::port::FadeTask fade;
    od::port::TitleHost host;
    // the demo's pad recording (ST51T.DAT's type-2 segment; the game loads it to 0x7a8800 at the stage load)
    std::vector<uint8_t> demo;
    bool demoMapped = false;
    {
        const auto t51 = od::readFile(game / "dat" / "ST51T.DAT");
        for (const od::DatSegment& sg : od::readContainer(t51))
            if (sg.type == 2) demo.assign(t51.begin() + sg.fileOffset, t51.begin() + sg.fileOffset + sg.size);
    }
    host.fade = &fade;
    host.scanSaves = [&] {
        uint32_t mask = 0;
        for (uint32_t i = 0; i < 32; ++i) {
            char nm[16];
            std::snprintf(nm, sizeof nm, "DASH2-%02u.DAT", i);
            if (fs::is_regular_file(game / nm)) mask |= 1u << i;
        }
        return mask;
    };
    host.readSave = [&](uint32_t slot, uint8_t* out, uint32_t size) {
        char nm[16];
        std::snprintf(nm, sizeof nm, "DASH2-%02u.DAT", slot);
        if (!fs::is_regular_file(game / nm)) return -1;
        const auto f = od::readFile(game / nm); // read only
        std::memcpy(out, f.data(), std::min<size_t>(size, f.size()));
        return 1;
    };
    int bad = 0, frameBad = 0;
    for (uint32_t k = 0; k < frames; ++k) {
        const uint32_t frame = rd(4);
        const uint16_t buttons = uint16_t(rd(2));
        const uint8_t before = uint8_t(rd(1)), after = uint8_t(rd(1));
        const uint8_t* pre = d.data() + o;
        const uint8_t* post = pre + total;
        o += size_t(total) * 2;
        load(pre);
        fade = {};
        od::port::padUpdate(od::port::pad0(), buttons, 0x80, 0x80);
        od::port::TitleTasks t;
        t.running = t.pending = od::port::TaskEntry(before);
        std::vector<od::port::ScreenPrim> prims;
        const Range* checked = kChecked;
        size_t nChecked = std::size(kChecked);
        try {
            if (t.running == od::port::TaskEntry::GameLoop) {
                // a game-loop frame (the demo, the attract): FUN_00542530 before the mode handler, FUN_005410b0
                // after it (the field logic itself is other tests' business: its state after the frame is taken from
                // the record where FUN_005410b0 reads it)
                if (m.u32(0xa676a8) == 0x7a8800 && !demoMapped) {
                    m.map(0x7a8800, demo);
                    demoMapped = true;
                }
                od::port::demoPadFrame();
                od::test::storeNativeState(m);
                // the stage transition (mode 1) starts the demo: FUN_00542460 (the recording at the address it
                // picks by 0x6d000d)
                if (const uint8_t* g = find(post, 0xa676a8); g && m.u32(0xa676a8) == 0) {
                    uint32_t rec = 0;
                    std::memcpy(&rec, g, 4);
                    if (rec != 0) {
                        if (!demoMapped) {
                            m.map(0x7a8800, demo);
                            demoMapped = true;
                        }
                        od::port::demoStart(rec);
                    }
                }
                od::test::storeNativeState(m);
                if (const uint8_t* g = find(post, 0xa6d180)) m.set8(0xa6d180, *g);
                for (uint32_t i = 0; i < 0x24; ++i)
                    if (const uint8_t* g = find(post, 0xa73cc0 + i)) m.set8(0xa73cc0 + i, *g);
                od::test::loadNativeState(m);
                if (od::port::softResetFrame(fade)) od::port::titleTaskSwitch(t, od::port::TaskEntry::Main);
                checked = kGameChecked;
                nChecked = std::size(kGameChecked);
            } else {
                od::port::titleTaskFrame(t, host, &prims);
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s frame %u: %s\n", file.filename().string().c_str(), frame, e.what());
            ++bad;
            break;
        }
        od::test::storeNativeState(m);
        int here = 0;
        if (uint8_t(t.pending) != after) {
            std::fprintf(stderr, "%s frame %u: task entry %u, game %u\n", file.filename().string().c_str(), frame,
                         unsigned(t.pending), after);
            ++here;
        }
        for (const Range& c : std::span(checked, nChecked))
            for (uint32_t i = 0; i < c.size; ++i) {
                const uint8_t* g = find(post, c.addr + i);
                if (!g) continue;
                const uint8_t got = m.u8(c.addr + i);
                if (got != *g) {
                    if (frameBad < 12)
                        std::fprintf(stderr, "%s frame %u: 0x%08x = %02x, game %02x\n",
                                     file.filename().string().c_str(), frame, c.addr + i, got, *g);
                    ++here;
                }
            }
        if (here) ++frameBad;
        bad += here;
    }
    std::printf("%s: %u frames, %d failing frames\n", file.filename().string().c_str(), frames, frameBad);
    return bad;
}

} // namespace

int main(int argc, char** argv) {
    od::content::registerAll(); // the game code dispatches into the content through its registry
    if (argc < 4) {
        std::fprintf(stderr, "usage: test_title <dash2.exe> <game dir> <tests/data>\n");
        return 2;
    }
    try {
        const fs::path exe = argv[1], game = argv[2], data = argv[3];
        od::bindTestDataSource(exe);
        int found = 0;
        for (const char* n : {"title_boot.bin", "title_idle.bin", "title_demo.bin", "title_newgame.bin", "title_tutorial.bin",
                              "title_difficulty.bin", "title_continue.bin", "title_gameover.bin"}) {
            if (!fs::exists(data / n)) {
                std::printf("%s: missing (regenerate, see the header of test_title.cpp)\n", n);
                continue;
            }
            ++found;
            CHECK(runGolden(exe, game, data / n) == 0);
        }
        CHECK(found > 0);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "test_title: %s\n", e.what());
        return 1;
    }
    return testResult("test_title");
}
