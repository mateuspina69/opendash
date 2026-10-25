#include "translator/gamemem.hpp"

#include <algorithm>

#include "engine/dat.hpp"
#include "engine/exe_coverage.hpp"
#include "engine/pe_image.hpp"

namespace od {

void GameMem::map(uint32_t base, std::span<const uint8_t> bytes) {
    for (Region& r : regions_)
        if (base >= r.base && uint64_t(base) + bytes.size() <= uint64_t(r.base) + r.bytes.size()) {
            std::copy(bytes.begin(), bytes.end(), r.bytes.mutableData() + (base - r.base));
            if (!r.written.empty() && !bytes.empty()) noteWrite(r, base, uint32_t(bytes.size()));
            files_.write(base, bytes.data(), uint32_t(bytes.size()));
            return;
        }
    // new regions go first so a fresh, more specific block shadows an older overlapping one; the loaded-file store
    // shares the block (a write through either is the other's)
    FileDataBlock block(bytes.begin(), bytes.end());
    files_.mapShared(base, block.share());
    regions_.insert(regions_.begin(), Region{base, std::move(block)});
}

void GameMem::mapZero(uint32_t base, uint32_t size) {
    FileDataBlock block(std::vector<uint8_t>(size, 0));
    files_.mapShared(base, block.share());
    regions_.insert(regions_.begin(), Region{base, std::move(block)});
}

void GameMem::mapExeData(const std::filesystem::path& exe) {
    const PeImage image = PeImage::read(exe);
    for (const PeImage::Section& s : image.sections()) {
        if (s.text) continue;
        // the section's bytes are the image's and the loaded files' (the game's writes into the program's sections
        // reach the image), under everything mapped later; the .bss part stays zero
        Region r{s.va, FileDataBlock(image.sectionBytes(s))};
        files_.mapShared(s.va, r.bytes.share(), false);
        if (exe_coverage::enabled()) {
            r.fileBytes = s.fileBytes;
            r.written.assign(r.bytes.size(), 0);
        }
        regions_.push_back(std::move(r)); // lowest priority
    }
}

void GameMem::setFences(std::vector<std::pair<uint32_t, uint32_t>> ranges) {
    std::sort(ranges.begin(), ranges.end());
    fences_.clear();
    for (const auto& r : ranges) {
        if (r.first >= r.second) continue;
        if (!fences_.empty() && r.first <= fences_.back().second) fences_.back().second = std::max(fences_.back().second, r.second);
        else fences_.push_back(r);
    }
}

void GameMem::checkFenced(uint32_t a, uint32_t n) const {
    auto it = std::upper_bound(fences_.begin(), fences_.end(), std::pair<uint32_t, uint32_t>{a, 0xffffffffu});
    // the fence starting at or before a, and the next one (it may start inside [a, a + n))
    const uint64_t end = uint64_t(a) + n;
    bool hit = it != fences_.end() && it->first < end;
    if (!hit && it != fences_.begin()) hit = std::prev(it)->second > a;
    if (hit) {
        char msg[96];
        std::snprintf(msg, sizeof msg, "GameMem: 0x%08x (+%u) is native state (the game reached it through the image)",
                      a, n);
        throw NativeStateAccess(msg);
    }
}

void GameMem::noteRead(const Region& r, uint32_t a, uint32_t n) {
    for (uint32_t i = a - r.base, e = i + n; i < e; ++i)
        if (!r.written[i])
            exe_coverage::mark(r.base + i, 1, i < r.fileBytes ? exe_coverage::kFileRead : exe_coverage::kBssRead);
}

void GameMem::mapDatPsx(std::span<const uint8_t> datFile) {
    for (const DatSegment& s : readContainer(datFile))
        if (s.type == 0 && s.arg != 0) map(kPsxBase + s.arg, datFile.subspan(s.fileOffset, s.size));
}

} // namespace od
