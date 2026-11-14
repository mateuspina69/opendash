#include "app/screenshot.hpp"

#include <SDL3/SDL_log.h>
#include <filesystem>
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

bool savePhoto(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba) {
    if (w <= 0 || h <= 0 || rgba.size() != size_t(w) * size_t(h) * 4) {
        SDL_Log("photo failed: no picture (%d x %d)", w, h);
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::vector<uint8_t> rgb(size_t(w) * size_t(h) * 3); // the back buffer's alpha is not the picture's
    for (size_t i = 0, n = size_t(w) * size_t(h); i < n; ++i)
        for (size_t c = 0; c < 3; ++c) rgb[i * 3 + c] = rgba[i * 4 + c];
    if (!stbi_write_png(path.c_str(), w, h, 3, rgb.data(), w * 3)) {
        SDL_Log("photo failed: %s", path.c_str());
        return false;
    }
    SDL_Log("photo: %s", path.c_str());
    return true;
}

} // namespace od::app
