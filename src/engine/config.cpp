#include "config.hpp"

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace od {

namespace {
#ifdef _WIN32
constexpr const char* kDefaultDatDir = "C:/Program Files (x86)/Capcom/Dash2/dat";
#endif
constexpr const char* kDefaultLangDir = ""; // empty: lang/ next to the executable (the app resolves it from the exe)

std::string trim(std::string s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    const auto e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? std::string{} : s.substr(b, e - b + 1);
}

std::filesystem::path pathValue(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                              (value.front() == '\'' && value.back() == '\'')))
        value = value.substr(1, value.size() - 2);
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}
std::filesystem::path g_iniPath = "opendash.ini";

// the default install path: Windows's, or the same path in the default Wine prefix
std::filesystem::path defaultDatDir() {
#ifdef _WIN32
    return kDefaultDatDir;
#else
    return std::filesystem::path(envValue("HOME")) / ".wine/drive_c/Program Files (x86)/Capcom/Dash2/dat";
#endif
}
} // namespace

std::string envValue(const char* name) {
#ifdef _WIN32
    char* env = nullptr;
    size_t len = 0;
    std::string value;
    if (_dupenv_s(&env, &len, name) == 0 && env) {
        value = env;
        free(env);
    }
    return value;
#else
    const char* env = std::getenv(name);
    return env ? env : "";
#endif
}

void setConfigDir(const std::filesystem::path& dir) {
    // an opendash.ini in the working directory keeps winning (the setups made before it moved next to the exe)
    std::error_code ec;
    g_iniPath = std::filesystem::exists("opendash.ini", ec) ? std::filesystem::path("opendash.ini") : dir / "opendash.ini";
}

const std::filesystem::path& configPath() { return g_iniPath; }

Config loadConfig() {
    Config cfg;
    bool languageSpecified = false;
    cfg.datDir = defaultDatDir();
    cfg.langDir = kDefaultLangDir;
    if (std::ifstream ini(g_iniPath); ini) {
        for (std::string line; std::getline(ini, line);) {
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = trim(line.substr(0, eq));
            if (key == "dat_dir") cfg.datDir = pathValue(line.substr(eq + 1));
            if (key == "exe") cfg.exePath = pathValue(line.substr(eq + 1));
            if (key == "aa") cfg.aa = trim(line.substr(eq + 1));
            if (key == "aa_quality") cfg.aaQuality = trim(line.substr(eq + 1));
            if (key == "lang_dir") cfg.langDir = pathValue(line.substr(eq + 1));
            if (key == "lang") { cfg.lang = trim(line.substr(eq + 1)); languageSpecified = true; }
            if (key == "disc") cfg.disc = pathValue(line.substr(eq + 1));
            if (key == "data") cfg.data = trim(line.substr(eq + 1));
            if (key == "restored_voices") cfg.restoredVoices = trim(line.substr(eq + 1)) != "0";
            if (key == "story_save") cfg.storySave = trim(line.substr(eq + 1));
            if (key == "display") cfg.display = trim(line.substr(eq + 1));
            if (key == "window") cfg.window = trim(line.substr(eq + 1));
            if (key == "fullscreen_mode") cfg.fullscreenMode = trim(line.substr(eq + 1));
            if (key == "vsync") cfg.vsync = trim(line.substr(eq + 1)) != "0";
            if (key == "fog") cfg.fog = trim(line.substr(eq + 1));
            if (key == "fog_source") cfg.fogSource = trim(line.substr(eq + 1));
            if (key == "water") cfg.water = trim(line.substr(eq + 1));
            if (key == "fog_distance") cfg.fogDistance = std::strtof(trim(line.substr(eq + 1)).c_str(), nullptr);
            if (key == "keys") cfg.keys = trim(line.substr(eq + 1));
            if (key == "pad") cfg.pad = trim(line.substr(eq + 1));
            if (key == "mouse") cfg.mouse = trim(line.substr(eq + 1));
            if (key == "net") cfg.net = trim(line.substr(eq + 1));
            if (key == "net_port") cfg.netPort = std::atoi(trim(line.substr(eq + 1)).c_str());
            if (key == "net_join") cfg.netJoin = trim(line.substr(eq + 1));
            if (key == "net_name") cfg.netName = trim(line.substr(eq + 1));
            if (key == "net_key") cfg.netKey = trim(line.substr(eq + 1));
        }
    }
    if (const std::string v = envValue("OPENDASH_DAT_DIR"); !v.empty()) cfg.datDir = pathValue(v);
    if (const std::string v = envValue("OPENDASH_EXE"); !v.empty()) cfg.exePath = pathValue(v);
    if (const std::string v = envValue("OPENDASH_LANG_DIR"); !v.empty()) cfg.langDir = pathValue(v);
    if (const std::string v = envValue("OPENDASH_LANG"); !v.empty()) { cfg.lang = v; languageSpecified = true; }
    if (const std::string v = envValue("OPENDASH_DISC"); !v.empty()) cfg.disc = pathValue(v);
    if (const std::string v = envValue("OPENDASH_DATA"); !v.empty()) cfg.data = v;
    if (cfg.exePath.empty()) cfg.exePath = cfg.datDir.parent_path() / "dash2.exe";
    if (cfg.data == "disc" && !languageSpecified) cfg.lang = "en";
    return cfg;
}

void saveConfigValue(const std::string& key, const std::string& value) {
    std::vector<std::string> lines;
    bool found = false;
    if (std::ifstream ini(g_iniPath); ini) {
        for (std::string line; std::getline(ini, line);) {
            const auto eq = line.find('=');
            if (eq != std::string::npos && trim(line.substr(0, eq)) == key) {
                line = key + "=" + value;
                found = true;
            }
            lines.push_back(line);
        }
    }
    if (!found) lines.push_back(key + "=" + value);
    std::ofstream out(g_iniPath, std::ios::trunc);
    for (const auto& l : lines) out << l << '\n';
}

} // namespace od
