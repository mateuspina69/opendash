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

// the PS1 source's pointer words: its routine values and data handles
bool ps1Pointer(uint32_t w) {
    return (w & 0xffff0000u) == od::Ps1DataSource::kRoutineTag || (w & 0xe0000000u) == od::Ps1DataSource::kDataTag;
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
            if (!ps1.hasTable(key) || !done.insert(key).second) return;
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
    std::printf("%zu tables on the disc: %zu read as the PC's\n", tables, equal);
    CHECK(tables > 0 && equal == tables);
    return testResult("test_ps1_source");
}
