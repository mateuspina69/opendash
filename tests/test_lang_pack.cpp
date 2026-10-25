// The English language pack read off the PS1 disc image (engine/lang_pack) vs the same pack as a folder: the font
// page, the widths, the text / button CLUTs and every message bank byte-identical.
#include <filesystem>
#include <memory>

#include "check.hpp"
#include "engine/dat.hpp"
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
    // the voices: snd/<file> is u32 offsets[n + 1], track i = [o[i], o[i + 1])
    int voiceFiles = 0, voiceTracks = 0;
    for (const auto& e : fs::directory_iterator(fs::path(argv[2]) / "snd")) {
        const std::string file = e.path().filename().string();
        const auto pc = od::readFile(e.path());
        const uint32_t n = od::rd32(pc.data()) / 4 - 1;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t a = od::rd32(pc.data() + 4 * i), b = od::rd32(pc.data() + 4 * i + 4);
            if (fromDisc->voiceTrack(file, i) != std::vector<uint8_t>(pc.begin() + a, pc.begin() + b))
                std::fprintf(stderr, "%s track %u differs\n", file.c_str(), i), ++g_failures;
            ++voiceTracks;
        }
        CHECK(fromDisc->voiceTrack(file, n).empty()); // no track past the file's
        ++voiceFiles;
    }
    CHECK(voiceFiles == 14);
    std::printf("%d message banks, %d exe banks, font, widths, CLUTs, %d voice tracks of %d files identical\n", banks,
                exeBanks, voiceTracks, voiceFiles);
    return testResult("test_lang_pack");
}
