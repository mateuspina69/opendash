// OpenDash audio output: an SDL3 software mixer behind od::port::SoundBackend (game/audio_logic hands it what the
// original handed DirectSound). Reimplemented, not bit-exact:
//  - buffers: one per bank entry with a sample, PCM16 mono resampled (linear) from the entry's playback frequency
//    to the 44.1 kHz mix; looped samples play [0, end) once, then [loopOfs, end) forever (the intro + loop
//    DirectSound buffer pair of FUN_0040a400); Play restarts from 0 like SetCurrentPosition(0) + Play.
//  - volume / pan: the game's DirectSound mapping (FUN_0040a800 / FUN_0040a860): volume 0..127 -> cB
//    (0 = -10000, else v * 3000 / 127 - 3000), pan 0..128 -> cB (p * 10000 / 64 - 10000) as a per-side
//    attenuation (DirectSound panning), gain = 10^(cB / 2000).
//  - streams: MP3 (music slots, voice clips) decoded with dr_mp3 (vendored, public domain / MIT-0) while mixing,
//    looped by seeking to 0 at the end when the slot's loop flag is set (the Xaudio stream of the original
//    restarts the same way); stream volume = the same cB mapping.
//  - language packs (engine/lang): ODXA voice tracks (CD-XA ADPCM, 18.9 / 37.8 kHz) are decoded per sector while
//    mixing, resampled like MP3 streams of another rate; their packs replace snd/ files of the same name.
//  - without an audio device (--shot runs, no device) advance(seconds) runs the mixer silently so stream "playing"
//    states (the message system waits on voice clips) still progress.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "game/audio_logic.hpp"

struct SDL_AudioStream;

namespace od::audio {

class Mixer : public od::port::SoundBackend {
public:
    static constexpr int kRate = 44100;

    explicit Mixer(std::filesystem::path sndDir);
    ~Mixer() override;
    // opens the default playback device; false = no device (silent mode, advance() keeps time)
    bool open();
    bool hasDevice() const { return stream_ != nullptr; }
    // silent mode: mix `seconds` of output into nothing
    void advance(double seconds);
    void setMasterGain(float g); // user volume 0..1 (F1 menu); 0 = mute
    float masterGain() const { return master_; }
    // peak |sample| of the output since the last call (0..1), per channel, and the number of samples mixed
    std::array<float, 2> takePeak();
    struct Stats {
        int buffers = 0, playingBuffers = 0, streams = 0, playingStreams = 0;
    };
    Stats stats();

    // ---- SoundBackend ----
    uint32_t createBuffer(uint32_t wav, int32_t loopOfs) override;
    void releaseBuffer(uint32_t h) override;
    bool play(uint32_t h, uint32_t freq) override;
    void setVolume(uint32_t h, int32_t v) override;
    void setPan(uint32_t h, int32_t p) override;
    void setFrequency(uint32_t h, uint32_t f) override;
    void stop(uint32_t h) override;
    void pause(uint32_t h) override;
    void resume(uint32_t h) override;
    uint32_t createStream(std::span<const uint8_t> mp3, uint32_t loop) override;
    void destroyStream(uint32_t s) override;
    void streamPlay(uint32_t s) override;
    void streamStop(uint32_t s) override;
    void streamVolume(uint32_t s, int32_t v) override;
    void streamPause(uint32_t s) override;
    void streamResume(uint32_t s) override;
    bool streamPlaying(uint32_t s) override;
    std::vector<uint8_t> readVoiceTrack(const std::string& file, uint32_t track) override;
    // language pack voice dirs, searched in order before the game's snd/ (same file names; ODXA or MP3 tracks):
    // e.g. <pack>/snd_restored (AI-restored voices), <pack>/snd. Empty = the game's snd/ only
    void setVoiceDirs(std::vector<std::filesystem::path> dirs);
    // a pack without a folder (the PS1 disc, engine/lang_pack): asked for a track before the voice dirs (empty: none)
    using VoiceSource = std::function<std::vector<uint8_t>(const std::string& file, uint32_t track)>;
    void setVoiceSource(VoiceSource source);

    // the game's DirectSound volume / pan mapping as linear gains (exposed for tests / logs)
    static float volumeGain(int32_t v);
    static std::array<float, 2> panGains(int32_t p);

private:
    struct Buffer {
        std::vector<int16_t> pcm;
        int64_t loopStart = -1; // frame where the loop part starts, -1 = one-shot
        uint32_t freq = 22050;
        float gain = 1.0f;
        std::array<float, 2> pan{1.0f, 1.0f};
        double pos = 0.0;
        bool playing = false, paused = false;
    };
    struct Stream;
    void mix(float* out, int frames); // under lock_
    static void callback(void* user, SDL_AudioStream* s, int additional, int total);

    std::filesystem::path sndDir_;
    std::vector<std::filesystem::path> voiceDirs_;
    VoiceSource voiceSource_;
    SDL_AudioStream* stream_ = nullptr;
    std::mutex lock_;
    std::map<uint32_t, Buffer> buffers_;
    std::map<uint32_t, std::unique_ptr<Stream>> streams_;
    uint32_t nextBuffer_ = 0x100, nextStream_ = 1;
    float master_ = 1.0f;
    std::array<float, 2> peak_{0.0f, 0.0f};
    std::vector<float> scratch_, decode_;
};

} // namespace od::audio
