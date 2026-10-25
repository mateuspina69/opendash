// Event runner / message window port vs. the game (tests/data/msg_ST*.bin -- not in git (*.bin), regenerate
// Each golden holds the game state (ranges
// listed in its header) at the capture points of every tick; the test loads the state at a point, runs the
// ported function and compares every range with the state the game reached:
//   E0 -> eventTick -> E1      (FUN_00543680)
//   S0 -> eventStart -> S1     (FUN_00543bb0, with the captured arguments)
//   W0 -> windowLogic -> W1    (FUN_00557c90)
//   W1 -> windowLayout -> W2   (FUN_00557eb0, state only)
// It also checks that the message bank the game had at 0x73c000 is the type-6 segment of ST<stage>.DAT.
//   test_message <dat dir> <dash2.exe> <tests/data> [max reported failures]
// Recipes:
//   msg_ST12_door_yes.bin    --stage 12 --sub 1 --teleport -3584 -192 -6048 --yaw 0
//                            --script "3 -; 2 circle; 45 -; 1 circle; 30 -"           (B2F door: yes -> flag 0x702)
//   msg_ST12_door_no.bin     (same) --script "3 -; 2 circle; 45 -; 2 right; 2 -; 1 circle; 30 -"   (cursor -> no)
//   msg_ST12_door_cancel.bin (same) --script "3 -; 2 circle; 45 -; 1 cross; 30 -"     (cross cancels)
//   msg_ST12_door_locked.bin (same) --poke 0xa73da2 1 --script "3 -; 2 circle; 60 -; 1 circle; 30 -"
//                            (flag 0x710 = locked: the door shows actor[0xf] = message 0)
//   msg_ST09_talk.bin        --stage 09 --teleport -204 0 -1117 --script "5 -; 1 circle; 60 -; 1 circle; 30 -;
//                            1 circle; 30 -; 1 circle; 30 -; 1 circle; 30 -; 1 circle; 60 -"   (NPC, talk camera)
//   msg_ST0A_talk.bin        --stage 0A --sub 0 --teleport -56 0 -256 --yaw 1024 --script (as ST09_talk)
//                            (stationary NPC, message 50: fb 27 / fb 30 / fb 21 + fb 1f / fb 3f, frame style 3)
//   msg_ST0A1_talk.bin       --stage 0A --sub 1 --teleport -56 0 320 --yaw 1024 --script "5 -; 1 circle; 60 -;
//                            1 circle; 30 -" + 6 x "1 circle; 30 -" + "; 1 circle; 60 -"   (message 51: fb 18)
//   msg_ST0B1_data.bin       --stage 0B --sub 1 --warmup 59 --teleport 576 -32 352 --yaw 2048 --script (as
//                            act_ST0B_save in test_actors.cpp)   (Data: fb 07 / 08 / 0c / 0e / 12 / 29 / 34 / 35,
//                            the save menu's windows: fb 23 / 2e / 44 / 45)
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "check.hpp"
#include "translator/actor_layout.hpp"
#include "translator/state_translator.hpp"
#include "data_source_binding.hpp"
#include "content/registry.hpp"
#include "engine/dat.hpp"
#include "translator/gamemem.hpp"
#include "translator/game_memory.hpp"
#include "game/message.hpp"
#include "game/event.hpp"
#include "game/text_script.hpp"

namespace fs = std::filesystem;

namespace {
struct Range {
    uint32_t addr, size, off;
};
const char* kPointName[] = {"E0", "E1", "S0", "S1", "W0", "W1", "W2"};
} // namespace

int main(int argc, char** argv) {
    od::content::registerAll(); // the game code dispatches into the content through its registry
    if (argc < 4) return 2;
    const fs::path datDir = argv[1], exe = argv[2], data = argv[3];
    od::bindTestDataSource(exe);
    const int maxReports = argc > 4 ? std::stoi(argv[4]) : 3;
    std::vector<fs::path> files;
    for (const auto& de : fs::directory_iterator(data))
        if (de.path().extension() == ".bin" && de.path().filename().string().rfind("msg_ST", 0) == 0)
            files.push_back(de.path());
    std::sort(files.begin(), files.end());
    CHECK(!files.empty());
    for (const fs::path& path : files) {
        const std::string name = path.filename().string();
        const auto f = od::readFile(path);
        if (f.size() < 16 || std::memcmp(f.data(), "MSG1", 4) != 0) {
            std::fprintf(stderr, "%s: not a MSG1 golden\n", name.c_str());
            CHECK(false);
            continue;
        }
        const uint32_t count = od::rd32(f.data() + 4), nr = od::rd32(f.data() + 8);
        size_t p = 12;
        std::vector<Range> ranges;
        uint32_t total = 0;
        for (uint32_t i = 0; i < nr; ++i, p += 8) {
            ranges.push_back({od::rd32(f.data() + p), od::rd32(f.data() + p + 4), total});
            total += ranges.back().size;
        }
        od::GameMem m;
        const od::ScopedGameMemory bound(m);
        m.mapExeData(exe);
        for (const Range& r : ranges)
            if (!m.mapped(r.addr, r.size)) m.mapZero(r.addr, r.size);
        const uint32_t ns = od::rd32(f.data() + p);
        p += 4;
        for (uint32_t i = 0; i < ns; ++i) {
            const uint32_t a = od::rd32(f.data() + p), n = od::rd32(f.data() + p + 4);
            m.mapZero(a, n);
            m.write(a, f.data() + p + 8, n);
            if (a == od::port::kMessageBank) { // the bank is the stage DAT's type-6 segment
                const auto st = od::readFile(datDir / (name.substr(4, 4) + ".DAT"));
                bool found = false;
                for (const od::DatSegment& s : od::readContainer(st))
                    if (s.type == 6) {
                        found = s.size <= n && std::memcmp(st.data() + s.fileOffset, f.data() + p + 8, s.size) == 0;
                        if (!found) std::fprintf(stderr, "%s: bank 0x73c000 != %s type-6 segment\n", name.c_str(),
                                                 name.substr(4, 4).c_str());
                    }
                CHECK(found);
            }
            p += 8 + n;
        }
        std::vector<uint8_t> img(total, 0), prev(total, 0);
        od::test::loadNativeState(m); // the native state from this golden's image (nothing of the previous one)
        auto load = [&](const std::vector<uint8_t>& s) {
            od::test::storeNativeState(m);
            for (const Range& r : ranges) m.write(r.addr, s.data() + r.off, r.size);
            od::test::loadNativeState(m);
        };
        int prevPoint = -1;
        uint32_t prevTick = 0;
        std::vector<uint32_t> args;
        int ok = 0, failed = 0, reported = 0;
        int steps[4] = {};
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t tick = od::rd32(f.data() + p);
            const int point = f[p + 6], na = f[p + 7];
            p += 8;
            std::vector<uint32_t> a(na);
            for (int k = 0; k < na; ++k, p += 4) a[size_t(k)] = od::rd32(f.data() + p);
            const uint32_t nruns = od::rd32(f.data() + p);
            p += 4;
            for (uint32_t k = 0; k < nruns; ++k) {
                const uint32_t off = od::rd32(f.data() + p), len = od::rd16(f.data() + p + 4);
                std::memcpy(img.data() + off, f.data() + p + 6, len);
                p += 6 + len;
            }
            const bool pair = prevTick == tick && ((prevPoint == 0 && point == 1) || (prevPoint == 2 && point == 3) ||
                                                   (prevPoint == 4 && point == 5) || (prevPoint == 5 && point == 6));
            if (pair) {
                load(prev);
                bool good = true;
                const int rep = reported < maxReports ? 8 : 0;
                try {
                    switch (point) {
                    case 1: od::port::eventTick(); ++steps[0]; break;
                    case 3: {
                        // the captured pointers (exe words) as the native objects they name (the translator's registry)
                        void* position = nullptr;
                        void* rotation = nullptr;
                        od::test::resolveNativePointer(0xa5f28c, args[0], typeid(od::port::Fixed16Vec3), position);
                        od::test::resolveNativePointer(0xa5f290, args[1], typeid(od::port::ShortVec3), rotation);
                        od::port::eventStart(static_cast<const od::port::Fixed16Vec3*>(position),
                                             static_cast<od::port::ShortVec3*>(rotation), args[2], uint16_t(args[3]),
                                             uint16_t(args[4]));
                    }
                        ++steps[1]; break;
                    case 5: od::port::windowLogic(); ++steps[2]; break;
                    default: od::port::windowLayout(); ++steps[3]; break;
                    }
                    od::test::storeNativeState(m);
                    int diffs = 0;
                    for (const Range& r : ranges)
                        for (uint32_t o = 0; o < r.size; ++o) {
                            const uint8_t got = m.u8(r.addr + o), want = img[r.off + o];
                            if (got == want) continue;
                            if (diffs < rep)
                                std::fprintf(stderr, "  %s tick %u %s->%s %08x: before %02x want %02x got %02x\n",
                                             name.c_str(), tick, kPointName[prevPoint], kPointName[point], r.addr + o,
                                             prev[r.off + o], want, got);
                            ++diffs;
                        }
                    good = diffs == 0;
                } catch (const std::exception& e) {
                    if (rep) std::fprintf(stderr, "  %s tick %u %s: %s\n", name.c_str(), tick, kPointName[point], e.what());
                    good = false;
                }
                if (good) {
                    ++ok;
                } else {
                    ++failed;
                    ++reported;
                }
            }
            if (point == 2) args = a;
            prev = img;
            prevPoint = point;
            prevTick = tick;
        }
        std::printf("%s: %d steps ok, %d failing (event %d, start %d, logic %d, layout %d)\n", name.c_str(), ok, failed,
                    steps[0], steps[1], steps[2], steps[3]);
        std::string seen;
        for (uint32_t h : od::port::messageHandlersSeen(true)) {
            char b[12];
            std::snprintf(b, sizeof b, " %06x", h);
            seen += b;
        }
        std::printf("  handlers:%s\n", seen.c_str());
        CHECK(failed == 0);
    }
    return testResult("test_message");
}
