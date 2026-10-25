// Screenshots of the scripted runs (--shot, --shot-every): JPEG, never BMP (a 1920x1080 BMP is 6 MB; --shot-every
// runs write hundreds of them).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace od::app {

// `path` with its extension replaced by .jpg (a path without one gets .jpg appended).
std::string shotJpegPath(const std::string& path);

// Saves the RGBA8 pixels (`w` x `h`, the renderer's read-back order) to shotJpegPath(path), quality 90.
// Logs and returns false on failure.
bool saveShot(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba);

} // namespace od::app
