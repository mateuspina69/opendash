// The game files' hash (see game_files_hash.hpp).
#include "engine/game_files_hash.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/disc_image.hpp"

namespace od {

namespace {

constexpr uint64_t kSeed = 0x9e3779b97f4a7c15ull;
constexpr uint64_t kMulA = 0xff51afd7ed558ccdull, kMulB = 0xc4ceb9fe1a85ec53ull; // the murmur3 finalizer's
constexpr size_t kChunk = size_t(1) << 20;

uint64_t rotl(uint64_t v, int r) { return (v << r) | (v >> (64 - r)); }

// A 64-bit hash fed in 8-byte words (fast enough to read the whole data set at startup, not cryptographic: it only
// tells two honest installations apart)
class Hasher {
public:
    void word(uint64_t w) { h_ = rotl(h_ ^ (w * kMulA), 31) * kMulB; }
    void bytes(const uint8_t* p, size_t n) {
        size_t i = 0;
        for (; i + 8 <= n; i += 8) {
            uint64_t w;
            std::memcpy(&w, p + i, 8);
            word(w);
        }
        uint64_t tail = 0;
        for (size_t k = 0; i + k < n; ++k) tail |= uint64_t(p[i + k]) << (8 * k);
        word(tail ^ (uint64_t(n - i) << 56));
    }
    void text(const std::string& s) { bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }
    uint64_t value() const {
        uint64_t v = h_;
        v ^= v >> 33;
        v *= kMulA;
        v ^= v >> 33;
        v *= kMulB;
        v ^= v >> 33;
        return v;
    }

private:
    uint64_t h_ = kSeed;
};

// the file's bytes in fixed kChunk chunks (each chunk ends with its tail word), then its size
uint64_t fileHash(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("game files hash: cannot read " + p.string());
    Hasher h;
    std::vector<uint8_t> buf(kChunk);
    uint64_t total = 0;
    for (;;) {
        in.read(reinterpret_cast<char*>(buf.data()), std::streamsize(buf.size()));
        const size_t n = size_t(in.gcount());
        if (n == 0) break;
        h.bytes(buf.data(), n);
        total += n;
        if (n < buf.size()) break;
    }
    h.word(total);
    return h.value();
}

uint64_t bytesHash(const std::vector<uint8_t>& bytes) {
    Hasher h;
    for (size_t at = 0; at < bytes.size(); at += kChunk)
        h.bytes(bytes.data() + at, std::min(kChunk, bytes.size() - at));
    h.word(bytes.size());
    return h.value();
}

std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper(uint8_t(c)));
    return s;
}

} // namespace

GameFilesHash gameFilesHash(const std::filesystem::path& datDir, const std::filesystem::path& exe) {
    GameFilesHash out;
    out.exe = fileHash(exe);
    std::vector<std::pair<std::string, std::filesystem::path>> dats;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(datDir, ec))
        if (e.is_regular_file() && upper(e.path().extension().string()) == ".DAT")
            dats.emplace_back(upper(e.path().filename().string()), e.path());
    if (ec) throw std::runtime_error("game files hash: cannot list " + datDir.string() + " (" + ec.message() + ")");
    std::sort(dats.begin(), dats.end());
    Hasher all;
    all.word(out.exe);
    all.word(dats.size());
    for (const auto& [name, path] : dats) {
        all.text(name);
        all.word(fileHash(path));
    }
    out.all = all.value();
    return out;
}

GameFilesHash gameFilesHash(const DiscImage& disc) {
    GameFilesHash out;
    out.exe = bytesHash(disc.file("SLUS_011.40"));
    std::vector<std::string> files;
    for (const DiscFile& file : disc.files())
        if ((file.path.starts_with("COMMON/") || file.path.starts_with("DAT/")) && file.path.ends_with(".BIN"))
            files.push_back(file.path);
    std::sort(files.begin(), files.end());
    Hasher all;
    all.text("OpenDash PS1 SLUS_011.40"); // different source layouts must not join each other accidentally
    all.word(out.exe);
    all.word(files.size());
    for (const std::string& name : files) {
        all.text(name);
        all.word(bytesHash(disc.file(name)));
    }
    out.all = all.value();
    return out;
}

} // namespace od
