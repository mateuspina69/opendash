// The reader keeps the file and each record's offsets and decodes into ONE current state while a test walks the
// records forward (materialising every record's full state took gigabytes on the long goldens).
#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>

#include "engine/dat.hpp"
#include "translator/gamemem.hpp"

namespace od::test {

struct Range {
    uint32_t addr, size;
};
// the full state of one record
struct Rec {
    uint32_t tick = 0;
    uint16_t buttons = 0;
    uint8_t point = 0;
    std::vector<uint8_t> blob;
    uint32_t segAddr = 0;
    std::vector<uint8_t> seg;
};
// a record's header and where its delta lives in the file
struct Meta {
    uint32_t tick = 0;
    uint16_t buttons = 0;
    uint8_t point = 0;
    uint8_t mode = 0;
    size_t enc = 0, encEnd = 0, segOff = 0;
    uint32_t segAddr = 0, segSize = 0;
};
struct DeltaGolden {
    std::vector<Range> ranges;
    std::vector<Meta> recs;
    size_t total = 0;
    std::vector<uint8_t> file;
    std::vector<uint32_t> statics; // the addresses of the static blocks written at read time
    mutable Rec cur;
    mutable size_t curIdx = SIZE_MAX;

    // a copy of record i's state (for the tests that hold two records at once)
    Rec copy(size_t i) const { return at(i); }
    // the full state of record i, valid until the next call (forward decoding; going back restarts at record 0)
    const Rec& at(size_t i) const {
        if (curIdx == SIZE_MAX || i < curIdx) {
            cur.blob.assign(total, 0);
            cur.seg.clear();
            cur.segAddr = 0;
            curIdx = SIZE_MAX;
        }
        for (size_t j = curIdx == SIZE_MAX ? 0 : curIdx + 1; j <= i; ++j) {
            const Meta& mt = recs[j];
            size_t p = mt.enc, o = 0;
            while (p < mt.encEnd) {
                const uint32_t z = od::rd16(file.data() + p), l = od::rd16(file.data() + p + 2);
                p += 4;
                o += z;
                for (uint32_t k = 0; k < l; ++k) cur.blob[o + k] ^= file[p + k];
                o += l;
                p += l;
            }
            if (mt.mode == 1) {
                cur.seg.assign(file.begin() + long(mt.segOff), file.begin() + long(mt.segOff + mt.segSize));
                cur.segAddr = mt.segAddr;
            }
        }
        curIdx = i;
        cur.tick = recs[i].tick;
        cur.buttons = recs[i].buttons;
        cur.point = recs[i].point;
        return cur;
    }
};

// statics are written into m (mapped first: always with remap, else only when not mapped yet); segTrailer = the
// records end with the segment fields (TRG1 / STG1; ACT1 has none)
inline bool readDeltaGolden(const std::filesystem::path& path, const char* magic, od::GameMem& m, DeltaGolden& g,
                            bool remap, bool segTrailer = true) {
    g.file = od::readFile(path);
    const auto& f = g.file;
    if (f.size() < 12 || std::memcmp(f.data(), magic, 4) != 0) return false;
    size_t p = 4;
    const uint32_t nr = od::rd32(f.data() + p);
    p += 4;
    for (uint32_t i = 0; i < nr; ++i, p += 8) {
        g.ranges.push_back({od::rd32(f.data() + p), od::rd32(f.data() + p + 4)});
        g.total += g.ranges.back().size;
    }
    const uint32_t ns = od::rd32(f.data() + p);
    p += 4;
    for (uint32_t i = 0; i < ns; ++i) {
        const uint32_t a = od::rd32(f.data() + p), n = od::rd32(f.data() + p + 4);
        g.statics.push_back(a);
        if (remap || !m.mapped(a, n)) m.mapZero(a, n);
        m.write(a, f.data() + p + 8, n);
        p += 8 + n;
    }
    const uint32_t count = od::rd32(f.data() + p);
    p += 4;
    g.recs.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        Meta r;
        r.tick = od::rd32(f.data() + p);
        r.buttons = od::rd16(f.data() + p + 4);
        r.point = f[p + 6];
        const uint32_t enc = od::rd32(f.data() + p + 8);
        p += 12;
        r.enc = p;
        r.encEnd = p + enc;
        p += enc;
        if (!segTrailer) {
            g.recs.push_back(r);
            continue;
        }
        r.segAddr = od::rd32(f.data() + p);
        r.segSize = od::rd32(f.data() + p + 4);
        r.mode = f[p + 8];
        p += 9;
        if (r.mode == 1) {
            r.segOff = p;
            p += r.segSize;
        }
        g.recs.push_back(r);
    }
    return true;
}

} // namespace od::test
