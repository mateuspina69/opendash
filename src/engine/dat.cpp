#include "dat.hpp"

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

namespace od {

namespace {
constexpr size_t kHeaderRegion = 0x800;
constexpr size_t kEntrySize = 0x10;
} // namespace

std::vector<DatSegment> readContainer(std::span<const uint8_t> data) {
    std::vector<DatSegment> segs;
    uint64_t fileOffset = kHeaderRegion;
    for (size_t off = 0; off + kEntrySize <= kHeaderRegion && off + kEntrySize <= data.size();
         off += kEntrySize) {
        const uint8_t* e = data.data() + off;
        if (std::memcmp(e, "dummy", 5) == 0) break;
        DatSegment s;
        s.type = rd32(e);
        s.size = rd32(e + 4);
        s.arg = rd32(e + 8);
        s.extra = rd32(e + 12);
        s.fileOffset = uint32_t(fileOffset);
        if (fileOffset + s.size > data.size())
            throw std::runtime_error("DAT segment " + std::to_string(segs.size()) +
                                     " exceeds file size");
        segs.push_back(s);
        fileOffset += s.size;
    }
    return segs;
}

std::vector<uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path.string());
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

} // namespace od
