#include "engine/exe_coverage.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace od::exe_coverage {

bool g_enabled = false;

namespace {
// dash2.exe's image: 0x400000 .. past the last section (0xa7xxxx); a fixed window keeps the file layout stable
constexpr uint32_t kLo = 0x400000, kSize = 0x800000;
constexpr uint32_t kVersion = 1;
std::vector<uint8_t> g_flags;
std::filesystem::path g_out;

void dump() {
    std::vector<uint8_t> merged(kSize, 0);
    {
        std::ifstream in(g_out, std::ios::binary);
        std::vector<uint8_t> old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (old.size() == 16 + size_t(kSize) && std::memcmp(old.data(), "ODXC", 4) == 0) {
            uint32_t ver, lo, size;
            std::memcpy(&ver, old.data() + 4, 4);
            std::memcpy(&lo, old.data() + 8, 4);
            std::memcpy(&size, old.data() + 12, 4);
            if (ver == kVersion && lo == kLo && size == kSize) std::memcpy(merged.data(), old.data() + 16, kSize);
        } else if (!old.empty()) {
            std::fprintf(stderr, "exe coverage: %s is not a coverage file, overwritten\n", g_out.string().c_str());
        }
    }
    size_t file = 0;
    for (uint32_t i = 0; i < kSize; ++i) {
        merged[i] |= g_flags[i];
        file += (merged[i] & kFileRead) != 0;
    }
    std::error_code ec;
    if (g_out.has_parent_path()) std::filesystem::create_directories(g_out.parent_path(), ec);
    std::ofstream out(g_out, std::ios::binary | std::ios::trunc);
    const uint32_t hdr[3] = {kVersion, kLo, kSize};
    out.write("ODXC", 4);
    out.write(reinterpret_cast<const char*>(hdr), sizeof hdr);
    out.write(reinterpret_cast<const char*>(merged.data()), kSize);
    out.close();
    if (!out) {
        std::fprintf(stderr, "exe coverage: FAILED to write %s\n", g_out.string().c_str());
        return;
    }
    std::fprintf(stderr, "exe coverage: %zu file bytes depended on (union) -> %s\n", file, g_out.string().c_str());
}
} // namespace

void enable(const std::filesystem::path& out) {
    if (g_enabled) return;
    g_out = out;
    g_flags.assign(kSize, 0);
    g_enabled = true;
    std::atexit(dump);
}

void mark(uint32_t va, uint32_t n, uint8_t flag) {
    if (va < kLo) return;
    for (uint32_t i = 0; i < n && va - kLo + i < kSize; ++i) g_flags[va - kLo + i] |= flag;
}

} // namespace od::exe_coverage
