// Native disc SFX banks must reach the same WAV-bank interface as PC files, without a PC-bank template.
#include <algorithm>
#include <cstring>

#include "check.hpp"
#include "engine/dat.hpp"
#include "engine/disc_image.hpp"
#include "engine/game_file.hpp"
#include "engine/ps1_container.hpp"

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const od::DiscImage disc(argv[1]);
    size_t banks = 0, entries = 0, audibleSamples = 0;
    for (const od::DiscFile& file : disc.files()) {
        if (!file.path.ends_with(".BIN")) continue;
        const auto bytes = disc.file(file.path);
        const auto blocks = od::ps1Blocks(bytes);
        const od::GameFile game = od::ps1GameFile(file.path, bytes);
        size_t expected = 0;
        for (size_t i = 0; i < blocks.size(); ++i) {
            if (blocks[i].type != od::kPs1SoundBank) continue;
            // Banks consumed by an 0x08/09 music collection are instruments, not a global SFX slot.
            bool music = false;
            for (size_t j = i; j > 0;) {
                const uint32_t type = blocks[--j].type;
                if (type == od::kPs1SoundBank) break;
                if (type == 8 || type == 9) { music = true; break; }
            }
            if (!music) ++expected;
        }
        size_t actual = 0;
        for (const od::FilePart& part : game.parts) {
            if (part.kind != od::FilePart::Kind::WavSfxBank) continue;
            ++actual;
            ++banks;
            const auto& data = part.bytes;
            CHECK(data.size() >= 4);
            if (data.size() < 4) continue;
            const uint32_t table = od::rd32(data.data());
            CHECK(table >= 4 && table <= data.size() && (table - 4) % 8 == 0);
            if (table > data.size() || table < 4) continue;
            entries += (table - 4) / 8;
            for (uint32_t at = 4; at < table; at += 8) {
                const uint8_t sample = data[at];
                if (!sample || !data[at + 1]) continue;
                const size_t slot = size_t(table) + sample * 8;
                CHECK(slot + 8 <= data.size());
                if (slot + 8 > data.size()) continue;
                const size_t wav = size_t(table) + od::rd32(data.data() + slot);
                CHECK(wav + 44 <= data.size());
                if (wav + 44 > data.size()) continue;
                CHECK(std::memcmp(data.data() + wav, "RIFF", 4) == 0);
                CHECK(std::memcmp(data.data() + wav + 8, "WAVEfmt ", 8) == 0);
                const uint32_t pcmBytes = od::rd32(data.data() + wav + 40);
                CHECK(wav + 44 + pcmBytes <= data.size());
                CHECK(od::rd16(data.data() + at + 2) != 0);
                if (wav + 44 + pcmBytes <= data.size() &&
                    std::any_of(data.begin() + wav + 44, data.begin() + wav + 44 + pcmBytes,
                                [](uint8_t byte) { return byte != 0; })) ++audibleSamples;
                const int32_t loop = int32_t(od::rd32(data.data() + slot + 4));
                CHECK(loop == -1 || (loop >= 0 && uint32_t(loop) < pcmBytes && loop % 2 == 0));
            }
        }
        if (actual != expected) {
            std::fprintf(stderr, "%s: %zu native SFX banks, expected %zu\n", file.path.c_str(), actual, expected);
            ++g_failures;
        }
    }
    std::printf("%zu native SFX banks, %zu tone slots, %zu audible sample references\n", banks, entries, audibleSamples);
    CHECK(banks >= 160 && entries > 1000 && audibleSamples > 1000);
    return testResult("test_ps1_sfx");
}
