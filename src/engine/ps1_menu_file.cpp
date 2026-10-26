#include "engine/ps1_menu_file.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <stdexcept>

namespace od {
namespace {
constexpr std::array<const char*, 4> kNames{"SUBSCN01.DAT", "SUBSCN02.DAT", "SUBSCN03.DAT", "SUBSCN06.DAT"};
constexpr std::array<size_t, 4> kOffsets{0, 0x5000, 0xa000, 0x18800};
size_t indexOf(std::string name) {
    for (char& c : name) c = char(std::toupper(uint8_t(c)));
    const auto it = std::find(kNames.begin(), kNames.end(), name);
    return size_t(it - kNames.begin());
}
} // namespace

bool isPs1MenuFile(const std::string& name) { return indexOf(name) < kNames.size(); }

GameFile ps1MenuFile(const DiscImage& disc, const std::string& name) {
    const size_t i = indexOf(name);
    if (i == kNames.size()) throw std::runtime_error("PS1 menu: unknown page " + name);
    const auto bytes = disc.file("COMMON/SUBSCN.BIN");
    const size_t end = i + 1 < kOffsets.size() ? kOffsets[i + 1] : bytes.size();
    if (end > bytes.size() || kOffsets[i] >= end) throw std::runtime_error("PS1 menu: truncated SUBSCN.BIN");
    return ps1GameFile(name, std::span(bytes).subspan(kOffsets[i], end - kOffsets[i]));
}
} // namespace od
