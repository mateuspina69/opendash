// The PS1 stores the four status-screen page containers consecutively in COMMON/SUBSCN.BIN.
#pragma once

#include <string>
#include <cstddef>

#include "engine/disc_image.hpp"
#include "engine/game_file.hpp"

namespace od {
// The four image/model containers are followed by a fifth container holding the shared menu overlay.
inline constexpr size_t kPs1MenuCodeOffset = 0x2a800;
bool isPs1MenuFile(const std::string& name);
GameFile ps1MenuFile(const DiscImage& disc, const std::string& name);
} // namespace od
