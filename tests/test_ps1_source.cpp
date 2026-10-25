// The PS1 data source (engine/ps1_data_source) vs the PC one, table by table: every table the disc source has loads
// through its typed loader (engine/data_source tableUses) from both, and the two loads read the same -- the table's and
// every pointee's bytes equal but at the pointer words (the PS1 source's routine values / data handles), the same
// routines resolved in the same order, the same pointees followed (argv: dash2.exe, the PC dat/, the disc's cue).
#include <cstdio>
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
        id == od::TableId::StageTextureFiles || id == od::TableId::MenuBankShopPriceStrings)
        return true;
    for (const od::Ps1ExeBank& b : od::ps1ExeBanks())
        if (b.table == id) return true;
    for (const auto& t : od::Ps1DataSource::addressTables())
        if (t.id == id) return true;
    return false;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) return 2;
    od::content::registerAll();
    const od::PcDataSource pc(argv[2], argv[1]);
    const od::Ps1DataSource ps1(std::make_shared<const od::DiscImage>(argv[3]));
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
        CHECK(od::datFileName(0) == "DEMO.DAT" && od::datFileName(56) == "ST00.DAT");
        const od::TableReader geometry = ps1.table({od::TableId::StageGeometryFiles, 0}, 2 * od::kStageCount);
        const od::TableReader texture = ps1.table({od::TableId::StageTextureFiles, 0}, 2 * od::kStageCount);
        CHECK(od::datFileName(geometry.u16(9 * 2)) == "ST09.DAT" && od::datFileName(texture.u16(9 * 2)) == "ST09T.DAT");
        CHECK(ps1.hasTable({od::TableId::MenuBankItemNames, 0}));
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
    { // the tables the game reads by address: their bytes the PC's but at the pointer words, which are null on both or
      // name the same routine (a function pointer) or are a PS1 data pointer where the PC has one
        size_t n = 0, same = 0;
        for (const auto& t : od::Ps1DataSource::addressTables()) {
            ++n;
            const od::TableKey key{t.id, 0};
            const uint32_t at = pc.stateAddress(key);
            const od::TableReader a = pc.stateData(at, t.bytes), b = ps1.stateData(ps1.stateAddress(key), t.bytes);
            bool ok = a.size() == t.bytes && b.size() == t.bytes;
            const uint32_t align = (4 - (at & 3)) & 3; // the first 4-aligned word's offset
            for (uint32_t o = 0; ok && o < t.bytes; ++o) {
                if (a.u8(o) == b.u8(o)) continue;
                const uint32_t w = o - ((o - align) & 3);
                ok = o >= align && w + 4 <= t.bytes && ps1Pointer(b.u32(w));
                if (!ok) std::fprintf(stderr, "  byte %u: %02x / %02x\n", o, a.u8(o), b.u8(o));
            }
            for (uint32_t w = align; ok && w + 4 <= t.bytes; w += 4) {
                const uint32_t pw = a.u32(w), sw = b.u32(w);
                if (pw == sw || !ps1Pointer(sw)) continue;
                if ((sw & 0xffff0000u) == od::Ps1DataSource::kRoutineTag) ok = pc.stateRoutine(pw).id() == ps1.stateRoutine(sw).id();
                else ok = pw != 0;
                if (!ok) std::fprintf(stderr, "  word %u: %08x / %08x\n", w, pw, sw);
            }
            if (ok) ++same;
            else std::fprintf(stderr, "%s: not the PC's\n", od::describe(key).c_str());
        }
        std::printf("%zu tables read by address on the disc: %zu read as the PC's\n", n, same);
        CHECK(n > 0 && same == n);
    }
    std::printf("%zu tables on the disc: %zu read as the PC's\n", tables, equal);
    CHECK(tables > 0 && equal == tables);
    return testResult("test_ps1_source");
}
