// GhostView with its looks (app/ghost_view, app/ghost_looks) on the ST09 golden's player: a slot with a new look is
// shown in the same update (its look built alone, its slices appended through the sink, no room rebuild); slots share
// a look; clearRemote removes the slot's object id from appendDynamic and drops the look no slot shows (its slices
// are reused by the next look); another fingerprint hides a slot; a new room upload draws nothing until the looks
// are made again for it.
//   test_ghost_view <dat dir> <dash2.exe> <tests/data>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "check.hpp"
#include "data_source_binding.hpp"
#include "plg_pose.hpp"
#include "app/ghost_view.hpp"
#include "content/registry.hpp"
#include "engine/dat.hpp"
#include "engine/vram.hpp"
#include "game/game_state.hpp"
#include "game/player/player_snapshot.hpp"
#include "game/player/player_state.hpp"
#include "translator/gamemem.hpp"
#include "translator/game_memory.hpp"
#include "translator/state_translator.hpp"

namespace fs = std::filesystem;

namespace {
constexpr uint32_t kPlayer = 0xa71d20;
constexpr uint64_t kFingerprint = 0x1234;
constexpr int kRoomSlices = 40; // the room upload's slices (the looks' come after them)

struct Sink {
    int appendCalls = 0, appended = 0, replaced = 0;
    od::app::SliceSink sink() {
        return {[this](const std::vector<uint8_t>& rgba, int count) {
                    ++appendCalls;
                    appended += count;
                    return rgba.size() == size_t(count) * 256 * 256 * 4;
                },
                [this](int slice, const std::vector<uint8_t>& rgba) {
                    replaced += slice >= kRoomSlices && rgba.size() == size_t(256) * 256 * 4;
                }};
    }
};

bool hasGhost(const od::app::GhostView& v, uint32_t generation, int slot) {
    std::vector<od::DynamicObject> dyn;
    v.appendDynamic(dyn, generation);
    return std::any_of(dyn.begin(), dyn.end(), [&](const od::DynamicObject& o) {
        return o.id == od::objectId(od::kKindPlayer, 1u + uint32_t(slot)) && !o.tris.empty();
    });
}
} // namespace

int main(int argc, char** argv) {
    od::content::registerAll();
    if (argc < 4) return 2;
    const fs::path datDir = argv[1], exe = argv[2], data = argv[3];
    od::bindTestDataSource(exe);
    std::map<std::string, od::GameFile> datFiles;
    int reads = 0;
    const od::ghost::ReadFile read = [&](const std::string& name) -> const od::GameFile& {
        auto it = datFiles.find(name);
        if (it == datFiles.end()) {
            ++reads;
            it = datFiles.emplace(name, od::pcGameFile(name, od::readFile(datDir / name))).first;
        }
        return it->second;
    };

    PlgPose g;
    CHECK(g.load(data / "player_ST09.bin"));
    od::GameMem local;
    local.mapExeData(exe);
    const od::ScopedGameMemory bound(local);
    local.mapDatPsx(od::readFile(datDir / "PL00P000.DAT"));
    local.mapZero(kPlayer, g.size);
    for (const PlgRange& r : g.ranges) local.mapZero(r.addr, r.size);
    g.apply(local, g.record(g.count / 2), kPlayer);
    od::test::loadNativeState(local);
    od::port::GameState& game = od::port::gameState();
    game.setMode(od::port::GameMode::Field);
    game.modeStep = uint8_t(od::port::FieldStep::Playing);
    game.stage = 9;
    game.room = 0;
    const od::port::RemotePlayerState mine{od::port::ghostRoomOf(), kFingerprint,
                                           od::port::capturePlayerSnapshot(od::port::player())};
    CHECK(mine.room.fieldPlay);
    CHECK((mine.snapshot.drawFlags[0] & mine.snapshot.drawFlags[1] & 2) != 0);

    od::Vram vram;
    vram.apply(od::pcGameFile("PL00T.DAT", od::readFile(datDir / "PL00T.DAT")));
    const od::app::GhostRoomUpload room1{1, kRoomSlices, &vram, 0};
    Sink s;
    od::app::GhostView v;

    // --- a new look: shown in the same update, its slices appended once, no room rebuild ---
    v.setRemote(0, mine);
    v.update(room1, kFingerprint, read, s.sink());
    std::printf("  slot 0: %s (%d slices appended in %d call(s))\n", v.status(0).c_str(), s.appended, s.appendCalls);
    CHECK(v.status(0) == "shown");
    CHECK(s.appendCalls == 1 && s.appended > 0);
    CHECK(v.looks().size() == 1 && v.looks().appendedSlices() == s.appended);
    CHECK(hasGhost(v, 1, 0));
    const int firstLookSlices = s.appended;

    // --- a second slot with the same look shares it; another look appends its own ---
    v.setRemote(1, mine);
    od::port::RemotePlayerState armed = mine;
    armed.snapshot.look.specialWeapon = 3; // a PL00R arm
    v.setRemote(2, armed);
    v.update(room1, kFingerprint, read, s.sink());
    CHECK(v.status(1) == "shown" && v.status(2) == "shown");
    CHECK(v.looks().size() == 2 && s.appendCalls == 2);
    CHECK(hasGhost(v, 1, 1) && hasGhost(v, 1, 2));
    const int readsAfterLooks = reads;
    v.update(room1, kFingerprint, read, s.sink()); // nothing new: no build, no read
    CHECK(s.appendCalls == 2 && reads == readsAfterLooks);

    // --- clearRemote: the object id leaves appendDynamic; the look no slot shows is dropped ---
    v.clearRemote(2);
    CHECK(!hasGhost(v, 1, 2));
    v.update(room1, kFingerprint, read, s.sink());
    CHECK(v.status(2) == "no remote state");
    CHECK(!hasGhost(v, 1, 2) && hasGhost(v, 1, 0));
    CHECK(v.looks().size() == 1);
    // the next new look takes the freed slices (written over) before appending
    od::port::RemotePlayerState other = mine;
    other.snapshot.look.specialWeapon = 4; // another arm
    v.setRemote(3, other);
    const int appendedBefore = s.appended;
    v.update(room1, kFingerprint, read, s.sink());
    std::printf("  slot 3: %s (%d slices written over, %d appended)\n", v.status(3).c_str(), s.replaced,
                s.appended - appendedBefore);
    CHECK(v.status(3) == "shown");
    CHECK(s.replaced > 0);

    // --- another loaded-data fingerprint: hidden ---
    od::port::RemotePlayerState otherData = mine;
    otherData.fingerprint = kFingerprint + 1;
    v.setRemote(4, otherData);
    v.update(room1, kFingerprint, read, s.sink());
    CHECK(v.status(4).rfind("loaded data differs", 0) == 0);
    CHECK(!hasGhost(v, 1, 4));

    // --- a new room upload: nothing drawn from the old slices; the next update makes the looks for it ---
    CHECK(!hasGhost(v, 2, 0));
    const int callsBefore = s.appendCalls;
    v.update({2, kRoomSlices + 5, &vram, 0}, kFingerprint, read, s.sink());
    CHECK(hasGhost(v, 2, 0) && hasGhost(v, 2, 3));
    CHECK(s.appendCalls > callsBefore);
    CHECK(v.looks().appendedSlices() >= firstLookSlices);

    // --- an unsupported look: not built, said why ---
    od::port::RemotePlayerState alien = mine;
    alien.snapshot.look.kind = 1;
    v.setRemote(5, alien);
    v.update({2, kRoomSlices + 5, &vram, 0}, kFingerprint, read, s.sink());
    std::printf("  slot 5: %s\n", v.status(5).c_str());
    CHECK(v.status(5) != "shown" && !hasGhost(v, 2, 5));
    return testResult("test_ghost_view");
}
