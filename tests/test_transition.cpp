// Stage transition port (game/transition) vs. the game, tick by tick (tests/data/trans_*.bin -- not in git
// Each golden holds the
// game state at fixed points of every game tick: the test loads the state at the start
// of the mode handler (point 0 = FUN_005413a0, 12 = FUN_00541340), runs the ported handler and compares the state
// at every point the port passes:
//   1 FUN_00542280 entry, 2 after the script runner (the transition scripts), then the player steps on the
//   game's own inputs (3 -> FUN_00574860 -> 4, 5 -> FUN_004140e0 -> 6 -> Maybe_UpdatePlayer -> 7; the actor
//   updates in between are not ported, so the state is reloaded at 5 / 6 / 8 = end of the frame body),
//   13 / 15 the per-stage room setup (not ported: reloaded at 14 / 16), 17 / 18 the camera of FUN_00541340,
//   9 after the mode handler (before FUN_005e15f0).
// Mode-1 ticks (stage load, OpenDash's own loader) only check FUN_00542370 (10 -> 11). The fade task (types 0/1)
// and the wipes (types 2..6) are checked between point 9 of a tick and point 0 / 12 of the next one.
// The room's collision comes from ST<stage>.DAT mapped where the game had it ([0x6d0084] - pool offset) with
// the golden's segment list on top (the door scripts toggle segment flags).
//   test_transition <dat dir> <dash2.exe> <tests/data> [max reported failures]
// Recipes:
//   trans_ST0F_passage.bin --stage 0F --sub 1 --teleport 0 0 1408 --yaw 2048 --script "5 -; 4 circle; 90 -"
//                          (exit 1, type 8 -> sub 2: walk-out, room switched in place, back-off 0x110, walk-in)
//   trans_ST0F_sub9.bin    --stage 0F --sub 5 --teleport -6528 0 0 --yaw 1024 --script "5 -; 4 circle; 80 -"
//                          (exit 3, type 8 -> sub 9: the arrival the app used to leave behind the door)
//   trans_ST0F_door.bin    --stage 0F --sub 1 --teleport 5000 0 0 --yaw 3072
//                          --script "3 -; 2 circle; 45 -; 1 circle; 150 -"
//                          (door actor event, "yes": kind 14, the player sinks with the door, fade 0x12 / 0x10,
//                          mode 2 room load, sub 5 at the destination, fade-in 2)
//   trans_ST08_walkoff.bin --stage 08 --sub 0 --teleport 0 -256 3264 --yaw 2048 --script "5 -; 20 up; 150 -"
//                          (exit 0, type 15 -> ST09: walk-off 39 ticks, wipe 0x28, mode 1 stage load)
//   trans_ST10_warp.bin    --stage 10 --sub 0 --teleport 5312 -1696 9472 --yaw 3072 --script "5 -; 20 up; 40 -"
//                          (exit 1, type 1 -> request 0xff: in-stage warp to sub 1 by the exit's delta)
//   trans_ST04_hinged.bin  --stage 04 --sub 0 --teleport 264 0 480 --yaw 2048 --script "5 -; 4 circle; 100 -"
//                          (exit 0, type 2 = kind 0: hinged door leaf type 1 follows the hand, walk-through)
//   trans_ST29_double.bin  --stage 29 --sub 0 --teleport 0 -384 4000 --yaw 2048 --script "5 -; 4 circle; 100 -"
//                          (exit 0, type 3 = kind 1: double hinged door, type-2 leaves)
//   trans_ST19_sliding.bin --stage 19 --sub 0 --teleport -768 -2048 1864 --yaw 2048 --script "5 -; 4 circle; 100 -"
//                          (exit 4, type 4 = kind 2: sliding pair, type-3 leaves, wipe 0x26, mode 2 load of sub 1)
//   trans_ST18_ladder.bin  --stage 18 --sub 2 --teleport -736 0 128 --yaw 1024 --script "5 -; 4 circle; 160 -"
//                          (exit 1, type 11 = kind 9: ladder / hole)
//   trans_ST08_kind4.bin   --stage 08 --sub 0 --teleport -1440 -1024 16 --yaw 1024 --script "5 -; 4 circle; 120 -"
//                          (exit 3, type 6 = kind 4 -> ST04: stage load)
//   trans_ST18_kind10.bin  --stage 18 --sub 0 --teleport -512 -2688 192 --yaw 0 --script "5 -; 4 circle; 100 -"
//                          (exit 0, type 12 = kind 10)
//   trans_ST2E_kind8.bin   --stage 2E --sub 4 --poke-early 0xa73d3e 1 --teleport -512 0 576 --yaw 0
//                          --script "5 -; 8 up; 110 -" (pit, type 10 = kind 8; story flag 0x3f0 keeps the room event off)
//   trans_ST40_camera2.bin --stage 40 --sub 0 --teleport 0 -768 -3792 --yaw 0 --script "5 -; 4 circle; 110 -"
//                          (the room's mode-2 camera, Maybe_InterpolateAndApplyCamera; the player stays frozen,
//                          the teleporter kind 11 is not reached)
//   trans_ST09_house0A.bin --stage 09 --sub 0 --teleport -972 -256 -6436 --yaw 1024
//                          --script "5 -; 20 up; 4 circle; 60 -; 4 circle; 160 -" (Yosionke exit 2 -> ST0A house)
//   trans_ST09_house0C.bin --stage 09 --sub 0 --teleport 1032 -256 -5888 --yaw 3072 (same script; exit 7 -> ST0C)
//   trans_ST0A_out09.bin   --stage 0A --sub 0 --teleport 464 0 -256 --yaw 3072 (same script; back to ST09)
//   trans_ST4E_lift.bin    --stage 4E --sub 0 --save <game>/DASH2-02.DAT --teleport 0 0 -3584 --yaw 0
//                          --script "3 -; 2 circle; 45 -; 1 circle; 150 -" (the lift up to ST24, kind 14)
//   trans_ST4E_down.bin    --stage 4E --sub 9 (same save / script) --teleport 0 0 1536 --yaw 2048 (down to ST51)
// The room setup's camera part (FUN_0040e290 with the room's record, game/camera roomCamera, param >= 0) is compared at
// point 14 on the bytes it writes (house pitch 480 / distance 672 vs. the town's 80 / 704).
// Door-leaf actors (static pool types 1 / 2 / 3) are checked at the end of Maybe_UpdateStaticActors (point 23 -> 5).
#include <optional>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "check.hpp"
#include "data_source_binding.hpp"
#include "golden_delta.hpp"
#include "translator/state_translator.hpp"
#include "content/registry.hpp"
#include "game/actor_collision.hpp"
#include "engine/dat.hpp"
#include "translator/gamemem.hpp"
#include "translator/game_memory.hpp"
#include "game/camera.hpp"
#include "game/actor_movement.hpp"
#include "game/modern_play.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_update.hpp"
#include "engine/room.hpp"
#include "game/door_leaves.hpp"
#include "game/fade.hpp"
#include "game/transition.hpp"

namespace fs = std::filesystem;
using od::port::kPlayer;

namespace {

using od::test::Meta;
using od::test::Range;
using od::test::Rec;
using Golden = od::test::DeltaGolden; // tests/golden_delta.hpp (TRG1)

const char* kPointName[] = {"5413a0", "542280", "542285", "542294", "542299", "4140e0", "5422b2", "5422b7", "5422e4",
                            "541091", "542370", "54242e", "541340", "5d5b58", "5d5b5f", "5d5b9e", "5d5ba5", "54137f",
                            "541384", "5413c8", "5413cd", "5422f0", "render", "5422a8"};

bool readGolden(const fs::path& path, od::GameMem& m, Golden& g) {
    return od::test::readDeltaGolden(path, "TRG1", m, g, true);
}

struct Runner {
    od::GameMem& m;
    const Golden& g;
    fs::path datDir;
    int maxReports;
    od::port::PlayerEnv env;
    std::map<int, od::GameFile> stFiles;
    uint32_t roomKey = 0xffffffff;
    size_t cursor = 0;
    int reported = 0;
    bool tickOk = true;
    bool playerStep = false; // the player port does not queue its footstep sounds (output only, see game/player)
    bool flagsOnly = false;
    bool cameraOnly = false; // room scripts: skip the event flags but 0x700..0x707, and the room script state
    bool staticOnly = false; // Maybe_UpdateStaticActors: only the ported actors (door leaves) and what they write
    bool roomCameraOnly = false; // the room setup's FUN_0040e290: only the camera bytes it writes
    std::string where;

    const od::GameFile& stFile(int stage) {
        auto it = stFiles.find(stage);
        if (it != stFiles.end()) return it->second;
        char name[16];
        std::snprintf(name, sizeof name, "ST%02X.DAT", stage);
        return stFiles[stage] = od::pcGameFile(name, od::readFile(datDir / name));
    }
    // the active grid of the loaded state, mapped where the game had its room block
    void bindRoom(const Rec& r) {
        const int stage = m.u8(0xa6d190);
        const uint32_t pool = m.u32(0x6d0084);
        // the cells pointer tells sub-grids with the same origin apart (ST19 sub 0 / 1)
        const uint32_t key = uint32_t(stage) << 24 ^ pool ^ (uint32_t(m.u8(0x6d0094)) << 8) ^ m.u8(0x6d0095) ^
                             (m.u32(0x6d0088) * 0x9e3779b1u);
        if (key != roomKey) {
            const auto block = od::roomBlock(stFile(stage));
            bool found = false;
            for (const od::RoomGrid& gr : od::roomGrids(block)) {
                if (gr.x0 != m.u8(0x6d0094) || gr.z0 != m.u8(0x6d0095)) continue;
                const od::CollisionRoom probe = od::collisionRoom(block, gr);
                const uint32_t base = pool - uint32_t(probe.pool);
                if (base + probe.segments != m.u32(0x6d008c) || base + probe.cells != m.u32(0x6d0088)) continue;
                m.map(base, block);
                const auto [shared, origin] = m.sharedAt(base, uint32_t(block.size()));
            env.room = od::collisionRoom(shared.share(), gr, origin); // the image's block itself (a copy detaches on the first write)
                found = true;
                break;
            }
            // none during a stage load (0xa6d190 already the new stage, the grid pointers still the old room's):
            // the previous room stays bound, the stage-load ticks run no collision
            (void)found;
            roomKey = key;
        }
        if (!r.seg.empty() && r.segAddr) m.write(r.segAddr, r.seg.data(), uint32_t(r.seg.size()));
    }
    void load(const Rec& r) {
        od::test::storeNativeState(m);
        size_t o = 0;
        for (const Range& rg : g.ranges) {
            m.write(rg.addr, r.blob.data() + o, rg.size);
            o += rg.size;
        }
        bindRoom(r);
        od::test::loadNativeState(m);
    }
    // bytes that the port does not reproduce by design are skipped
    // what the ported part of FUN_00512890 (flag 0x700 -> FUN_00511270(0)) writes; its per-stage handlers are not
    // ported, so the rest of the state is taken from the game afterwards
    static bool roomScriptPart(uint32_t a) {
        return (a >= 0xa725a0 && a < 0xa72660) || a == 0xa73da0 || (a >= 0xa5f280 && a < 0xa5f2a0) ||
               (a >= 0xa7adc0 && a < 0xa7b040) || (a >= kPlayer && a < kPlayer + 0x2a4) || (a >= 0xa6d2e0 && a < 0xa6d300) ||
               (a >= 0xa6bef0 && a < 0xa6bf60) || (a >= 0xa72234 && a < 0xa72238);
    }
    // a byte of a door-leaf actor (ported type in the game's or the port's state), the sound queue, the
    // static pool's pending frees
    bool staticPart(uint32_t a, const Rec& want, size_t rangeStart, const Range& rg) const {
        // (the static pool's pending-free masks 0xa6c418 are not compared: unported actors free themselves too;
        // a freed leaf shows in its byte 0)
        if ((a >= 0xa676c0 && a < 0xa67840) || (a >= 0xa7223c && a < 0xa72240) || (a >= 0xa6d33c && a < 0xa6d340))
            return true;
        if (a < 0xa67860 || a >= 0xa6ab60) return false;
        const uint32_t actor = a - (a - 0xa67860) % 0xcc;
        const size_t wo = rangeStart + (actor - rg.addr);
        const bool wantPorted = !(want.blob[wo + 3] & 8) && want.blob[wo + 4] >= 1 && want.blob[wo + 4] <= 3 &&
                                (want.blob[wo] & 1);
        return wantPorted || ((m.u8(actor) & 1) && od::port::staticActorPorted(actor));
    }
    static bool ignored(uint32_t a) {
        // the file loader: load-pending flag 0xa714c4, request queue 0xa79fe0 (OpenDash loads synchronously)
        if ((a >= 0xa714c4 && a < 0xa714c8) || (a >= 0xa79fe0 && a < 0xa79ff0)) return true;
        if (a == kPlayer + 0x86 || a == kPlayer + 0x87) return true; // matrix pad (stack garbage, see test_player)
        if (a >= 0xa6bcb8 && a < 0xa6bcbc) return true;              // FUN_00512600 stores an uninitialised local
        if (a >= kPlayer + 0x2a4 && a < kPlayer + 0x500) return true; // bone pose (test_player_pose)
        // render side, not ported: draw-list pointers 0x6d004c / 0x6d0070 / 0x6d0090, frame counter 0xa6bb88
        // (FUN_005422f0 / FUN_005efc90 ...), pose scratch 0x6d0040.., collision query scratch 0x6d00c0..0x6d013f
        if ((a >= 0x6d0040 && a < 0x6d0080) || (a >= 0x6d0090 && a < 0x6d0094) || (a >= 0xa6bb88 && a < 0xa6bb8c) ||
            (a >= 0x6d00c0 && a < 0x6d0200) || (a >= 0xa711b0 && a < 0xa71200) || // + GTE scratch, light list
            (a >= 0xa599e4 && a < 0xa59a00) || (a >= 0xa59a04 && a < 0xa59a50) || (a >= 0xa59a54 && a < 0xa59a60))
            return true; // render matrices around the fog parameters 0xa599e0 / 0xa59a00 / 0xa59a50
        if (false)
            return true;
        return false;
    }
    int compare(const Rec& want, const char* step) {
        od::test::storeNativeState(m);
        int diffs = 0;
        size_t o = 0;
        for (const Range& rg : g.ranges) {
            for (uint32_t k = 0; k < rg.size; ++k) {
                const uint32_t a = rg.addr + k;
                const uint8_t got = m.u8(a), w = want.blob[o + k];
                if (got == w || ignored(a)) continue;
                // footstep sounds / dust effects of the player are output-only no-ops in the player port
                if (playerStep && ((a >= 0xa676c0 && a < 0xa67840) || (a >= 0xa7223c && a < 0xa72240) ||
                                   (a >= 0xa6d33c && a < 0xa6d340) || (a >= 0xa6c6e0 && a < 0xa6cbe0) ||
                                   (a >= 0xa71120 && a < 0xa71128)))
                    continue;
                if (flagsOnly && !roomScriptPart(a)) continue;
                if (cameraOnly && !((a >= 0xa6bca0 && a < 0xa6bd30) || (a >= 0x6d0014 && a < 0x6d0040))) continue;
                if (staticOnly && !staticPart(a, want, o + k - (a - rg.addr), rg)) continue;
                if (roomCameraOnly && !((a >= 0xa6bcaa && a < 0xa6bcc8) || (a >= 0xa6bccc && a < 0xa6bcd8) ||
                                        (a >= 0xa6bcdc && a < 0xa6bce8) || (a >= 0xa6bd48 && a < 0xa6bd52) ||
                                        (a >= 0xa6bd70 && a < 0xa6bd76) || (a >= 0xa6bd78 && a < 0xa6bd7e)))
                    continue;
                if (diffs < 12 && reported < maxReports)
                    std::fprintf(stderr, "  tick %u %s @%s 0x%06x: want %02x got %02x\n", want.tick, step,
                                 kPointName[want.point], a, w, got);
                ++diffs;
            }
            o += rg.size;
        }
        // segment flags: unported static actors toggle them too (ST10/0 at tick 3): not compared after them
        if (!staticOnly && !want.seg.empty() && want.segAddr && m.mapped(want.segAddr, uint32_t(want.seg.size())))
            for (uint32_t k = 7; k < want.seg.size(); k += 8) // +3 of each entry: flags the scripts toggle
                if (m.u8(want.segAddr + k) != want.seg[k]) {
                    if (diffs < 12 && reported < maxReports)
                        std::fprintf(stderr, "  tick %u %s segment byte 0x%06x: want %02x got %02x\n", want.tick, step,
                                     want.segAddr + k, want.seg[k], m.u8(want.segAddr + k));
                    ++diffs;
                }
        if (diffs) tickOk = false;
        return diffs;
    }
    // the next record must be `point`: compare (and optionally reload it afterwards)
    const Rec* expect(uint8_t point, const char* step, bool compareIt = true) {
        while (cursor < g.recs.size() &&
               (((g.recs[cursor].point == 10 || g.recs[cursor].point == 11) && point != 10 && point != 11) ||
                g.recs[cursor].point == 19))
            ++cursor; // FUN_00542370 runs inline in the port; 19 = before the room scripts (informational)
        if (cursor >= g.recs.size() || g.recs[cursor].point != point) {
            if (reported < maxReports)
                std::fprintf(stderr, "  %s: expected point %s, golden has %s\n", step, kPointName[point],
                             cursor < g.recs.size() ? kPointName[g.recs[cursor].point] : "end");
            tickOk = false;
            throw std::runtime_error("point sequence");
        }
        const Rec& r = g.at(cursor++);
        if (compareIt) compare(r, step);
        return &r;
    }
};

} // namespace

int main(int argc, char** argv) {
    od::content::registerAll(); // the game code dispatches into the content through its registry
    if (argc < 4) return 2;
    const fs::path datDir = argv[1], exe = argv[2], data = argv[3];
    od::bindTestDataSource(exe);
    od::port::setModernPlay(false); // the goldens hold the exe's gameplay (game/modern_play)
    const int maxReports = argc > 4 ? std::stoi(argv[4]) : 3;
    std::vector<fs::path> files;
    for (const auto& de : fs::directory_iterator(data))
        if (de.path().extension() == ".bin" && de.path().filename().string().rfind("trans_", 0) == 0)
            files.push_back(de.path());
    std::sort(files.begin(), files.end());
    CHECK(!files.empty());
    for (const fs::path& path : files) {
        od::GameMem m;
        const od::ScopedGameMemory bound(m);
        m.mapExeData(exe);
        m.mapDatPsx(od::readFile(datDir / "PL00P000.DAT"));
        Golden g;
        if (!readGolden(path, m, g)) {
            std::fprintf(stderr, "%s: not a TRG1 golden\n", path.filename().string().c_str());
            CHECK(false);
            continue;
        }
        for (const Range& r : g.ranges)
            if (!m.mapped(r.addr, r.size)) m.mapZero(r.addr, r.size);
        od::test::loadNativeState(m); // the native state from this golden's image (nothing of the previous one)
        Runner run{m, g, datDir, maxReports};
        int roomCameras = 0;
        od::port::FadeTask fade;
        od::port::TransitionHooks hooks;
        hooks.frameStart = [&] { run.expect(1, "pre-frame"); };
        hooks.frame = [&] {
            run.expect(2, "script runner");
            run.load(*run.expect(3, "frame body", false));
            od::port::playerInputAndDispatch(run.env);
            run.playerStep = true;
            run.expect(4, "player dispatch");
            run.playerStep = false;
            // the actor updates before Maybe_UpdateStaticActors are not ported: the state at its call (point 23),
            // then the door-leaf actors (ported types) are compared at its end (point 5)
            run.load(*run.expect(23, "actor updates", false));
            od::port::staticActorsUpdate();
            run.staticOnly = true;
            const Rec* r5 = run.expect(5, "static actors");
            run.staticOnly = false;
            run.load(*r5);
            od::port::actorCollision();
            run.playerStep = true;
            run.expect(6, "actor collision");
            run.playerStep = false;
            od::port::playerUpdate(run.env);
            run.playerStep = true;
            run.expect(7, "player update");
            run.playerStep = false;
            // the camera (Maybe_UpdateCameraSystem: mode 0 follow, mode 1 script sequences) on the state after the
            // player: compared at the end of the frame body (camera struct 0xa6bca0.., view 0x6d0014..)
            od::port::cameraUpdate();
            run.cameraOnly = true;
            const Rec* r8 = run.expect(8, "camera");
            run.cameraOnly = false;
            run.load(*r8);
        };
        hooks.roomSetup = [&](int param) {
            run.expect(param < 0 ? 15 : 13, "room init");
            // the per-stage room setup is not ported, but its camera part is (FUN_0040e290(table + sub * 12) when
            // param >= 0, the room's record of the data source: game/camera roomCamera): compared on the bytes it writes
            const std::optional<od::port::RoomCamera> camera =
                param >= 0 ? od::port::roomCamera(m.u8(0xa6d190), m.u8(0xa6d191)) : std::nullopt;
            const bool table = camera.has_value();
            if (table) od::port::cameraRoomInit(*camera);
            run.roomCameraOnly = table;
            const Rec* r = run.expect(param < 0 ? 16 : 14, "room setup camera", table);
            run.roomCameraOnly = false;
            if (table) ++roomCameras;
            run.load(*r);
        };
        hooks.roomScripts = [&] {
            // the per-stage room handlers of FUN_00512890 are not ported: compare all but their event flags /
            // room script state, then take the game's state
            run.flagsOnly = true;
            run.expect(20, "room scripts");
            run.flagsOnly = false;
            run.load(g.at(run.cursor - 1));
        };
        hooks.render = [&] {
            run.expect(21, "pre-render");
            run.load(*run.expect(22, "render", false));
        };
        hooks.camera = [&] {
            run.expect(17, "pre-camera");
            run.load(*run.expect(18, "camera", false));
        };
        int ticks = 0, failed = 0, fadeChecks = 0;
        Rec last9Copy; // the state at the last point 9 (a copy: the reader decodes one record at a time)
        const Rec* last9 = nullptr;
        bool fadeFromPort = false;
        while (run.cursor < g.recs.size()) {
            const Meta& r = g.recs[run.cursor];
            if (r.point != 0 && r.point != 12 && r.point != 10) {
                if (r.point == 9) {
                    last9Copy = g.at(run.cursor);
                    last9 = &last9Copy;
                }
                ++run.cursor;
                continue;
            }
            run.tickOk = true;
            try {
                if ((r.point == 0 || r.point == 12) && last9 && fade.active && m.u8(0xa72260) <= 6 && fadeFromPort) {
                    // the fade task between two game task frames (types 0/1 = 0x53f950, wipes 2..6 = 0x53fa40..)
                    run.load(*last9);
                    od::port::fadeTick(fade);
                    od::test::storeNativeState(m);
                    std::vector<uint32_t> fadeBytes;
                    for (uint32_t a = 0xa72260; a < 0xa72272; ++a) fadeBytes.push_back(a);
                    // the wipes' display-list / draw-environment state (phases 0 / 2)
                    for (uint32_t a : {0x6d0009u, 0x6d000au, 0x6d000bu, 0xa72248u, 0xa59fe8u, 0xa723c2u, 0xa723d6u,
                                       0xa723deu, 0xa72452u, 0xa72466u, 0xa7246eu, 0xa723ecu, 0xa7247cu})
                        fadeBytes.push_back(a);
                    for (uint32_t a : fadeBytes) {
                        size_t o = 0;
                        for (const Range& rg : g.ranges) {
                            if (a >= rg.addr && a < rg.addr + rg.size) {
                                const uint8_t w = g.at(run.cursor).blob[o + (a - rg.addr)];
                                if (m.u8(a) != w) {
                                    if (run.reported < maxReports)
                                        std::fprintf(stderr, "  tick %u fade task 0x%06x: want %02x got %02x\n", r.tick,
                                                     a, w, m.u8(a));
                                    run.tickOk = false;
                                }
                                break;
                            }
                            o += rg.size;
                        }
                    }
                    ++fadeChecks;
                }
                ++run.cursor;
                run.load(g.at(run.cursor - 1));
                if (r.point == 10) { // mode 1: FUN_00542370 only
                    od::port::arrivalPlacement();
                    run.expect(11, "arrival placement");
                } else {
                    const bool wasActive = fade.active;
                    if (m.u8(0xa6d180) == 3) od::port::fieldMode(run.env, hooks, fade);
                    else od::port::roomLoadedMode(run.env, hooks, fade);
                    fadeFromPort = fade.active && (!wasActive || fadeFromPort);
                    last9Copy = *run.expect(9, "mode handler");
                    last9 = &last9Copy;
                }
            } catch (const std::exception& e) {
                if (std::string(e.what()) != "point sequence" && run.reported < maxReports)
                    std::fprintf(stderr, "  tick %u: %s\n", r.tick, e.what());
                run.tickOk = false;
                while (run.cursor < g.recs.size() && g.recs[run.cursor].point != 9) ++run.cursor;
            }
            ++ticks;
            if (!run.tickOk) {
                ++failed;
                ++run.reported;
            }
        }
        std::printf("%s: %d ticks: %d ok, %d failing (%d fade checks, %d room camera inits)\n",
                    path.filename().string().c_str(), ticks, ticks - failed, failed, fadeChecks, roomCameras);
        CHECK(failed == 0);
    }
    return testResult("test_transition");
}
