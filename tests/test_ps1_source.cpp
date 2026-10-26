// The PS1 data source (engine/ps1_data_source) vs the PC one, table by table: every table the disc source has loads
// through its typed loader (engine/data_source tableUses) from both, and the two loads read the same -- the table's and
// every pointee's bytes equal but at the pointer words (the PS1 source's routine values / data handles), the same
// routines resolved in the same order, the same pointees followed (argv: dash2.exe, the PC dat/, the disc's cue).
#include <cstdio>
#include <algorithm>
#include <functional>
#include <memory>
#include <set>

#include "check.hpp"
#include "content/registry.hpp"
#include "engine/dat.hpp"
#include "engine/data_source.hpp"
#include "engine/disc_image.hpp"
#include "engine/pc_data_source.hpp"
#include "engine/ps1_data_source.hpp"
#include "engine/ps1_exe_banks.hpp"
#include "game/dat_names.hpp"

namespace {

struct Event {
    char kind; // T table bytes, P pointee bytes, R routine
    std::vector<uint8_t> bytes;
    uint32_t routine = 0;
};

class Recorder final : public od::DataSource {
public:
    explicit Recorder(const od::DataSource& in) : in_(in) {}
    mutable std::vector<Event> events;
    bool hasFile(const std::string& n) const override { return in_.hasFile(n); }
    od::GameFile gameFile(const std::string& n) const override { return in_.gameFile(n); }
    bool hasTable(od::TableKey k) const override { return in_.hasTable(k); }
    od::TableReader table(od::TableKey k, uint32_t n) const override { return keep('T', in_.table(k, n)); }
    uint32_t stateAddress(od::TableKey k) const override { return in_.stateAddress(k); }
    uint32_t routineStateValue(od::RoutineId id) const override { return in_.routineStateValue(id); }

protected:
    od::TableReader pointee(uint32_t p, uint32_t n) const override { return keep('P', in_.stateData(p, n)); }
    std::string pointeeString(uint32_t p, size_t max) const override {
        const od::TableReader r = keep('P', in_.stateData(p, uint32_t(max)));
        std::string s;
        for (size_t i = 0; i < r.size() && r.u8(i) != 0; ++i) s += char(r.u8(i));
        return s;
    }
    od::RoutineRef routineOf(uint32_t p) const override {
        const od::RoutineRef r = in_.stateRoutine(p);
        events.push_back({'R', {}, uint32_t(static_cast<uint16_t>(r.id()))});
        return r;
    }

private:
    od::TableReader keep(char kind, const od::TableReader& r) const {
        std::vector<uint8_t> b;
        if (!r.empty()) {
            const auto s = r.bytes(0, r.size());
            b.assign(s.begin(), s.end());
        }
        events.push_back({kind, b});
        return od::TableReader(*this, std::move(b));
    }
    const od::DataSource& in_;
};

// the PS1 source's pointer words: its routine values and its data addresses
bool ps1Pointer(uint32_t w) {
    return (w & 0xffff0000u) == od::Ps1DataSource::kRoutineTag ||
           (w >= od::Ps1DataSource::kVirtualBase && w < od::Ps1DataSource::kRoutineTag);
}

bool same(const std::vector<Event>& pc, const std::vector<Event>& ps, std::string& why) {
    if (pc.size() != ps.size()) return why = "different reads (" + std::to_string(pc.size()) + " / " + std::to_string(ps.size()) + ")", false;
    for (size_t i = 0; i < pc.size(); ++i) {
        const Event &a = pc[i], &b = ps[i];
        if (a.kind != b.kind) return why = "read " + std::to_string(i) + ": another kind", false;
        if (a.kind == 'R') {
            if (a.routine != b.routine) return why = "routine " + std::to_string(i) + " differs", false;
            continue;
        }
        if (a.bytes.size() != b.bytes.size()) return why = "read " + std::to_string(i) + ": another size", false;
        for (size_t o = 0; o < a.bytes.size(); ++o) {
            if (a.bytes[o] == b.bytes[o]) continue;
            const size_t w = o & ~size_t(3);
            if (w + 4 <= b.bytes.size() && ps1Pointer(od::rd32(b.bytes.data() + w))) continue;
            return why = "read " + std::to_string(i) + " byte " + std::to_string(o) + " differs", false;
        }
    }
    return true;
}

// the tables the PS1 source makes from the disc in the PS1's own form (another content than the PC's), and the ones
// checked apart: the room parameters (the PS1's records), the tables the game reads by address (their extent the PC's;
// a load past it reads the PS1's own neighbours, as the PS1 does)
bool madeTable(od::TableId id) {
    if (id == od::TableId::DatFileNames || id == od::TableId::StageGeometryFiles || id == od::TableId::RoomParams ||
        id == od::TableId::StageTextureFiles || id == od::TableId::MenuBankShopPriceStrings ||
        id == od::TableId::VoiceFileNames || id == od::TableId::AudioBuiltInVoices)
        return true;
    for (const od::Ps1ExeBank& b : od::ps1ExeBanks())
        if (b.table == id) return true;
    for (const auto& t : od::Ps1DataSource::addressTables())
        if (t.key.id == id) return true;
    return false;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) return 2;
    od::content::registerAll();
    const od::PcDataSource pc(argv[2], argv[1]);
    const od::Ps1DataSource ps1(std::make_shared<const od::DiscImage>(argv[3]));
    for (const od::TableId id : {od::TableId::TextLogicHandlers, od::TableId::TextLayoutHandlers,
                               od::TableId::OpeningScriptSteps, od::TableId::OpeningCastCrewVariants,
                               od::TableId::ArrivalDescentSteps, od::TableId::ArrivalDescentCameraScript,
                               od::TableId::StatusScreenHighlightClutRect, od::TableId::StatusScreenClassTables,
                               od::TableId::OpeningCastCrewTracks, od::TableId::ArrivalCabinSteps,
                               od::TableId::ArrivalCabinMouth, od::TableId::TownStoryCastJunkShopVariants,
                               od::TableId::AttractSt02RoomHandlers, od::TableId::StatusControlsCursorTree})
        CHECK(ps1.hasTable({id, 0}));
    { // Camera bytecode continues beyond the PC-identical prefix. Its spawn-record operands must still translate.
        uint32_t at = ps1.stateAddress({od::TableId::OpeningScriptCameraScript, 0});
        size_t spawns = 0;
        for (size_t instructions = 0; instructions < 512; ++instructions) {
            const auto op = ps1.stateData(at, 20);
            const uint32_t code = op.u32(0) >> 24;
            if (code == 0xff) break;
            if (code == 0x40 || code == 0x41) {
                const uint32_t record = op.u32(4);
                CHECK(record >= od::Ps1DataSource::kVirtualBase && record < od::Ps1DataSource::kRoutineTag);
                CHECK(ps1.stateData(record, 20).size() == 20);
                ++spawns;
            }
            const uint32_t size = code >= 0x10 && code <= 0x1a ? 16 :
                                  code == 0x1b || code == 0x40 || code == 0x41 ? 8 : code == 0x42 ? 20 : 4;
            at += size;
        }
        CHECK(spawns > 0);
    }
    for (uint32_t stage : {1u, 2u})
        CHECK(ps1.hasTable({od::TableId::RoomSpawnList, od::roomIndex(int(stage), 0)}));
    { // A wrongly shaped pointer array previously dispatched junk-shop actors into texture bytes.
        const auto classes = ps1.table({od::TableId::StageAnimatedClasses, 0x0a}, 4);
        const auto handlers = ps1.stateData(classes.u32(0), 8);
        CHECK(handlers.routine(0).is(od::RoutineId::TownNpcUpdate));
        CHECK(handlers.routine(4).is(od::RoutineId::JunkShopCastUpdate));
    }
    { // Story 0, room 0 of the cabin runs its arrival handler after ST08's landing.
        const auto stories = ps1.table({od::TableId::RoomScriptHandlers, 4}, 4);
        const auto rooms = ps1.stateData(stories.u32(0), 12);
        CHECK(rooms.routine(0).is(od::RoutineId::St04Story0Sub0RoomHandlerA));
        CHECK(rooms.routine(4).is(od::RoutineId::EmptyRoutine));
    }
    { // ST02's later actor types lie past the old eight-byte verified prefix of the class table.
        const auto a = pc.table({od::TableId::StageAnimatedClasses, 2}, 244);
        const auto b = ps1.table({od::TableId::StageAnimatedClasses, 2}, 244);
        size_t found = 0;
        for (uint32_t o = 0; o < 244; o += 4) {
            if (!a.u32(o)) continue;
            CHECK(b.u32(o) >= od::Ps1DataSource::kVirtualBase && b.u32(o) < od::Ps1DataSource::kRoutineTag);
            if (b.u32(o) < od::Ps1DataSource::kVirtualBase || b.u32(o) >= od::Ps1DataSource::kRoutineTag) continue;
            const auto pa = pc.stateData(a.u32(o), 4), pb = ps1.stateData(b.u32(o), 4);
            CHECK(pa.routine(0).id() == pb.routine(0).id());
            ++found;
        }
        CHECK(found >= 3);
    }
    { // The shared menu overlay follows four terminated image containers; SLUS's tree links into it.
        const auto root = ps1.table({od::TableId::StatusControlsCursorTree, 0}, 40);
        CHECK(root.u32(4) == ps1.stateAddress({od::TableId::MenuBankPanelHelp, 0}));
        for (uint32_t o : {12u, 20u, 28u, 36u}) {
            const uint32_t pointer = root.u32(o);
            CHECK(pointer >= od::Ps1DataSource::kVirtualBase && pointer < od::Ps1DataSource::kRoutineTag);
            CHECK(ps1.stateData(pointer, 8).size() == 8);
        }
        const auto icons = ps1.table({od::TableId::StatusControlsItemIconArts, 0}, 64);
        const auto expected = pc.table({od::TableId::StatusControlsItemIconArts, 0}, 64);
        CHECK(std::equal(icons.bytes(0,64).begin(), icons.bytes(0,64).end(), expected.bytes(0,64).begin()));
    }
    // Required by ST09's story-0 Roll spawner. The parity loop below deliberately skips absent tables, so it alone
    // cannot detect a missing mapping that stops an otherwise playable room.
    CHECK(ps1.hasTable({od::TableId::RollSt09FollowerPoint, 0}));
    for (const od::TableId id : {od::TableId::FadeDirections, od::TableId::FadeSpeeds,
                                od::TableId::TownStoryJunkShopEyes, od::TableId::TownStoryJunkShopMouth})
        CHECK(ps1.hasTable({id, 0}));
    if (ps1.hasTable({od::TableId::FadeDirections, 0})) {
        const auto directions = ps1.table({od::TableId::FadeDirections, 0}, 3);
        CHECK(directions.s8(0) == 1 && directions.s8(2) == -1);
    }
    { // Fade speeds must come from the real request table; a readable zero-filled area stalls every fade.
        const auto a = pc.table({od::TableId::FadeSpeeds, 0}, 12);
        const auto b = ps1.table({od::TableId::FadeSpeeds, 0}, 12);
        for (uint32_t o = 0; o < 12; ++o) CHECK(a.u8(o) == b.u8(o));
    }
    { // The 41-entry bank on ST05 uses the disc's text encoding and offsets.
        const auto bank = ps1.table({od::TableId::St05EntryListEntryNames, 0}, 713);
        CHECK(bank.u16(0) == 41 * 2 && bank.u8(712) == 0xff);
        for (uint32_t i = 1; i < 41; ++i) CHECK(bank.u16(i * 2) >= bank.u16((i - 1) * 2));
    }
    { // Four real paths precede the critter's handler tables; zero-filled RAM is not a path table.
        const auto a = pc.table({od::TableId::LeapingCritterPaths, 0}, 16);
        const auto b = ps1.table({od::TableId::LeapingCritterPaths, 0}, 16);
        for (uint32_t i = 0; i < 4; ++i) {
            CHECK(b.u32(i * 4) != 0);
            if (b.u32(i * 4) == 0) continue;
            const uint32_t bytes = i < 2 ? 16 : 12;
            const auto pa = pc.stateData(a.u32(i * 4), bytes), pb = ps1.stateData(b.u32(i * 4), bytes);
            for (uint32_t o = 0; o < bytes; ++o) CHECK(pa.u8(o) == pb.u8(o));
        }
    }
    size_t tables = 0, equal = 0;
    std::set<od::TableKey> done;
    for (const od::TableUse& u : od::tableUses()) {
        auto check = [&](od::TableKey key) {
            if (!ps1.hasTable(key) || !done.insert(key).second || madeTable(key.id)) return;
            ++tables;
            Recorder a(pc), b(ps1);
            std::string why;
            try {
                od::bindDataSource(&a);
                u.load(key);
                od::bindDataSource(&b);
                u.load(key);
            } catch (const std::exception& e) {
                why = e.what();
            }
            od::bindDataSource(nullptr);
            if (why.empty() && same(a.events, b.events, why)) ++equal;
            else std::fprintf(stderr, "%s: %s\n", od::describe(key).c_str(), why.c_str());
        };
        if (!u.family) check({u.id, u.index});
        else
            for (int stage = 0; stage < od::kStageCount; ++stage) {
                check({u.id, uint32_t(stage)});
                for (int room = 0; room < 256; ++room) check({u.id, od::roomIndex(stage, room)});
            }
    }
    { // the tables made from the disc: the load indices' names (SLUS_011.40's file table, the PS1's numbering) and the
      // stages' files by name
        const od::ScopedDataSource bound(ps1);
        CHECK(od::datFileName(2) == "DEMO.DAT" && od::datFileName(58) == "ST00.DAT");
        const od::TableReader geometry = ps1.table({od::TableId::StageGeometryFiles, 0}, 2 * od::kStageCount);
        const od::TableReader texture = ps1.table({od::TableId::StageTextureFiles, 0}, 2 * od::kStageCount);
        CHECK(od::datFileName(geometry.u16(9 * 2)) == "ST09.DAT" && od::datFileName(texture.u16(9 * 2)) == "ST09T.DAT");
        CHECK(ps1.hasTable({od::TableId::MenuBankItemNames, 0}));
        for (const char* name : {"SUBSCN01.DAT", "SUBSCN02.DAT", "SUBSCN03.DAT", "SUBSCN06.DAT"}) {
            CHECK(ps1.hasFile(name));
            if (!ps1.hasFile(name)) continue;
            CHECK(od::datFileName(od::datFileIndex(name)) == name);
            CHECK(ps1.gameFile(name).has(od::FilePart::Kind::Vram));
        }
    }
    { // the room parameters: the PS1's 0x24-byte records, the PC's from +0x3 on, no fog range past them
        size_t rooms = 0, same = 0;
        for (int stage = 0; stage < od::kStageCount; ++stage)
            for (int room = 0; room < 256; ++room) {
                const od::TableKey key{od::TableId::RoomParams, od::roomIndex(stage, room)};
                if (!ps1.hasTable(key) || !pc.hasTable(key)) continue;
                ++rooms;
                const od::TableReader a = pc.table(key, 0x2c), b = ps1.table(key, 0x2c);
                bool equal = b.f32(0x24) == 0.0f && b.f32(0x28) == 0.0f;
                for (uint32_t o = 3; o < 0x24; ++o) equal = equal && a.u8(o) == b.u8(o);
                same += equal;
            }
        std::printf("%zu room parameter records on the disc: %zu the PC's\n", rooms, same);
        CHECK(rooms > 0 && same == rooms && ps1.hasTable({od::TableId::RoomParams, od::roomIndex(9, 0)}));
    }
    { // the tables the game reads by address, and the ones their data pointers lead to: their bytes the PC's but at the
      // pointer words, which are null on both or name the same routine (a function pointer) or point at the same bytes
        size_t n = 0, same = 0, followed = 0;
        std::set<uint32_t> seen;
        // a pointee (followed): the bytes up to its first difference, its first word at least (its PS1 table can run
        // past the PC object it is: the PC keeps other data there)
        std::function<bool(uint32_t, uint32_t, uint32_t, const std::string&, bool)> sameAt =
            [&](uint32_t at, uint32_t ps1At, uint32_t bytes, const std::string& what, bool pointee) {
                const od::TableReader a = pc.stateData(at, bytes), b = ps1.stateData(ps1At, bytes);
                bool ok = a.size() == bytes && b.size() == bytes;
                const uint32_t align = (4 - (at & 3)) & 3; // the first 4-aligned word's offset
                for (uint32_t o = 0; ok && o < bytes; ++o) {
                    if (a.u8(o) == b.u8(o)) continue;
                    const uint32_t w = o - ((o - align) & 3);
                    ok = o >= align && w + 4 <= bytes && ps1Pointer(b.u32(w));
                    if (!ok && pointee && o >= std::min<uint32_t>(bytes, 4)) {
                        bytes = o < align ? o : w;
                        ok = true;
                        break;
                    }
                    if (!ok) std::fprintf(stderr, "%s: byte %u: %02x / %02x\n", what.c_str(), o, a.u8(o), b.u8(o));
                }
                for (uint32_t w = align; ok && w + 4 <= bytes; w += 4) {
                    const uint32_t pw = a.u32(w), sw = b.u32(w);
                    if (pw == sw || !ps1Pointer(sw)) continue;
                    if ((sw & 0xffff0000u) == od::Ps1DataSource::kRoutineTag) {
                        ok = pc.stateRoutine(pw).id() == ps1.stateRoutine(sw).id();
                    } else if (pw == 0) {
                        ok = false;
                    } else if (const uint32_t m = ps1.tableBytesFrom(sw); m && seen.insert(sw).second) {
                        ++followed;
                        ok = sameAt(pw, sw, m, what + "->" + std::to_string(w), true);
                    }
                    if (!ok) std::fprintf(stderr, "%s: word %u: %08x / %08x\n", what.c_str(), w, pw, sw);
                }
                return ok;
            };
        size_t own = 0, ownTables = 0;
        for (const auto& t : od::Ps1DataSource::addressTables()) {
            const od::TableKey key = t.key;
            if (t.own) { // the PS1's own: it reads (no PC bytes to match)
                ++ownTables;
                const size_t got = ps1.stateData(ps1.stateAddress(key), t.bytes).size();
                own += got == t.bytes;
                if (got != t.bytes) std::fprintf(stderr, "%s: own table read %zu / %u bytes\n",
                                               od::describe(key).c_str(), got, t.bytes);
                continue;
            }
            ++n;
            if (sameAt(pc.stateAddress(key), ps1.stateAddress(key), t.bytes, od::describe(key), false)) ++same;
        }
        std::printf("%zu tables read by address on the disc (%zu followed): %zu read as the PC's\n", n, followed, same);
        std::printf("%zu tables read by address the PS1's own, read whole\n", own);
        CHECK(n > 0 && same == n);
        CHECK(own == ownTables);
    }
    std::printf("%zu tables on the disc: %zu read as the PC's\n", tables, equal);
    CHECK(tables > 0 && equal == tables);
    return testResult("test_ps1_source");
}
