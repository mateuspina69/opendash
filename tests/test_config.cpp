#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "check.hpp"
#include "engine/config.hpp"

namespace fs = std::filesystem;

int main() {
    // Environment overrides are tested separately from these on-disk configuration cases.
    for (const char* key : {"OPENDASH_DATA", "OPENDASH_DISC", "OPENDASH_LANG", "OPENDASH_EXE", "OPENDASH_DAT_DIR", "OPENDASH_LANG_DIR"}) {
#ifdef _WIN32
        _putenv_s(key, "");
#else
        unsetenv(key);
#endif
    }
    const fs::path previous = fs::current_path();
    const fs::path root = fs::temp_directory_path() /
        ("od_test_config_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    fs::current_path(root);
    od::setConfigDir(root);
    {
        std::ofstream ini(root / "opendash.ini");
        ini << "data=disc\ndisc=  \"C:/Games/Mega Man Legends 2 (USA).cue\"  \n"
               "exe='C:/Games/PC/dash2.exe'\ndat_dir=\"C:/Games/PC/dat\"\n";
    }
    const od::Config quoted = od::loadConfig();
    CHECK(quoted.data == "disc");
    CHECK(quoted.disc == fs::path("C:/Games/Mega Man Legends 2 (USA).cue"));
    CHECK(quoted.exePath == fs::path("C:/Games/PC/dash2.exe"));
    CHECK(quoted.datDir == fs::path("C:/Games/PC/dat"));
    CHECK(quoted.lang == "en"); // the USA disc has its own English font and text
    {
        const auto text = u8"data=disc\nlang=ptbr\nlang_dir=\"C:/Jogos/Mídia\"\ndisc=C:/Games/USA.cue\n";
        std::ofstream ini(root / "opendash.ini", std::ios::binary);
        ini.write(reinterpret_cast<const char*>(text), std::streamsize(std::char_traits<char8_t>::length(text)));
    }
    const od::Config plain = od::loadConfig();
    CHECK(plain.disc == fs::path("C:/Games/USA.cue"));
    CHECK(plain.lang == "ptbr"); // an explicit language selection is preserved
    CHECK(plain.langDir == fs::path(u8"C:/Jogos/Mídia"));
    std::ofstream(root / "opendash.ini") << "data=pc\n";
    CHECK(od::loadConfig().lang == "jp");
    fs::current_path(previous);
    fs::remove_all(root);
    return testResult("test_config");
}
