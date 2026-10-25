// The English language pack read off the PS1 disc image (engine/lang_pack) vs the same pack as a folder: the font
// page, the widths, the text / button CLUTs and every message bank byte-identical.
#include <filesystem>
#include <memory>

#include "check.hpp"
#include "engine/lang_pack.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    auto disc = std::make_shared<const od::DiscImage>(argv[1]);
    const auto fromDisc = od::lang::discPack(disc, argv[1]);
    const auto fromFolder = od::lang::folderPack(argv[2]);

    for (const char* name : {"font.bin", "widths.bin", "colours.bin", "buttons.bin"}) {
        CHECK(fromDisc->has(name));
        CHECK(fromDisc->file(name) == fromFolder->file(name));
    }
    int banks = 0;
    for (const auto& e : fs::directory_iterator(fs::path(argv[2]) / "msg")) {
        const std::string name = "msg/" + e.path().filename().string();
        CHECK(fromDisc->has(name));
        CHECK(fromDisc->file(name) == fromFolder->file(name));
        ++banks;
    }
    CHECK(banks > 0);
    int exeBanks = 0;
    for (const auto& e : fs::directory_iterator(fs::path(argv[2]) / "exe")) {
        const std::string name = "exe/" + e.path().filename().string();
        CHECK(fromDisc->has(name));
        if (fromDisc->has(name) && fromDisc->file(name) != fromFolder->file(name))
            std::fprintf(stderr, "%s differs\n", name.c_str()), ++g_failures;
        ++exeBanks;
    }
    CHECK(exeBanks == 27);
    CHECK(!fromDisc->has("msg/NOSUCH.bin"));
    CHECK(!fromDisc->has("art/TITLE.bin")); // not read from the disc yet
    CHECK(fromDisc->folder().empty());
    std::printf("%d message banks, %d exe banks, font, widths, CLUTs identical\n", banks, exeBanks);
    return testResult("test_lang_pack");
}
