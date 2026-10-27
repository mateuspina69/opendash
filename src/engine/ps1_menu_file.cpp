#include "engine/ps1_menu_file.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <stdexcept>

namespace od {
namespace {
constexpr std::array<const char*, 4> kNames{"SUBSCN01.DAT", "SUBSCN02.DAT", "SUBSCN03.DAT", "SUBSCN06.DAT"};
constexpr std::array<size_t, 4> kOffsets{0, 0x5000, 0xa000, 0x18800};
// The nine map containers in COMMON/SUBMAP.BIN, in MAP0..MAP8 order. MAP2 is the disc's empty terminator;
// each other container has a 33-sector image block and a one-sector terminator (exe 0x5e8700's map loads).
constexpr std::array<const char*, 9> kMapNames{
    "MAP0.DAT", "MAP1.DAT", "MAP2.DAT", "MAP3.DAT", "MAP4.DAT", "MAP5.DAT", "MAP6.DAT", "MAP7.DAT", "MAP8.DAT"};
constexpr std::array<size_t, 10> kMapOffsets{
    0, 0x11000, 0x22000, 0x22800, 0x33800, 0x44800, 0x55800, 0x66800, 0x77800, 0x88800};
template <size_t N> size_t indexOf(std::string name, const std::array<const char*, N>& names) {
    for (char& c : name) c = char(std::toupper(uint8_t(c)));
    const auto it = std::find(names.begin(), names.end(), name);
    return size_t(it - names.begin());
}
} // namespace

bool isPs1MenuFile(const std::string& name) { return indexOf(name, kNames) < kNames.size(); }

GameFile ps1MenuFile(const DiscImage& disc, const std::string& name) {
    const size_t i = indexOf(name, kNames);
    if (i == kNames.size()) throw std::runtime_error("PS1 menu: unknown page " + name);
    const auto bytes = disc.file("COMMON/SUBSCN.BIN");
    const size_t end = i + 1 < kOffsets.size() ? kOffsets[i + 1] : bytes.size();
    if (end > bytes.size() || kOffsets[i] >= end) throw std::runtime_error("PS1 menu: truncated SUBSCN.BIN");
    return ps1GameFile(name, std::span(bytes).subspan(kOffsets[i], end - kOffsets[i]));
}

bool isPs1MapFile(const std::string& name) { return indexOf(name, kMapNames) < kMapNames.size(); }

GameFile ps1MapFile(const DiscImage& disc, const std::string& name) {
    const size_t i = indexOf(name, kMapNames);
    if (i == kMapNames.size()) throw std::runtime_error("PS1 map: unknown picture " + name);
    const auto bytes = disc.file("COMMON/SUBMAP.BIN");
    const size_t begin = kMapOffsets[i], end = kMapOffsets[i + 1];
    if (end > bytes.size()) throw std::runtime_error("PS1 map: truncated SUBMAP.BIN");
    return ps1GameFile(name, std::span(bytes).subspan(begin, end - begin));
}
} // namespace od
