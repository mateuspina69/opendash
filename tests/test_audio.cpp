// Sound logic port (game/audio_logic) vs. the game, frame by frame (tests/data/snd_*.bin -- not in git (*.bin),
// Each record holds the sound state
// before and after one Maybe_SoundUpdatePerFrame (0x5d3d60) and the DirectSound-level calls it made; the test loads
// the state before, runs od::port::soundUpdate with a recording backend and compares the state after (every byte of
// the recorded ranges) and the calls (op, handle, value) in order.
//   test_audio <dash2.exe> <tests/data> [max reported failures]
// Recipes:
//   snd_ST09_start.bin  --stage 09 --warmup 0 --script "300 -"     (stage load: BGM 0x15, channel 0 fade-in)
//   snd_ST09_talk.bin   --stage 09 --teleport -204 0 -1117 --script "5 -; 1 circle; 60 -; 1 circle; 30 -;
//                       1 circle; 30 -; 1 circle; 30 -; 1 circle; 30 -; 1 circle; 60 -"   (message typing 0x84, 0x81)
//   snd_ST13_combat.bin --stage 13 --teleport -2688 -192 -2200 --yaw 2048 --script "5 -; 3 square; 5 -; 3 square;
//                       5 -; 3 square; 5 -; 3 square; 5 -; 3 square; 5 -; 3 square; 5 -; 3 square; 10 -; 45 up; 20 -"
//                       (buster shots, hits, enemy, explosions, pickups, footsteps)
//   snd_ST09_skate.bin  --stage 09 --poke 0xa71ec3 1 --script "10 -; 20 circle; 30 circle+up; 10 circle+up+left;
//                       10 circle; 10 -; 30 circle; 20 -"   (jet-skate engine: loop voice kind 4, pitch from param)
//   snd_ST13_enemy.bin  --stage 13 --teleport -2688 -192 -3500 --yaw 0 --script "250 -"
//   snd_ST13_hurt.bin   --stage 13 --teleport -2688 -192 -1124 --script "5 -; 80 -; 20 up; 60 -"
//   snd_ST08_walkoff.bin --stage 08 --teleport 0 -256 3264 --yaw 2048 --script "5 -; 20 up; 300 -"
//                       (exit to ST09: BGM fade-out of the 4 fade channels, stage load, new BGM)
//   snd_ST19_door.bin   --stage 19 --teleport -768 -2048 1864 --yaw 2048 --script "5 -; 4 circle; 100 -"
//   snd_ST0F_door.bin   --stage 0F --sub 1 --teleport 5000 0 0 --yaw 3072 --script "3 -; 2 circle; 45 -; 1 circle;
//                       150 -"   (door event: message typing, door sounds 0x13f)
#include <algorithm>
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
#include "game/actor_pools.hpp"
#include "game/audio_logic.hpp"
#include "engine/dat.hpp"
#include "translator/gamemem.hpp"
#include "translator/game_memory.hpp"

namespace fs = std::filesystem;

namespace {

struct Range {
    uint32_t addr, size;
};
struct Static {
    uint32_t addr;
    std::vector<uint8_t> bytes;
};
struct Rec {
    uint32_t tick = 0;
    std::vector<Static> statics;
    std::vector<uint8_t> before, after;
    std::vector<od::port::SoundCall> calls;
};

void decode(const uint8_t* p, size_t n, std::vector<uint8_t>& blob) {
    size_t o = 0;
    for (const uint8_t* end = p + n; p < end;) {
        const uint32_t z = od::rd16(p), l = od::rd16(p + 2);
        p += 4;
        o += z;
        for (uint32_t k = 0; k < l; ++k) blob[o + k] ^= p[k];
        o += l;
        p += l;
    }
}

bool readGolden(const fs::path& path, std::vector<Range>& ranges, std::vector<Rec>& recs) {
    const auto f = od::readFile(path);
    if (f.size() < 12 || std::memcmp(f.data(), "SND1", 4) != 0) return false;
    size_t p = 4;
    const uint32_t nr = od::rd32(f.data() + p);
    p += 4;
    size_t total = 0;
    for (uint32_t i = 0; i < nr; ++i, p += 8) {
        ranges.push_back({od::rd32(f.data() + p), od::rd32(f.data() + p + 4)});
        total += ranges.back().size;
    }
    const uint32_t count = od::rd32(f.data() + p);
    p += 4;
    std::vector<uint8_t> prev(total, 0);
    for (uint32_t i = 0; i < count; ++i) {
        Rec r;
        r.tick = od::rd32(f.data() + p);
        const uint32_t ns = od::rd16(f.data() + p + 6);
        p += 8;
        for (uint32_t k = 0; k < ns; ++k) {
            const uint32_t a = od::rd32(f.data() + p), n = od::rd32(f.data() + p + 4);
            r.statics.push_back({a, std::vector<uint8_t>(f.begin() + long(p + 8), f.begin() + long(p + 8 + n))});
            p += 8 + n;
        }
        uint32_t n = od::rd32(f.data() + p);
        r.before = prev;
        decode(f.data() + p + 4, n, r.before);
        p += 4 + n;
        n = od::rd32(f.data() + p);
        r.after = r.before;
        decode(f.data() + p + 4, n, r.after);
        p += 4 + n;
        const uint32_t nc = od::rd32(f.data() + p);
        p += 4;
        for (uint32_t k = 0; k < nc; ++k, p += 9)
            r.calls.push_back({f[p], od::rd32(f.data() + p + 1), od::rd32(f.data() + p + 5)});
        prev = r.after;
        recs.push_back(std::move(r));
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    od::content::registerAll(); // the game code dispatches into the content through its registry
    if (argc < 3) {
        std::fprintf(stderr, "usage: test_audio <dash2.exe> <tests/data> [max reports]\n");
        return 2;
    }
    const fs::path exe = argv[1], data = argv[2];
    od::bindTestDataSource(exe);
    const int maxReports = argc > 3 ? std::atoi(argv[3]) : 8;
    int files = 0;
    std::vector<fs::path> paths; // every tests/data/snd_*.bin
    for (const auto& de : fs::directory_iterator(data))
        if (de.path().filename().string().rfind("snd_", 0) == 0 && de.path().extension() == ".bin")
            paths.push_back(de.path());
    std::sort(paths.begin(), paths.end());
    for (const fs::path& path : paths) {
        const std::string nameStr = path.filename().string();
        const char* name = nameStr.c_str();
        std::vector<Range> ranges;
        std::vector<Rec> recs;
        if (!readGolden(path, ranges, recs)) {
            std::fprintf(stderr, "%s: bad golden\n", name);
            ++g_failures;
            continue;
        }
        ++files;
        od::GameMem m;
        const od::ScopedGameMemory bound(m);
        m.mapExeData(exe);
        for (const Range& r : ranges) m.mapZero(r.addr, r.size); // overwrites the exe's bytes where they overlap
        od::test::loadNativeState(m); // the native state from this golden's image (nothing of the previous one)
        od::port::RecordingSoundBackend rec;
        od::port::setSoundBackend(&rec);
        int failed = 0, reported = 0, calls = 0, frames = 0;
        for (const Rec& r : recs) {
            od::test::storeNativeState(m);
            for (const Static& s : r.statics) m.map(s.addr, s.bytes);
            size_t o = 0;
            for (const Range& rg : ranges) {
                m.write(rg.addr, r.before.data() + o, rg.size);
                o += rg.size;
            }
            od::test::loadNativeState(m);
            rec.calls.clear();
            bool ok = true;
            try {
                od::port::soundUpdate();
            } catch (const std::exception& e) {
                if (reported < maxReports) std::fprintf(stderr, "  %s tick %u: %s\n", name, r.tick, e.what());
                ok = false;
            }
            od::test::storeNativeState(m);
            o = 0;
            int diffs = 0;
            for (const Range& rg : ranges) {
                for (uint32_t k = 0; k < rg.size; ++k) {
                    const uint8_t want = r.after[o + k], got = m.u8(rg.addr + k);
                    if (want == got) continue;
                    if (reported < maxReports && diffs < 8)
                        std::fprintf(stderr, "  %s tick %u %08x: want %02x got %02x\n", name, r.tick, rg.addr + k, want,
                                     got);
                    ++diffs;
                }
                o += rg.size;
            }
            if (rec.calls != r.calls) {
                if (reported < maxReports) {
                    std::fprintf(stderr, "  %s tick %u calls:\n", name, r.tick);
                    for (const auto& c : r.calls) std::fprintf(stderr, "    want %s\n", od::port::soundCallText(c).c_str());
                    for (const auto& c : rec.calls) std::fprintf(stderr, "    got  %s\n", od::port::soundCallText(c).c_str());
                }
                ok = false;
            }
            calls += int(r.calls.size());
            ++frames;
            if (diffs || !ok) {
                ++failed;
                ++reported;
            }
        }
        od::port::setSoundBackend(nullptr);
        std::printf("%s: %d frames, %d backend calls: %d ok, %d failing\n", name, frames, calls, frames - failed, failed);
        CHECK(failed == 0);
    }
    CHECK(files > 0);

    // snd_pan.txt: the exe's Maybe_GetListenerDistance + Maybe_ComputeSfx3DPanVolume and Maybe_ComputeSfxExplicitPan
    // on an input grid -> dist, voice pan +8, volume +9.
    if (const fs::path pan = data / "snd_pan.txt"; fs::exists(pan)) {
        od::GameMem m;
        const od::ScopedGameMemory bound(m);
        m.mapExeData(exe);
        // the listener on the player (its position and rotation: the points the native listener names)
        constexpr uint32_t kListenerPos = od::port::kPlayer + 0x10, kListenerRot = od::port::kPlayer + 0x28;
        m.set32(0xa6d2fc, kListenerPos);
        m.set32(0xa79fa4, kListenerRot);
        m.set8(0xa71793, 0);
        od::test::loadNativeState(m);
        od::port::buildSin64Tables(); // native state: built after the load (each case stores it into the image first)
        std::FILE* f = std::fopen(pan.string().c_str(), "r");
        char line[256];
        int lines = 0, bad = 0;
        while (f && std::fgets(line, sizeof line, f)) {
            int mono = 0, master = 0, a[8], want[3] = {0, 0, 0}, got[3] = {0, 0, 0};
            od::test::storeNativeState(m);
            if (std::sscanf(line, "D %d %d %d %d %d %d %d %d %d -> %d %d %d", &mono, &master, &a[0], &a[1], &a[2], &a[3],
                            &a[4], &a[5], &a[6], &want[0], &want[1], &want[2]) == 12) {
                m.set8(0xa6d1c8, uint32_t(mono));
                m.set8(0xa71790, uint32_t(master));
                m.set32(kListenerPos, uint32_t(a[0]) << 16);
                m.set32(kListenerPos + 4, 0);
                m.set32(kListenerPos + 8, uint32_t(a[1]) << 16);
                m.set16(kListenerRot + 2, uint32_t(a[2]));
                od::test::loadNativeState(m);
                od::port::soundPanProbe(a[6], a[3], a[4], a[5], got);
            } else if (std::sscanf(line, "E %d %d %d %d %d -> %d %d", &mono, &master, &a[0], &a[1], &a[2], &want[1],
                                   &want[2]) == 7) {
                m.set8(0xa6d1c8, uint32_t(mono));
                m.set8(0xa71790, uint32_t(master));
                od::test::loadNativeState(m);
                od::port::soundExplicitPanProbe(a[0], a[1], a[2], got);
            } else {
                continue;
            }
            ++lines;
            if (got[0] != want[0] || got[1] != want[1] || got[2] != want[2]) {
                if (bad < maxReports) std::fprintf(stderr, "  snd_pan: got %d %d %d for %s", got[0], got[1], got[2], line);
                ++bad;
            }
        }
        if (f) std::fclose(f);
        std::printf("snd_pan.txt: %d cases, %d failing\n", lines, bad);
        CHECK(lines > 0 && bad == 0);
    }
    return testResult("test_audio");
}
