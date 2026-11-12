#include "app/music_choice.hpp"

#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

#include <SDL3/SDL_log.h>

namespace od::music_choice {

namespace {
bool g_extended = true;
std::filesystem::path g_dir;
} // namespace

void configure(bool extended, std::filesystem::path originalsDir) {
    g_extended = extended;
    g_dir = std::move(originalsDir);
}

void setExtended(bool extended) { g_extended = extended; }

bool extended() { return g_extended; }

bool available() {
    std::error_code ec;
    return !g_dir.empty() && std::filesystem::is_directory(g_dir, ec) &&
           std::filesystem::directory_iterator(g_dir, ec) != std::filesystem::directory_iterator();
}

const GameFile& musicOf(const std::string& name, const GameFile& file, GameFile& storage) {
    if (g_extended || g_dir.empty()) return file;
    const std::filesystem::path p = g_dir / name;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec)) return file;
    try {
        std::ifstream in(p, std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        storage = pcGameFile(name, bytes);
        return storage;
    } catch (const std::exception& e) {
        SDL_Log("music choice: %s: %s (the extended music kept)", p.string().c_str(), e.what());
        return file;
    }
}

} // namespace od::music_choice
