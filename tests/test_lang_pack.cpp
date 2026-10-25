// The English language pack read off the PS1 disc image (engine/lang_pack) vs the same pack as a folder, byte for
// byte: the font page, the widths, the text / button CLUTs, the message banks, the exe banks, the title art, the voice
// tracks and the SFX banks rebuilt from the PS1 sound banks (argv: the cue, the pack folder, the PC dat/).
#include <filesystem>
#include <memory>

#include "check.hpp"
#include "engine/dat.hpp"
#include "engine/game_file.hpp"
#include "engine/lang_pack.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 4) return 2;
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
    CHECK(fromDisc->has("art/TITLE.bin") && fromDisc->file("art/TITLE.bin") == fromFolder->file("art/TITLE.bin"));
    CHECK(!fromDisc->has("art/GAME.bin")); // not read from the disc yet
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
    // the SFX banks: for every PC bank (argv[3] = the PC dat/), the disc's rebuilt bank = the folder's (empty where
    // the folder has none: the PC's bank stays)
    int sfxBanks = 0, sfxRebuilt = 0;
    for (const auto& e : fs::directory_iterator(argv[3])) {
        const std::string name = e.path().filename().string();
        const od::GameFile pc = od::pcGameFile(name, od::readFile(e.path()));
        for (const od::FilePart& p : pc.parts) {
            if (p.kind != od::FilePart::Kind::WavSfxBank) continue;
            const auto d = fromDisc->sfxBank(name, p.arg, p.extra, p.bytes);
            if (d != fromFolder->sfxBank(name, p.arg, p.extra, p.bytes))
                std::fprintf(stderr, "SFX bank %s %u %u differs\n", name.c_str(), p.arg, p.extra), ++g_failures;
            sfxRebuilt += !d.empty();
            ++sfxBanks;
        }
    }
    CHECK(sfxRebuilt == 60);
    std::printf("%d SFX banks, %d rebuilt from the disc as the folder's\n", sfxBanks, sfxRebuilt);
    std::printf("%d message banks, %d exe banks, font, widths, CLUTs, %d voice tracks of %d files identical\n", banks,
                exeBanks, voiceTracks, voiceFiles);
    return testResult("test_lang_pack");
}
