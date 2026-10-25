#include "engine/pe_image.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "engine/dat.hpp"
#include "engine/exe_coverage.hpp"

namespace od {

PeImage::PeImage(std::vector<uint8_t> file) : file_(std::move(file)) {
    auto need = [&](size_t off, size_t n) {
        if (off + n > file_.size()) throw std::runtime_error("dash2.exe: truncated PE");
    };
    need(0, 0x40);
    const uint32_t pe = rd32(file_.data() + 0x3C);
    need(pe, 0xF8);
    if (std::memcmp(file_.data() + pe, "PE\0\0", 4) != 0) throw std::runtime_error("dash2.exe: no PE signature");
    const uint16_t nSections = rd16(file_.data() + pe + 6);
    const uint16_t optSize = rd16(file_.data() + pe + 20);
    const size_t opt = pe + 24;
    if (rd16(file_.data() + opt) != 0x10B) throw std::runtime_error("dash2.exe: expected PE32");
    imageBase_ = rd32(file_.data() + opt + 28);
    const size_t sec = opt + optSize;
    need(sec, size_t(nSections) * 40);
    for (int i = 0; i < nSections; ++i) {
        const uint8_t* h = file_.data() + sec + size_t(i) * 40;
        Section s;
        s.va = imageBase_ + rd32(h + 12);
        s.vsize = rd32(h + 8);
        s.rawOff = rd32(h + 20);
        s.rawSize = rd32(h + 16);
        s.text = std::strncmp(reinterpret_cast<const char*>(h), ".text", 5) == 0;
        s.fileBytes = uint32_t(std::min<size_t>(s.rawSize, file_.size() > s.rawOff ? file_.size() - s.rawOff : 0));
        sections_.push_back(s);
    }
}

PeImage PeImage::read(const std::filesystem::path& exe) { return PeImage(readFile(exe)); }

const PeImage::Section* PeImage::text() const {
    for (const Section& s : sections_)
        if (s.text) return &s;
    return nullptr;
}

const PeImage::Section* PeImage::sectionOf(uint32_t va, uint32_t n) const {
    for (const Section& s : sections_)
        if (va >= s.va && uint64_t(va) + n <= uint64_t(s.va) + s.extent()) return &s;
    return nullptr;
}

bool PeImage::inFile(uint32_t va, uint32_t n) const {
    const Section* s = sectionOf(va, n);
    return s && uint64_t(va - s->va) + n <= s->fileBytes;
}

void PeImage::copy(uint32_t va, uint32_t n, uint8_t* out) const {
    const Section* s = sectionOf(va, n);
    if (!s) throw std::runtime_error("dash2.exe: address not in the image");
    const uint32_t off = va - s->va;
    const uint32_t inFileBytes = off >= s->fileBytes ? 0 : std::min(n, s->fileBytes - off);
    std::copy_n(file_.data() + s->rawOff + off, inFileBytes, out);
    std::fill_n(out + inFileBytes, n - inFileBytes, uint8_t(0));
    if (exe_coverage::enabled()) {
        exe_coverage::mark(va, inFileBytes, exe_coverage::kFileRead);
        exe_coverage::mark(va + inFileBytes, n - inFileBytes, exe_coverage::kBssRead);
    }
}

std::span<const uint8_t> PeImage::fileView(uint32_t va, uint32_t n) const {
    if (!inFile(va, n)) throw std::runtime_error("dash2.exe: address not in file image");
    const Section* s = sectionOf(va, n);
    if (exe_coverage::enabled()) exe_coverage::mark(va, n, exe_coverage::kFileRead);
    return {file_.data() + s->rawOff + (va - s->va), n};
}

std::vector<uint8_t> PeImage::sectionBytes(const Section& s) const {
    std::vector<uint8_t> bytes(s.extent(), 0); // the zero tail stays zero
    std::copy_n(file_.data() + s.rawOff, s.fileBytes, bytes.begin());
    return bytes;
}

} // namespace od
