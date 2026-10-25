// PlayerSnapshot vs. the game: for every tick of every tests/data/player_ST*.bin golden (see test_player.cpp), capture a
// snapshot of the struct the game ended the tick with, apply it to a copy of the image at another actor
// address, pose it there and compare the pose outputs (+0x74..+0x94, +0x2b0..size) with the game's. The
// snapshot must also round-trip.
//   test_player_snapshot <dat dir> <dash2.exe> <tests/data>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "check.hpp"
#include "data_source_binding.hpp"
#include "plg_pose.hpp"
#include "content/registry.hpp"
#include "engine/dat.hpp"
#include "engine/data_source.hpp"
#include "translator/gamemem.hpp"
#include "translator/game_memory.hpp"
#include "game/loaded_data_fingerprint.hpp"
#include "game/player/player_model.hpp"
#include "game/player/player_pose.hpp"
#include "game/player/player_snapshot.hpp"
#include "game/player/player_state.hpp"
#include "translator/state_translator.hpp"

namespace fs = std::filesystem;

namespace {
constexpr uint32_t kPlayer = 0xa71d20;
constexpr uint32_t kActor = 0x7c000000, kFrames = 0x7c000800;
// the remote player (posed from a snapshot; its outputs written into its own image at kActor by the translator)
od::port::Player& remoteActor() {
    static od::port::Player& actor = []() -> od::port::Player& {
        static od::port::Player a;
        a.at = kActor;
        return a;
    }();
    return actor;
}

// No golden has a tick with an animation id >= 0x80 (stage animation: frames read from the table at +0x298), so
// check that path differentially: a local actor posed from a synthetic table in the image vs. the ghost built
// from its snapshot, and that the ghost depends on stageAnimBase (another table gives another pose).
constexpr uint32_t kTable = 0x7d000000, kTable2 = 0x7d001000;

void fillTable(od::GameMem& m, uint32_t base, uint32_t seed) {
    m.mapZero(base, 0x1000);
    m.set32(base + 8, 0x40 * 4); // step 0: first frame at word 0x40 (rel = value >> 2)
    uint32_t x = seed;
    for (uint32_t o = 0x100; o < 0x400; o += 4) { // frames of 16 dwords from word 0x40 on
        x = x * 1664525u + 1013904223u;
        m.set32(base + o, x);
    }
}

int poseDiffs(const od::GameMem& a, uint32_t actorA, const od::GameMem& b, uint32_t actorB) {
    int diffs = 0;
    for (uint32_t o = 0x2b0; o < 0x4f0; ++o)
        if (!isPad(o) && a.u8(actorA + o) != b.u8(actorB + o)) ++diffs;
    return diffs;
}

void stageAnimCase(od::GameMem local) {
    const od::ScopedGameMemory boundLocal(local);
    fillTable(local, kTable, 1);
    fillTable(local, kTable2, 99);
    for (uint32_t ch = 0; ch < 2; ++ch) {
        local.set8(kPlayer + 0xa0 + ch * 0x10, 0x80);
        local.set8(kPlayer + 0xa2 + ch * 0x10, 0);
        local.set8(local.u32(kPlayer + 0x98 + ch * 0x10), 1 + ch);
    }
    local.set8(kPlayer + 0x2a5, 0); // no blend: keep the case on the plain decode
    local.set8(kPlayer + 0x2a7, 0);
    local.set32(kPlayer + 0x298, kTable);
    od::test::loadPlayerAt(local, kPlayer, od::port::player());
    od::port::playerPose(od::port::player());
    od::test::storePlayerAt(local, kPlayer, od::port::player());
    const auto snap = od::port::capturePlayerSnapshot(od::port::player());
    CHECK(snap.animId[0] == 0x80 && snap.stageAnimBase == kTable);

    od::GameMem ghost = local;
    ghost.mapZero(kActor, 0x1000);
    const od::ScopedGameMemory boundGhost(ghost);
    od::port::applyPlayerSnapshot(remoteActor(), kFrames, snap);
    od::port::playerPose(remoteActor());
    od::test::storePlayerAt(ghost, kActor, remoteActor());
    const int diffs = poseDiffs(ghost, kActor, local, kPlayer);
    CHECK(diffs == 0);

    auto other = snap;
    other.stageAnimBase = kTable2;
    od::port::applyPlayerSnapshot(remoteActor(), kFrames, other);
    od::port::playerPose(remoteActor());
    od::test::storePlayerAt(ghost, kActor, remoteActor());
    const int moved = poseDiffs(ghost, kActor, local, kPlayer);
    CHECK(moved > 0);
    std::printf("stage animation case (ids 0x80): %d diffs vs local, %d bytes change with another table\n", diffs, moved);
}

struct Totals { uint32_t ticks = 0; int bad = 0, roundTripBad = 0, wireBad = 0, highAnim = 0; bool stageAnimDone = false; };

bool runFile(const fs::path& path, const fs::path& datDir, const fs::path& exe, Totals& t) {
    PlgPose g;
    if (!g.load(path)) {
        std::fprintf(stderr, "%s: not a PLG2/PLG3 golden\n", path.filename().string().c_str());
        return false;
    }
    const uint32_t count = g.count, size = g.size;
    const auto& ranges = g.ranges;

    od::GameMem local;
    local.mapExeData(exe);
    local.mapDatPsx(od::readFile(datDir / "PL00P000.DAT"));
    local.mapZero(kPlayer, size);
    for (const PlgRange& r : ranges) local.mapZero(r.addr, r.size);
    const od::ScopedGameMemory boundLocal(local);

    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* r = g.record(i);
        const uint8_t* after = g.after(r);
        g.apply(local, r, kPlayer);
        od::test::loadPlayerAt(local, kPlayer, od::port::player());
        const auto snap = od::port::capturePlayerSnapshot(od::port::player());
        ++t.ticks;
        { // the netplay payload of this tick round-trips byte for byte
            const od::port::RemotePlayerState sent{{9, 2, true}, 0x0123456789abcdefull, snap};
            const auto bytes = od::port::encodePlayerState(sent);
            const auto back = od::port::decodePlayerState(bytes);
            if (bytes.size() != od::port::kRemoteStateBytes || !back || !(*back == sent)) ++t.wireBad;
        }
        if (snap.animId[0] >= 0x80 || snap.animId[1] >= 0x80) ++t.highAnim;

        od::GameMem ghost = local; // the ghost image: a copy of the local one
        ghost.mapZero(kActor, 0x1000);
        const od::ScopedGameMemory boundGhost(ghost);
        od::port::applyPlayerSnapshot(remoteActor(), kFrames, snap);
        try {
            od::port::playerPose(remoteActor());
        } catch (const std::exception& e) {
            std::fprintf(stderr, "tick %u: %s\n", od::rd32(r), e.what());
            ++t.bad;
            continue;
        }
        od::test::storePlayerAt(ghost, kActor, remoteActor());
        if (!sameOutputs(ghost, kActor, after, size, od::rd32(r))) ++t.bad;
        if (!(od::port::capturePlayerSnapshot(remoteActor()) == snap)) ++t.roundTripBad;
    }
    if (!t.stageAnimDone) { // `local` still holds the last tick of the first golden
        t.stageAnimDone = true;
        stageAnimCase(local);
    }
    return true;
}
// decodePlayerState refuses what is not a payload of the layout: other sizes, layouts, stages, flag bytes
bool wireRejectCase() {
    od::port::RemotePlayerState s;
    s.room = {8, 2, true};
    s.snapshot.pos[0] = -0x12345678;
    s.snapshot.yaw = 0xfff;
    const auto ok = od::port::encodePlayerState(s);
    bool good = od::port::decodePlayerState(ok).has_value();
    for (size_t n = 0; n < ok.size(); ++n) good &= !od::port::decodePlayerState(std::span(ok.data(), n));
    auto longer = ok;
    longer.push_back(0);
    good &= !od::port::decodePlayerState(longer);
    auto layout = ok;
    layout[0] = od::port::kRemoteStateLayout + 1;
    good &= !od::port::decodePlayerState(layout);
    auto stage = ok;
    stage[1] = uint8_t(od::kStageCount);
    good &= !od::port::decodePlayerState(stage);
    auto flag = ok;
    flag[3] = 2; // fieldPlay
    good &= !od::port::decodePlayerState(flag);
    return good;
}

// the loaded-data fingerprint tells the stage files and the script banks apart
bool fingerprintCase() {
    using od::port::loadedDataFingerprint;
    const od::port::LoadedDataIdentity a{"ST53.DAT", "ST53T.DAT", {"", ""}};
    od::port::LoadedDataIdentity b = a, c = a, d = a;
    b.stageGeometry = "ST5301.DAT";
    c.altBanks[1] = "ST3A02.DAT";
    d.stageGeometry = "ST53.DATS"; // the separator: "ST53.DATS" + "T.DAT" is not "ST53.DAT" + "ST53T.DAT"
    d.stageTexture = "T53T.DAT";
    return loadedDataFingerprint(a) == loadedDataFingerprint(od::port::LoadedDataIdentity{a}) &&
           loadedDataFingerprint(a) != loadedDataFingerprint(b) && loadedDataFingerprint(a) != loadedDataFingerprint(c) &&
           loadedDataFingerprint(a) != loadedDataFingerprint(d);
}

// playerLookFiles (pure, exe tables only) names the same files as the game's request for every look
bool lookFilesCase(const fs::path& exe) {
    od::GameMem exeMem;
    exeMem.mapExeData(exe);
    const auto def = od::playerLookFiles(od::port::PlayerLook{});
    CHECK(def.has_value());
    CHECK(def->body == "PL00P000.DAT");
    CHECK(def->weapon.empty());
    // every special weapon 3..17 has a PL00R entry (nonzero index) in 0x8d37e2
    for (int w = 3; w <= 17; ++w) CHECK(exeMem.u16(0x8d37e2 + uint32_t(w) * 2) != 0);
    int n = 0;
    for (int costume = 0; costume <= 2; ++costume)
        for (int variant = 0; variant <= 5; ++variant) {
            if (costume * 6 + variant >= 17) continue; // body table 0x8d37b4..0x8d37d6 = 17 entries
            for (int weapon = 0; weapon <= 17; ++weapon) {
                od::port::PlayerLook look;
                look.costume = uint8_t(costume);
                look.costumeVariant = uint8_t(variant);
                look.specialWeapon = int8_t(weapon);
                od::GameMem mem = exeMem;
                mem.set8(kPlayer + 0x4, 0);
                mem.set8(kPlayer + 0x1a2, costume);
                mem.set8(kPlayer + 0x1a3, variant);
                mem.set8(kPlayer + 0x18e, weapon);
                mem.set16(0xa6d1d8, 0);
                mem.set16(0xa6d1da, 0);
                const od::ScopedGameMemory bound(mem);
                od::test::loadNativeState(mem); // the player and the request as set above
                od::playerDatRequest();
                od::test::storeNativeState(mem);
                const std::string body = od::datFileName(mem.u16(0xa6d1d8));
                const std::string wpn = weapon > 2 ? od::datFileName(mem.u16(0xa6d1da)) : std::string();
                const auto f = od::playerLookFiles(look);
                CHECK(f.has_value());
                CHECK(f->body == body);
                CHECK(f->weapon == wpn);
                CHECK(!f->body.empty());
                CHECK(f->weapon.empty() == (weapon <= 2));
                ++n;
            }
        }
    od::port::PlayerLook other;
    other.kind = 1;
    CHECK(!od::playerLookFiles(other).has_value());
    od::port::PlayerLook oob; // outside the exe tables: no files, never an out-of-table read
    oob.specialWeapon = 18;
    CHECK(!od::playerLookFiles(oob).has_value());
    oob = od::port::PlayerLook{};
    oob.costume = 3; // 3 * 6 = 18 >= 17 entries
    CHECK(!od::playerLookFiles(oob).has_value());
    oob = od::port::PlayerLook{};
    oob.costume = 2;
    oob.costumeVariant = 5; // 17
    CHECK(!od::playerLookFiles(oob).has_value());
    oob = od::port::PlayerLook{};
    oob.costume = 255; // int8 -1: a negative index
    CHECK(!od::playerLookFiles(oob).has_value());
    // the user's save (+0x1a2 = 2, +0x1a3 = 1): the game loads PL00P001 (cross-checked with playerDatRequest above)
    od::port::PlayerLook save;
    save.costume = 2;
    save.costumeVariant = 1;
    const auto sf = od::playerLookFiles(save);
    CHECK(sf.has_value());
    CHECK(sf->body == "PL00P001.DAT");
    std::printf("%d looks: playerLookFiles == playerDatRequest\n", n);
    return true;
}
// capture outside field play: the frame pointers may be 0 / unmapped; it must not throw and is not drawable
bool unmappedFrameCase() {
    od::GameMem m;
    const od::ScopedGameMemory bound(m);
    m.mapZero(kActor, 0x500);
    m.set8(kActor + 0, 1);
    m.set8(kActor + 1, 3); // would be drawn
    m.set32(kActor + 0x98, 0);
    m.set32(kActor + 0xa8, 0);
    od::test::loadPlayerAt(m, kActor, remoteActor());
    bool threw = false;
    od::port::PlayerSnapshot s;
    try {
        s = od::port::capturePlayerSnapshot(remoteActor());
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(!threw);
    CHECK(s.drawFlags[0] == 0 && s.drawFlags[1] == 0);
    CHECK(s.animFrame[0] == 0 && s.animFrame[1] == 0);
    od::port::PlayerSnapshot fresh; // defaults are defined
    CHECK(fresh.pos[0] == 0 && fresh.stageAnimBase == 0 && fresh.face[1] == 0);
    return true;
}
} // namespace

int main(int argc, char** argv) {
    od::content::registerAll();
    if (argc < 4) return 2;
    const fs::path datDir = argv[1], exe = argv[2], data = argv[3];
    od::bindTestDataSource(exe);
    // every golden scenario player_ST<stage>[_name].bin: ST09 is the base; the others add the animations a
    // snapshot must also carry (stage animations with ids >= 0x80, hurt, ledge, doors ...)
    std::vector<fs::path> files;
    for (const auto& de : fs::directory_iterator(data))
        if (de.path().extension() == ".bin" && de.path().filename().string().rfind("player_ST", 0) == 0)
            files.push_back(de.path());
    std::sort(files.begin(), files.end());
    CHECK(!files.empty());
    CHECK(lookFilesCase(exe));
    CHECK(unmappedFrameCase());
    CHECK(wireRejectCase());
    CHECK(fingerprintCase());
    Totals t;
    for (const fs::path& f : files) CHECK(runFile(f, datDir, exe, t));
    std::printf("%zu goldens, %u ticks, %d with pose differences, %d round-trip mismatches, %d payload mismatches, "
                "%d ticks with anim id >= 0x80\n",
                files.size(), t.ticks, t.bad, t.roundTripBad, t.wireBad, t.highAnim);
    CHECK(t.bad == 0);
    CHECK(t.roundTripBad == 0);
    CHECK(t.wireBad == 0);
    return testResult("test_player_snapshot");
}
