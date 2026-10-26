// Disc-only voice references use the same four-byte records and clip order as the engine's PC interface.
#include <memory>
#include <set>

#include "check.hpp"
#include "engine/pc_data_source.hpp"
#include "engine/ps1_data_source.hpp"
#include "engine/ps1_xa.hpp"
#include "engine/lang_pack.hpp"
#include "engine/lang.hpp"

namespace {

void syntheticClips() {
    const od::DiscFile file{"XA/TEST.XA", 100, 8 * od::kForm1Bytes};
    std::vector<od::RawSector> sectors(8);
    auto audio = [&](size_t sector, uint8_t channel, bool eof) {
        sectors[sector][17] = channel;
        sectors[sector][18] = uint8_t(0x04 | (eof ? 0x80 : 0));
        sectors[sector][19] = 0x04;
    };
    audio(0, 1, false);
    audio(1, 0, true);
    audio(2, 1, true);
    audio(3, 0, true);
    audio(5, 1, false); // no EOF: keep the incomplete final clip
    const auto clips = od::ps1XaClips(file, [&](uint32_t lba) { return sectors.at(lba - file.lba); });
    CHECK(clips.size() == 4);
    CHECK(clips[0].channel == 0 && clips[0].sectors == std::vector<uint32_t>{101});
    CHECK(clips[1].channel == 0 && clips[1].sectors == std::vector<uint32_t>{103});
    CHECK(clips[2].channel == 1 && clips[2].sectors == (std::vector<uint32_t>{100, 102}));
    CHECK(clips[3].sectors == std::vector<uint32_t>{105});
    const std::array<uint8_t, 8> descriptor{0, 0, 0, 0, 0x80, 0, 3, 0};
    CHECK(od::ps1XaTrack(descriptor, file.lba, clips) == 0); // aligned seek and low-rate flag
    const std::array<uint8_t, 8> highDescriptor{0, 1, 0, 0, 0x82, 1, 16, 0};
    const std::vector<od::Ps1XaClip> highClips{{2, 0, {file.lba + 0x10002}}};
    CHECK(od::ps1XaTrack(highDescriptor, file.lba, highClips) == 0);
    auto throws = [&](std::array<uint8_t, 8> d) {
        try { (void)od::ps1XaTrack(d, file.lba, clips); } catch (const std::runtime_error&) { return true; }
        return false;
    };
    CHECK(throws({0, 0, 3, 0, 0, 0, 3, 0})); // empty range
    CHECK(throws({0, 0, 4, 0, 0, 0, 3, 0})); // reversed range
    CHECK(throws({0, 0, 0, 0, 7, 0, 3, 0})); // absent channel
    CHECK(throws({0, 0, 0, 0, 0, 0, 5, 0})); // range spans two clips
    CHECK(throws({0, 0, 0, 0, 1, 0, 2, 0})); // range cuts a clip short
    CHECK(od::ps1XaClips(od::DiscFile{"EMPTY", 0, 0}, [](uint32_t) { return od::RawSector{}; }).empty());
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) return 2;
    syntheticClips();
    const od::PcDataSource pc(argv[2], argv[1]);
    const od::Ps1DataSource ps1(std::make_shared<const od::DiscImage>(argv[3]));
    for (int stage = 0; stage < od::kStageCount; ++stage)
        for (const auto id : {od::TableId::StageVoices, od::TableId::StageSecondVoices}) {
            const od::TableKey stageKey{id, uint32_t(stage)};
            if (pc.hasTable(stageKey)) CHECK(ps1.hasTable(stageKey));
        }
    for (const auto& t : od::Ps1DataSource::addressTables()) {
        if (t.key.id != od::TableId::StageVoices && t.key.id != od::TableId::StageSecondVoices) continue;
        const auto got = ps1.stateData(ps1.stateAddress(t.key), t.bytes).size();
        if (got != t.bytes) {
            std::fprintf(stderr, "%s own extent: %zu != %u\n", od::describe(t.key).c_str(), got, t.bytes);
            ++g_failures;
        }
    }
    CHECK(ps1.hasTable({od::TableId::VoiceFileNames, 0}));
    CHECK(ps1.hasTable({od::TableId::AudioBuiltInVoices, 0}));
    if (ps1.hasTable({od::TableId::VoiceFileNames, 0})) {
        const auto names = ps1.table({od::TableId::VoiceFileNames, 0}, 0x400);
        const auto expected = pc.table({od::TableId::VoiceFileNames, 0}, 0x400);
        for (size_t i = 0; i < 20; ++i) CHECK(names.followString(i * 4, 32) == expected.followString(i * 4, 32));
    }
    const od::TableKey key{od::TableId::StageVoices, 8};
    const auto actual = ps1.table(key, 23 * 4), expected = pc.table(key, 23 * 4);
    CHECK(std::equal(actual.bytes(0, actual.size()).begin(), actual.bytes(0, actual.size()).end(),
                     expected.bytes(0, expected.size()).begin()));
    const auto pointer = ps1.stateAddress(key);
    CHECK(ps1.stateData(pointer + 4, 4).u16(2) == 1);
    // The table pointer is materialized before the overlay is loaded; loading stage files must preserve it.
    (void)ps1.gameFile("ST08T.DAT");
    (void)ps1.gameFile("ST12T.DAT");
    CHECK(ps1.stateAddress(key) == pointer);
    CHECK(ps1.stateData(pointer + 4, 4).u16(2) == 1);
    size_t voiceTables = 0, references = 0;
    std::set<std::pair<uint16_t, uint16_t>> tracks;
    for (const auto& t : od::Ps1DataSource::addressTables()) {
        if (t.key.id != od::TableId::StageVoices && t.key.id != od::TableId::StageSecondVoices) continue;
        const auto entries = ps1.table(t.key, t.bytes), want = pc.table(t.key, t.bytes);
        const uint32_t base = ps1.stateAddress(t.key);
        for (size_t i = 0; i < t.bytes; i += 4) {
            const uint16_t file = entries.u16(i), track = entries.u16(i + 2);
            if (!file && !track) break; // the PS1 descriptor terminator; label extents may include adjacent PC data
            if (file != want.u16(i) || track != want.u16(i + 2)) {
                std::fprintf(stderr, "%s entry %zu: {%u,%u} != {%u,%u}\n", od::describe(t.key).c_str(), i / 4,
                             file, track, want.u16(i), want.u16(i + 2));
                ++g_failures;
            }
            const auto pointed = ps1.stateData(base + uint32_t(i), 4);
            CHECK(pointed.u16(0) == file && pointed.u16(2) == track);
            tracks.emplace(file, track);
            ++references;
        }
        ++voiceTables;
    }
    if (ps1.hasTable({od::TableId::AudioBuiltInVoices, 0})) {
        const uint32_t base = ps1.stateAddress({od::TableId::AudioBuiltInVoices, 0});
        const uint32_t expectedBase = pc.stateAddress({od::TableId::AudioBuiltInVoices, 0});
        for (uint32_t i = 0x7f00; i < 0x7f05; ++i) {
            const auto entry = ps1.stateData(base + i * 4, 4), want = pc.stateData(expectedBase + i * 4, 4);
            CHECK(entry.u16(0) == want.u16(0) && entry.u16(2) == want.u16(2));
            tracks.emplace(entry.u16(0), entry.u16(2));
        }
    }
    CHECK(voiceTables == 87 && references > 2900 && tracks.size() == 424);
    // ST37's general label names only the used prefix. The voice table provides its complete 49 descriptors.
    const od::TableKey full{od::TableId::StageSecondVoices, 0x37};
    CHECK(ps1.stateData(ps1.stateAddress(full) + 48 * 4, 4).u16(2) == 48);
    const auto names = ps1.table({od::TableId::VoiceFileNames, 0}, 0x400);
    const auto pack = od::lang::discPack(std::make_shared<const od::DiscImage>(argv[3]), argv[3]);
    std::map<uint16_t, uint16_t> lastTracks;
    for (const auto& [file, track] : tracks) {
        const std::string name = names.followString(file * 4, 32);
        const auto bytes = pack->voiceTrack(name, track);
        const auto decoded = od::lang::parseXaTrack(bytes);
        CHECK(decoded && decoded->sectors > 0);
        lastTracks[file] = track;
    }
    CHECK(lastTracks.size() == 14);
    for (const auto& [file, last] : lastTracks)
        CHECK(pack->voiceTrack(names.followString(file * 4, 32), last + 1).empty());
    std::printf("%zu voice tables, %zu references, %zu unique tracks\n", voiceTables, references, tracks.size());
    return testResult("test_ps1_voices");
}
