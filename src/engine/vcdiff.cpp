#include "engine/vcdiff.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace od {

namespace {

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error("vcdiff: " + what); }

// the window indicator's bits (VCD_ADLER32: xdelta3's checksum of the window's target)
constexpr uint8_t kVcdSource = 0x01, kVcdTarget = 0x02, kVcdAdler32 = 0x04;
// the header indicator's bits
constexpr uint8_t kVcdDecompress = 0x01, kVcdCodeTable = 0x02, kVcdAppHeader = 0x04;

enum Op : uint8_t { kNoop, kAdd, kRun, kCopy };
struct Half {
    Op op = kNoop;
    uint8_t size = 0, mode = 0; // size 0: a varint in the instructions follows
};
using CodeTable = std::array<std::array<Half, 2>, 256>;

// RFC 3284 5.6, the default code table
CodeTable defaultCodeTable() {
    CodeTable t{};
    size_t i = 0;
    t[i++] = {{{kRun, 0, 0}, {}}};
    for (uint8_t s = 0; s <= 17; ++s) t[i++] = {{{kAdd, s, 0}, {}}};
    for (uint8_t m = 0; m <= 8; ++m) {
        t[i++] = {{{kCopy, 0, m}, {}}};
        for (uint8_t s = 4; s <= 18; ++s) t[i++] = {{{kCopy, s, m}, {}}};
    }
    for (uint8_t m = 0; m <= 5; ++m)
        for (uint8_t a = 1; a <= 4; ++a)
            for (uint8_t s = 4; s <= 6; ++s) t[i++] = {{{kAdd, a, 0}, {kCopy, s, m}}};
    for (uint8_t m = 6; m <= 8; ++m)
        for (uint8_t a = 1; a <= 4; ++a) t[i++] = {{{kAdd, a, 0}, {kCopy, 4, m}}};
    for (uint8_t m = 0; m <= 8; ++m) t[i++] = {{{kCopy, 4, m}, {kAdd, 1, 0}}};
    if (i != 256) fail("the default code table is not 256 entries");
    return t;
}
const CodeTable kCodeTable = defaultCodeTable();

constexpr size_t kNear = 4, kSame = 3; // the default address cache sizes

uint32_t adler32(std::span<const uint8_t> d) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < d.size();) {
        const size_t end = std::min(d.size(), i + 5552); // the most bytes before the sums can overflow
        for (; i < end; ++i) {
            a += d[i];
            b += a;
        }
        a %= 65521;
        b %= 65521;
    }
    return b << 16 | a;
}

// a section of a window (data, instructions, addresses) read front to back
struct Reader {
    std::span<const uint8_t> d;
    size_t p = 0;
    const char* what;
    bool done() const { return p >= d.size(); }
    uint8_t byte() {
        if (p >= d.size()) fail(std::string("the window's ") + what + " end early");
        return d[p++];
    }
    uint64_t varint() {
        uint64_t v = 0;
        for (int n = 0; n < 10; ++n) {
            const uint8_t b = byte();
            v = v << 7 | (b & 0x7f);
            if (!(b & 0x80)) return v;
        }
        fail(std::string("an integer past 64 bits in the window's ") + what);
    }
    std::span<const uint8_t> take(uint64_t n) {
        if (n > d.size() - p) fail(std::string("the window's ") + what + " end early");
        const auto s = d.subspan(p, size_t(n));
        p += size_t(n);
        return s;
    }
};

uint64_t varint(std::ifstream& f, const char* what) {
    uint64_t v = 0;
    for (int n = 0; n < 10; ++n) {
        const int c = f.get();
        if (c < 0) fail(std::string("the patch ends in ") + what);
        v = v << 7 | (uint8_t(c) & 0x7f);
        if (!(c & 0x80)) return v;
    }
    fail(std::string("an integer past 64 bits in ") + what);
}

uint8_t byte(std::ifstream& f, const char* what) {
    const int c = f.get();
    if (c < 0) fail(std::string("the patch ends in ") + what);
    return uint8_t(c);
}

} // namespace

VcdiffTarget::VcdiffTarget(const std::filesystem::path& source, const std::filesystem::path& patch)
    : source_(source, std::ios::binary), patch_(patch, std::ios::binary), patchPath_(patch) {
    if (!source_) fail("cannot open the source " + source.string());
    if (!patch_) fail("cannot open the patch " + patch.string());
    const uint64_t patchBytes = std::filesystem::file_size(patch);
    const uint64_t sourceBytes = std::filesystem::file_size(source);
    uint8_t magic[4] = {};
    patch_.read(reinterpret_cast<char*>(magic), 4);
    if (!patch_ || magic[0] != 0xd6 || magic[1] != 0xc3 || magic[2] != 0xc4)
        fail(patch.string() + " is not a VCDIFF file");
    const uint8_t header = byte(patch_, "the header");
    if (header & kVcdDecompress) fail(patch.string() + " uses secondary compression");
    if (header & kVcdCodeTable) fail(patch.string() + " has its own code table");
    if (header & kVcdAppHeader) patch_.seekg(std::streamoff(varint(patch_, "the header")), std::ios::cur);

    // the windows' headers: their sections are skipped, read only when the window is built
    while (patch_.peek() != std::ifstream::traits_type::eof()) {
        Window w;
        w.indicator = byte(patch_, "a window");
        if ((w.indicator & (kVcdSource | kVcdTarget)) == (kVcdSource | kVcdTarget))
            fail("a window copies from both the source and the target");
        if (w.indicator & (kVcdSource | kVcdTarget)) {
            w.segmentLength = varint(patch_, "a window");
            w.segmentPosition = varint(patch_, "a window");
            const uint64_t limit = (w.indicator & kVcdSource) ? sourceBytes : size_;
            if (w.segmentPosition > limit || w.segmentLength > limit - w.segmentPosition)
                fail("a window's segment lies past the " + std::string((w.indicator & kVcdSource) ? "source" : "target"));
        }
        varint(patch_, "a window"); // the delta encoding's length
        w.target = size_;
        w.length = varint(patch_, "a window");
        if (byte(patch_, "a window") != 0) fail("a window's sections are compressed");
        w.dataLength = varint(patch_, "a window");
        w.instructionLength = varint(patch_, "a window");
        w.addressLength = varint(patch_, "a window");
        if (w.indicator & kVcdAdler32)
            for (int k = 0; k < 4; ++k) w.adler = w.adler << 8 | byte(patch_, "a window");
        w.sections = uint64_t(patch_.tellg());
        const uint64_t sections = w.dataLength + w.instructionLength + w.addressLength;
        if (sections > patchBytes - w.sections) fail("a window's sections lie past the end of the patch");
        patch_.seekg(std::streamoff(sections), std::ios::cur);
        size_ += w.length;
        windows_.push_back(w);
    }
}

void VcdiffTarget::read(uint64_t offset, std::span<uint8_t> out) const {
    std::lock_guard lock(lock_);
    readLocked(offset, out);
}

void VcdiffTarget::readLocked(uint64_t offset, std::span<uint8_t> out) const {
    if (offset > size_ || out.size() > size_ - offset)
        fail("a read past the end of the target of " + patchPath_.string());
    auto it = std::upper_bound(windows_.begin(), windows_.end(), offset,
                               [](uint64_t o, const Window& w) { return o < w.target; });
    size_t done = 0;
    for (size_t k = size_t(it - windows_.begin()) - 1; done < out.size(); ++k) {
        const Window& w = windows_[k];
        const std::vector<uint8_t>& t = built(k);
        const uint64_t from = offset + done - w.target;
        const size_t n = size_t(std::min<uint64_t>(out.size() - done, w.length - from));
        std::copy_n(t.begin() + std::ptrdiff_t(from), n, out.begin() + std::ptrdiff_t(done));
        done += n;
    }
}

const std::vector<uint8_t>& VcdiffTarget::built(size_t window) const {
    constexpr size_t kKept = 2;
    for (auto it = cache_.begin(); it != cache_.end(); ++it)
        if (it->first == window) {
            cache_.splice(cache_.begin(), cache_, it);
            return cache_.front().second;
        }
    std::vector<uint8_t> t = build(windows_[window]); // a VCD_TARGET window reads earlier windows through the cache
    cache_.emplace_front(window, std::move(t));
    while (cache_.size() > kKept) cache_.pop_back();
    return cache_.front().second;
}

std::vector<uint8_t> VcdiffTarget::build(const Window& w) const {
    std::vector<uint8_t> sections(size_t(w.dataLength + w.instructionLength + w.addressLength));
    patch_.clear();
    patch_.seekg(std::streamoff(w.sections));
    if (!patch_.read(reinterpret_cast<char*>(sections.data()), std::streamsize(sections.size())))
        fail("cannot read a window of " + patchPath_.string());
    const std::span<const uint8_t> all(sections);
    Reader data{all.subspan(0, size_t(w.dataLength)), 0, "data"};
    Reader inst{all.subspan(size_t(w.dataLength), size_t(w.instructionLength)), 0, "instructions"};
    Reader addr{all.subspan(size_t(w.dataLength + w.instructionLength)), 0, "addresses"};

    std::vector<uint8_t> segment(size_t(w.segmentLength));
    if (w.indicator & kVcdSource) {
        source_.clear();
        source_.seekg(std::streamoff(w.segmentPosition));
        if (!source_.read(reinterpret_cast<char*>(segment.data()), std::streamsize(segment.size())))
            fail("cannot read the source segment of a window");
    } else if (w.indicator & kVcdTarget) {
        readLocked(w.segmentPosition, segment);
    }

    std::vector<uint8_t> t;
    t.reserve(size_t(w.length));
    std::array<uint64_t, kNear> near{};
    std::array<uint64_t, kSame * 256> same{};
    size_t nextNear = 0;
    while (!inst.done()) {
        for (const Half& h : kCodeTable[inst.byte()]) {
            if (h.op == kNoop) continue;
            const uint64_t size = h.size ? h.size : inst.varint();
            if (size > w.length - t.size()) fail("a window builds more than its length");
            if (h.op == kAdd) {
                const auto s = data.take(size);
                t.insert(t.end(), s.begin(), s.end());
            } else if (h.op == kRun) {
                t.insert(t.end(), size_t(size), data.byte());
            } else {
                const uint64_t here = w.segmentLength + t.size();
                uint64_t a = 0;
                if (h.mode == 0) a = addr.varint();
                else if (h.mode == 1) a = here - addr.varint();
                else if (h.mode < 2 + kNear) a = near[h.mode - 2] + addr.varint();
                else a = same[(h.mode - 2 - kNear) * 256 + addr.byte()];
                if (a >= here) fail("a copy from past the bytes built so far");
                near[nextNear] = a;
                nextNear = (nextNear + 1) % kNear;
                same[a % same.size()] = a;
                // the segment, then the window's own target, which the copy may overlap as it grows
                for (uint64_t k = 0; k < size; ++k, ++a)
                    t.push_back(a < w.segmentLength ? segment[size_t(a)] : t[size_t(a - w.segmentLength)]);
            }
        }
    }
    if (t.size() != w.length) fail("a window builds " + std::to_string(t.size()) + " bytes, not its " +
                                   std::to_string(w.length));
    if ((w.indicator & kVcdAdler32) && adler32(t) != w.adler) fail("a window's checksum does not match");
    return t;
}

} // namespace od
