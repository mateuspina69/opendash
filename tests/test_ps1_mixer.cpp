// The actual host mixer must play disc music packets, including silent-clock runs used by scripted gameplay.
#include <memory>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "check.hpp"
#include "audio/mixer.hpp"
#include "engine/disc_image.hpp"
#include "engine/game_file.hpp"

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const od::DiscImage disc(argv[1]);
    const od::GameFile stage = od::ps1GameFile("ST09T.DAT", disc.file("DAT/ST09T.BIN"));
    const od::FilePart* music = stage.first(od::FilePart::Kind::Ps1Music);
    CHECK(music != nullptr);
    if (!music) return testResult("test_ps1_mixer");
    od::audio::Mixer mixer({}); // no PC snd/ fallback and no sound device
    const uint32_t stream = mixer.createStream(music->bytes, music->arg);
    mixer.streamVolume(stream, 127);
    mixer.streamPlay(stream);
    mixer.advance(1.0);
    const auto first = mixer.takePeak();
    CHECK(first[0] > 0.001f && first[1] > 0.001f);
    CHECK(mixer.streamPlaying(stream));
    mixer.streamPause(stream);
    mixer.advance(0.1);
    const auto paused = mixer.takePeak();
    CHECK(paused[0] == 0.0f && paused[1] == 0.0f);
    mixer.streamResume(stream);
    mixer.advance(0.2);
    const auto resumed = mixer.takePeak();
    CHECK(resumed[0] > 0.001f || resumed[1] > 0.001f);
    mixer.streamStop(stream);
    CHECK(!mixer.streamPlaying(stream));
    mixer.streamPlay(stream); // rewind sampler state as well as the host's interpolation state
    mixer.advance(1.0);
    CHECK(mixer.takePeak() == first);
    mixer.destroyStream(stream);
    CHECK(!mixer.streamPlaying(stream));
    {
        namespace fs = std::filesystem;
        const auto scratch = fs::temp_directory_path() /
            ("opendash-voice-priority-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(scratch / "restored");
        fs::create_directories(scratch / "original");
        auto voice = [&](const fs::path& path, std::initializer_list<uint8_t> bytes) {
            std::ofstream out(path, std::ios::binary);
            for (uint8_t b : bytes) out.put(char(b));
        };
        voice(scratch / "restored/test.dat", {8,0,0,0,9,0,0,0,0x11});
        voice(scratch / "original/test.dat", {12,0,0,0,13,0,0,0,14,0,0,0,0x22,0x33});
        od::audio::Mixer voices({});
        voices.setVoiceDirs({scratch / "restored", scratch / "original"});
        CHECK(voices.readVoiceTrack("test.dat", 0) == std::vector<uint8_t>{0x11});
        CHECK(voices.readVoiceTrack("test.dat", 1) == std::vector<uint8_t>{0x33});
        voices.setVoiceSource([](const std::string&, uint32_t) { return std::vector<uint8_t>{0x44}; }, true);
        CHECK(voices.readVoiceTrack("test.dat", 0) == std::vector<uint8_t>{0x11});
        CHECK(voices.readVoiceTrack("test.dat", 2) == std::vector<uint8_t>{0x44});
        // An empty native sound directory must not accidentally read a file in the working directory.
        const auto previous = fs::current_path();
        fs::current_path(scratch / "original");
        od::audio::Mixer noPcSound({});
        CHECK(noPcSound.readVoiceTrack("test.dat", 0).empty());
        fs::current_path(previous);
        fs::remove_all(scratch);
    }
    return testResult("test_ps1_mixer");
}
