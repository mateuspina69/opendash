#include "app/screenshot.hpp"

#include <SDL3/SDL_log.h>
#include <stb_image_write.h> // the implementation is compiled in app/ui/hd_art.cpp

namespace od::app {

std::string shotJpegPath(const std::string& path) {
    std::string out = path;
    const size_t slash = out.find_last_of("/\\"), dot = out.rfind('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) out.resize(dot);
    return out + ".jpg";
}

bool saveShot(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba) {
    const std::string out = shotJpegPath(path);
    if (w <= 0 || h <= 0 || rgba.size() != size_t(w) * size_t(h) * 4 ||
        !stbi_write_jpg(out.c_str(), w, h, 4, rgba.data(), 90)) {
        SDL_Log("screenshot failed: %s", out.c_str());
        return false;
    }
    return true;
}

} // namespace od::app
