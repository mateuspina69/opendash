// Game camera port vs. the game (tests/data/player_ST09.bin, see test_player.cpp for how to regenerate it).
// Camera state 0xa6bca0 is captured at the start of every tick; the camera runs after Maybe_UpdatePlayer, so
// state(tick i) + player after tick i -> cameraUpdate must give state(tick i + 1).
//   test_player_camera <dat dir> <dash2.exe> <tests/data>
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
#include "game/message.hpp"
#include "game/event.hpp"
#include "game/actor_pools.hpp"
#include "game/camera.hpp"
#include "game/player/player_state.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    od::content::registerAll(); // the game code dispatches into the content through its registry
    if (argc < 4) return 2;
    const fs::path datDir = argv[1], exe = argv[2], data = argv[3];
    od::bindTestDataSource(exe);
    for (const char* name : {"player_ST09.bin", "player_ST09_talk.bin"}) {
    const auto file = od::readFile(data / name);
    CHECK(plgMagic(file));
    if (g_failures) return testResult("test_player_camera");
    const uint32_t count = od::rd32(file.data() + 4), size = od::rd32(file.data() + 8), nr = od::rd32(file.data() + 12);
    struct Range { uint32_t addr, size; size_t off; };
    std::vector<Range> ranges;
    size_t p = 16, off = 6 + 4 * size_t(size);
    for (uint32_t i = 0; i < nr; ++i, p += 8) {
        ranges.push_back({od::rd32(file.data() + p), od::rd32(file.data() + p + 4), off});
        off += ranges.back().size;
    }
    const size_t rec = off + plgSkipPass(file, p);
    const Range* cam = nullptr;
    for (const Range& r : ranges)
        if (r.addr == 0xa6bca0) cam = &r; // the camera block (the translator's layout of game/camera_state)
    CHECK(cam != nullptr);
    if (!cam) return testResult("test_player_camera");

    constexpr uint32_t kPlayer = 0xa71d20;
    od::GameMem m;
    const od::ScopedGameMemory bound(m);
    m.mapExeData(exe);
    m.mapZero(kPlayer, size);
    for (const Range& r : ranges) m.mapZero(r.addr, r.size);
    // The talk golden: between the camera of tick i and the capture of tick i + 1 the game runs the message windows
    // (FUN_005578a0: fb 15 writes the talk camera targets) and the event runner (FUN_00543680: FUN_0040e810 /
    // FUN_0040e8f0 start / end the talk camera). Both run here too, on the ST09 message bank.
    const bool events = std::string(name).find("talk") != std::string::npos;
    if (events) {
        if (!m.mapped(0xa5f290, 0x10)) m.mapZero(0xa5f290, 0x10);
        const auto st = od::readFile(datDir / "ST09.DAT");
        for (const od::DatSegment& s : od::readContainer(st))
            if (s.type == 6) {
                m.mapZero(od::port::kMessageBank, 0x4000);
                m.write(od::port::kMessageBank, st.data() + s.fileOffset, s.size);
            }
    }
    od::test::loadNativeState(m); // the native state from this golden's image (nothing of the previous one)
    // the actor slot's rotation (+0x28: the translator's layout of game/actor_state Actor::rotation)
    constexpr uint32_t kActorRotationOffset = 0x28;
    const uint32_t kSpeakerStandIn = od::port::animatedPool()[od::port::kAnimatedFieldSlots - 1].at + kActorRotationOffset;
    int bad = 0, reported = 0, skippedTicks = 0;
    const Range* evt = nullptr;
    for (const Range& rg : ranges)
        if (rg.addr == 0xa5f280) evt = &rg;
    for (uint32_t i = 0; i + 1 < count; ++i) {
        const uint8_t* r = file.data() + p + size_t(i) * rec;
        const uint8_t* next = file.data() + p + size_t(i + 1) * rec;
        od::test::storeNativeState(m);
        for (const Range& rg : ranges) m.write(rg.addr, r + rg.off, rg.size);
        m.write(kPlayer, r + 6 + 3 * size_t(size), size); // player after Maybe_UpdatePlayer
        if (events) {
            // the golden keeps 0x10 bytes of the event request: a non-zero rotation pointer (+0x10) stands for the
            // speaker's (its yaw is not compared); an event the NPC starts during tick i (after the capture) is not
            // in record i: that tick is skipped (the start is compared by test_message, camera range included)
            // (a stand-in when the golden holds none: the rotation of the field's last animated slot, a native object
            // the translator names; the camera does not read it)
            if (m.u32(0xa5f290) == 0) m.set32(0xa5f290, kSpeakerStandIn);
            if (evt && r[evt->off] == 0 && next[evt->off] != 0) {
                ++skippedTicks;
                continue;
            }
        }
        od::test::loadNativeState(m);
        try {
            od::port::cameraUpdate();
            if (events) {
                od::port::messageTick();
                od::port::eventTick();
            }
        } catch (const std::exception& e) {
            if (bad < 6) std::fprintf(stderr, "tick %u: %s\n", od::rd32(r), e.what());
            ++bad;
            continue;
        }
        od::test::storeNativeState(m);
        int diffs = 0;
        for (uint32_t o = 0; o < cam->size; ++o) {
            const uint8_t want = next[cam->off + o], got = m.u8(cam->addr + o);
            if (want == got) continue;
            if (reported < 16) {
                std::fprintf(stderr, "tick %u camera +0x%02x: before %02x want %02x got %02x\n", od::rd32(r), o,
                             r[cam->off + o], want, got);
                ++reported;
            }
            ++diffs;
        }
        if (diffs) {
            ++bad;
            std::fprintf(stderr, "  %s tick %u: %d bytes differ\n", name, od::rd32(r), diffs);
        }
    }
    std::printf("%s: %u ticks (%d skipped: event started by an actor), %d with camera differences\n", name, count - 1, skippedTicks, bad);
    CHECK(bad == 0);
    }
    return testResult("test_player_camera");
}
