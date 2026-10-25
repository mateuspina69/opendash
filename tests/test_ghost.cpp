// GhostSet vs. the game: the ST09 pose golden posed through a ghost (outputs equal the golden's), the local state
// untouched, the weapon file of the look mapped, rebuild on a new generation only, and the hidden reasons.
//   test_ghost <dat dir> <dash2.exe> <tests/data>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <vector>

#include "check.hpp"
#include "data_source_binding.hpp"
#include "plg_pose.hpp"
#include "content/registry.hpp"
#include "engine/dat.hpp"
#include "engine/file_data.hpp"
#include "translator/gamemem.hpp"
#include "translator/game_memory.hpp"
#include "game/ghost.hpp"
#include "game/player/player_model.hpp"
#include "game/player/player_model_state.hpp"
#include "game/player/player_snapshot.hpp"
#include "game/player/player_state.hpp"
#include "translator/state_translator.hpp"

namespace fs = std::filesystem;

namespace {
constexpr uint32_t kPlayer = 0xa71d20;
// Marker address for the generation case: a loaded-file block of the local files of its own, outside every type-0
// segment of the look files used here (those are PS1 RAM, below 0x830000; checked in main), so the look mapping of a
// rebuilt ghost can never overwrite it.
constexpr uint32_t kMarker = 0x7e800000;

uint8_t markerOf(const od::LoadedFileData& files) { return files.reader(kMarker).read<uint8_t>(0); }
void setMarker(od::LoadedFileData& files, uint8_t v) { files.map(kMarker, std::vector<uint8_t>{v}); }

// the local state the ghost must never write, in the exe's layout: the player struct, 0x6d00c0..0x6d01b0,
// 0xa7aa80..0xa7ad60 (the pose scratch and the matrix stack)
std::vector<uint8_t> localBytes(od::GameMem& image) {
    od::test::storeNativeState(image);
    std::vector<uint8_t> b;
    auto add = [&](uint32_t lo, uint32_t n) {
        const auto v = image.view(lo, n);
        b.insert(b.end(), v.begin(), v.end());
    };
    add(kPlayer, 0x500);
    add(0x6d00c0, 0x6d01b0 - 0x6d00c0);
    add(0xa7aa80, 0xa7ad60 - 0xa7aa80);
    return b;
}

// the ghost's posed player in the exe's layout at its own address (an image of its own)
od::GameMem ghostImage(const od::ghost::GhostSet& set, int slot) {
    od::GameMem m;
    m.mapZero(od::ghost::actorAddr(slot), 0x1000);
    od::test::storePlayerAt(m, od::ghost::actorAddr(slot), const_cast<od::port::Player&>(*set.player(slot)));
    return m;
}
} // namespace

int main(int argc, char** argv) {
    od::content::registerAll();
    if (argc < 4) return 2;
    const fs::path datDir = argv[1], exe = argv[2], data = argv[3];
    od::bindTestDataSource(exe);
    std::map<std::string, od::GameFile> datFiles; // each file read once (ReadFile hands out references)
    const od::ghost::ReadFile read = [&](const std::string& name) -> const od::GameFile& {
        auto it = datFiles.find(name);
        if (it == datFiles.end()) it = datFiles.emplace(name, od::pcGameFile(name, od::readFile(datDir / name))).first;
        return it->second;
    };

    PlgPose g;
    CHECK(g.load(data / "player_ST09.bin"));
    const uint32_t count = g.count, size = g.size;

    od::GameMem exeMem;
    exeMem.mapExeData(exe);
    od::GameMem local = exeMem;
    const od::ScopedGameMemory bound(local); // the local image and its loaded files: the ghosts are built from them
    local.mapDatPsx(od::readFile(datDir / "PL00P000.DAT"));
    local.mapZero(kPlayer, size);
    local.mapZero(kMarker, 0x10);
    for (const PlgRange& r : g.ranges) local.mapZero(r.addr, r.size);
    CHECK(local.mapped(kPlayer, 0x500) && local.mapped(0x6d00c0, 0xf0) && local.mapped(0xa7aa80, 0x2e0));

    // --- ST09 golden through ghost slot 0 ---
    od::ghost::GhostSet gs;
    int bad = 0, hidden = 0, touched = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* r = g.record(i);
        const uint8_t* after = g.after(r);
        g.apply(local, r, kPlayer);
        od::test::loadNativeState(local);
        const auto snap = od::port::capturePlayerSnapshot(od::port::player());
        const auto before = localBytes(local);
        const auto h = gs.pose(0, 1, snap, read);
        if (h != od::ghost::Hidden::No) { ++hidden; continue; }
        if (localBytes(local) != before) ++touched;
        if (!sameOutputs(ghostImage(gs, 0), od::ghost::actorAddr(0), after, size, od::rd32(r))) ++bad;
    }
    std::printf("ST09: %u ticks, %d hidden, %d with pose differences, %d with local bytes touched\n", count, hidden, bad, touched);
    CHECK(hidden == 0);
    CHECK(bad == 0);
    CHECK(touched == 0);

    // --- a look with special weapon 3: the PL00R file is mapped ---
    auto snap = od::port::capturePlayerSnapshot(od::port::player());
    snap.look = od::port::PlayerLook{}; // kind 0, costume 1 / variant 0 = PL00P000 (the body mapped in `local`)
    snap.look.specialWeapon = 3;
    const auto files = od::playerLookFiles(snap.look);
    CHECK(files && files->body == "PL00P000.DAT" && !files->weapon.empty());
    CHECK(gs.pose(1, 1, snap, read) == od::ghost::Hidden::No);
    CHECK(gs.files(1) != nullptr);
    od::LoadedFileData ref = od::loadedFiles();
    const auto weaponDat = od::readFile(datDir / files->weapon);
    ref.mapCode(od::pcGameFile(files->weapon, weaponDat));
    uint32_t first = 0;
    for (const od::DatSegment& s : od::readContainer(weaponDat))
        if (s.type == 0 && s.arg != 0) { first = od::LoadedFileData::kPsxBase + s.arg; break; }
    CHECK(first != 0);
    const auto a = gs.files(1)->view(first, 64), b = ref.view(first, 64);
    CHECK(std::equal(a.begin(), a.end(), b.begin()));
    // the weapon arm (PS1 0x113340) is not in PL00P000: it must be unmapped or different locally
    CHECK(!od::loadedFiles().mapped(first, 64) ||
          !std::equal(a.begin(), a.end(), od::loadedFiles().view(first, 64).begin()));
    std::printf("weapon 3 look: %s mapped at 0x%08x\n", files->weapon.c_str(), first);

    // --- generation: rebuilt only when it changes ---
    // the marker must not lie in any type-0 segment of the look files used (PL00P000, the PL00R above)
    for (const auto& bytes : {od::readFile(datDir / "PL00P000.DAT"), weaponDat})
        for (const od::DatSegment& s : od::readContainer(bytes))
            if (s.type == 0 && s.arg != 0)
                CHECK(!(kMarker >= od::LoadedFileData::kPsxBase + s.arg &&
                        kMarker < od::LoadedFileData::kPsxBase + s.arg + s.size));
    CHECK(od::loadedFiles().mapped(kMarker));
    snap.look = od::port::PlayerLook{};
    setMarker(od::loadedFiles(), 1);
    CHECK(gs.pose(0, 2, snap, read) == od::ghost::Hidden::No);
    CHECK(markerOf(*gs.files(0)) == 1);
    setMarker(od::loadedFiles(), 2);
    CHECK(gs.pose(0, 2, snap, read) == od::ghost::Hidden::No);
    CHECK(markerOf(*gs.files(0)) == 1); // same generation: not rebuilt
    CHECK(gs.pose(0, 3, snap, read) == od::ghost::Hidden::No);
    CHECK(markerOf(*gs.files(0)) == 2); // new generation: rebuilt
    setMarker(od::loadedFiles(), 3);
    auto other = snap;
    other.look.costumeVariant = 1;
    CHECK(gs.pose(0, 3, other, read) == od::ghost::Hidden::No);
    CHECK(markerOf(*gs.files(0)) == 3); // look change: rebuilt

    // --- hidden reasons ---
    auto hid = snap;
    hid.drawFlags[0] = hid.drawFlags[1] = 0;
    CHECK(gs.pose(2, 1, hid, read) == od::ghost::Hidden::NotVisible);
    CHECK(gs.files(2) == nullptr);
    auto k1 = snap;
    k1.look.kind = 1;
    CHECK(gs.pose(2, 1, k1, read) == od::ghost::Hidden::UnsupportedCharacter);
    CHECK(gs.files(2) == nullptr);
    const od::ghost::ReadFile badRead = [](const std::string&) -> const od::GameFile& { throw std::runtime_error("gone"); };
    CHECK(gs.pose(2, 1, snap, badRead) == od::ghost::Hidden::MissingFile);
    CHECK(gs.files(2) == nullptr);
    // --- NotVisible keeps the files: hide -> show with the same generation / look does not rebuild ---
    int reads = 0;
    const od::ghost::ReadFile counting = [&](const std::string& n) -> const od::GameFile& {
        ++reads;
        return read(n);
    };
    od::ghost::GhostSet g2;
    CHECK(g2.pose(0, 1, snap, counting) == od::ghost::Hidden::No);
    const int firstReads = reads;
    // the look's body, plus each local player file (0xa6d1d8 / 0xa6d1da) that is not one of the look's (cleared)
    int expectReads = 1;
    for (const uint16_t i : od::playerDatLoaded()) {
        const std::string n = od::datFileName(i);
        expectReads += !n.empty() && n != "PL00P000.DAT";
    }
    CHECK(firstReads == expectReads);
    setMarker(*g2.files(0), 0x5a); // survives only if the files are kept
    CHECK(g2.pose(0, 1, hid, counting) == od::ghost::Hidden::NotVisible);
    CHECK(g2.files(0) != nullptr);
    CHECK(g2.pose(0, 1, snap, counting) == od::ghost::Hidden::No);
    CHECK(reads == firstReads && markerOf(*g2.files(0)) == 0x5a);
    CHECK(markerOf(od::loadedFiles()) == 3); // the ghost's write is its own

    // --- MissingFile: one read per (generation, look); a new generation retries ---
    int fails = 0;
    const od::ghost::ReadFile failing = [&](const std::string&) -> const od::GameFile& {
        ++fails;
        throw std::runtime_error("gone");
    };
    od::ghost::GhostSet g3;
    for (int i = 0; i < 5; ++i) CHECK(g3.pose(0, 1, snap, failing) == od::ghost::Hidden::MissingFile);
    CHECK(fails == 1);
    CHECK(g3.pose(0, 2, snap, failing) == od::ghost::Hidden::MissingFile);
    CHECK(fails == 2);
    CHECK(g3.pose(0, 2, other, failing) == od::ghost::Hidden::MissingFile);
    CHECK(fails == 3);
    CHECK(g3.pose(0, 2, other, read) == od::ghost::Hidden::MissingFile); // same key: not retried even if it would work
    CHECK(g3.pose(0, 3, other, read) == od::ghost::Hidden::No);

    // --- a look without a PL00R never poses from the local's weapon data ---
    // Two local images that differ only in the special-weapon file the local player holds (none / PL00R0B, recorded
    // in 0xa6d1da as Maybe_RequestPlayerFiles 0x5d5c40 does): the same no-weapon snapshot must give the same pose,
    // on the ST09 ticks and on the special-weapon arm animations 0x60 / 0x61 (channel 1, table 0x751e20 = PS1
    // 0x121e20, which the PL00R brings; FUN_005722c0 picks them with the special weapon armed)
    {
        od::port::PlayerLook armed;
        armed.specialWeapon = 12;
        const auto wf = od::playerLookFiles(armed);
        CHECK(wf && !wf->weapon.empty());
        const auto wdat = od::readFile(datDir / wf->weapon);
        od::GameMem noWeapon = local, withWeapon = local;
        for (const od::DatSegment& s : od::readContainer(wdat)) // no PL00R loaded: those bytes zero
            if (s.type == 0 && s.arg != 0)
                noWeapon.map(od::LoadedFileData::kPsxBase + s.arg, std::vector<uint8_t>(s.size, 0));
        withWeapon.mapDatPsx(wdat);
        const uint16_t weaponIndex = exeMem.u16(0x8d37e2 + 12 * 2);
        CHECK(noWeapon.u32(0x751e20) != withWeapon.u32(0x751e20)); // the arm table differs between the two
        od::ghost::GhostSet ga, gb;
        int differ = 0, posed = 0;
        // the local image this ghost set is built from, with the local player's weapon file record
        auto poseOn = [&](od::GameMem& image, uint16_t heldWeapon, od::ghost::GhostSet& set,
                          const od::port::PlayerSnapshot& s) {
            const od::ScopedGameMemory boundImage(image);
            od::port::player_model_state::playerFiles().weapon = heldWeapon;
            return set.pose(0, 1, s, read);
        };
        auto poseBoth = [&](od::port::PlayerSnapshot s) {
            s.look = od::port::PlayerLook{}; // PL00P000, no PL00R
            const auto ha = poseOn(noWeapon, 0, ga, s), hb = poseOn(withWeapon, weaponIndex, gb, s);
            CHECK(ha == od::ghost::Hidden::No && hb == od::ghost::Hidden::No);
            if (ha != od::ghost::Hidden::No || hb != od::ghost::Hidden::No) return;
            ++posed;
            const uint32_t at = od::ghost::actorAddr(0);
            const od::GameMem ia = ghostImage(ga, 0), ib = ghostImage(gb, 0);
            const auto a1 = ia.view(at + 0x74, 0x20), b1 = ib.view(at + 0x74, 0x20);
            const auto a2 = ia.view(at + 0x2b0, size - 0x2b0), b2 = ib.view(at + 0x2b0, size - 0x2b0);
            if (!std::equal(a1.begin(), a1.end(), b1.begin()) || !std::equal(a2.begin(), a2.end(), b2.begin())) ++differ;
        };
        for (uint32_t i = 0; i < count; ++i) {
            g.apply(local, g.record(i), kPlayer);
            od::test::loadNativeState(local);
            poseBoth(od::port::capturePlayerSnapshot(od::port::player()));
        }
        auto arm = od::port::capturePlayerSnapshot(od::port::player());
        arm.channelMix = 1; // +0xc0 = 1: the arms (bones 2..4 / 5..7) take channel 1 (FUN_005e25b0)
        for (int id = 0x60; id <= 0x61; ++id) { // the arm animations' first frame
            arm.animId[1] = uint8_t(id);
            arm.animStep[1] = 0;
            arm.animFrame[1] = 0;
            poseBoth(arm);
        }
        std::printf("no-weapon look over two local weapon images: %d poses, %d differ\n", posed, differ);
        CHECK(differ == 0);
        // and no byte the local PL00R mapped reaches the ghosts' files
        int leaked = 0;
        for (const od::DatSegment& s : od::readContainer(wdat))
            if (s.type == 0 && s.arg != 0) {
                const auto fa = ga.files(0)->view(od::LoadedFileData::kPsxBase + s.arg, s.size);
                const auto fb = gb.files(0)->view(od::LoadedFileData::kPsxBase + s.arg, s.size);
                for (uint32_t k = 0; k < s.size; ++k) leaked += fa[k] != fb[k];
                std::printf("  PL00R0B segment at 0x%08x (+0x%x): %s\n", od::LoadedFileData::kPsxBase + s.arg, s.size,
                            std::equal(fa.begin(), fa.end(), fb.begin()) ? "same in both ghost images" : "differs");
            }
        CHECK(leaked == 0);
    }

    gs.clear(0);
    CHECK(gs.files(0) == nullptr && gs.player(0) == nullptr);
    return testResult("test_ghost");
}
