// SDL3 software mixer (see mixer.hpp).
#include "audio/mixer.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "audio/dr_mp3.h"
#include "audio/music_loops.hpp"
#include "engine/lang.hpp"
#include "engine/file_data.hpp"
#include "engine/ps1_music.hpp"

namespace od::audio {

struct Mixer::Stream {
    std::vector<uint8_t> data;
    drmp3 dec{};
    std::unique_ptr<od::Ps1MusicPlayer> ps1;
    std::optional<od::lang::XaTrack> xa; // ODXA track of a language pack (CD-XA ADPCM) instead of MP3
    od::lang::XaDecoder xaDec;
    uint32_t xaSector = 0;
    std::vector<int16_t> xaL, xaR;
    // music with a loop entry (audio/music_loops): the whole track decoded up front, played [0, loopEnd) then
    // [loopStart, loopEnd) forever instead of restarting the MP3 from 0
    std::vector<int16_t> pcm; // interleaved stereo
    uint32_t loopStart = 0, loopEnd = 0, pcmPos = 0;
    bool ok = false;
    bool loop = false, playing = false, paused = false, ended = false;
    uint32_t channels = 2, rate = kRate;
    float gain = 0.0f;
    double pos = 0.0; // fractional position between the frames a and b (linear rate conversion)
    float al = 0.0f, ar = 0.0f, bl = 0.0f, br = 0.0f;
    bool primed = false; // a / b hold the first two source frames
    std::vector<float> pending; // decoded, not yet mixed (interleaved stereo at `rate`)
    size_t pendingAt = 0;

    ~Stream() {
        if (ok && !xa && !ps1) drmp3_uninit(&dec);
    }
    bool init() {
        if (od::isPs1Music(data)) {
            try {
                ps1 = std::make_unique<od::Ps1MusicPlayer>(data, loop);
                rate = Mixer::kRate;
                channels = 2;
                return ok = true;
            } catch (const std::exception& e) {
                SDL_Log("audio: PS1 music: %s", e.what());
                return false;
            }
        }
        if ((xa = od::lang::parseXaTrack(data))) {
            channels = xa->stereo ? 2 : 1;
            rate = xa->rate;
            return ok = true;
        }
        ok = drmp3_init_memory(&dec, data.data(), data.size(), nullptr) != 0;
        if (ok) {
            channels = dec.channels;
            rate = dec.sampleRate;
        }
        return ok;
    }
    void rewind() {
        if (ps1) ps1->rewind();
        else if (ok && !xa) drmp3_seek_to_pcm_frame(&dec, 0);
        xaSector = 0;
        xaDec = {};
        pcmPos = 0;
        pending.clear();
        pendingAt = 0;
        ended = false;
        primed = false;
        pos = 0.0;
    }
    // next stereo frame at the stream's own rate; false at the end (after looping if set)
    bool next(float& l, float& r) {
        if (ps1) {
            if (ps1->next(l, r)) return true;
            ended = true;
            return false;
        }
        if (!pcm.empty()) {
            if (pcmPos >= loopEnd) {
                if (!loop) {
                    ended = true;
                    return false;
                }
                pcmPos = loopStart;
            }
            l = float(pcm[size_t(pcmPos) * 2]) / 32768.0f;
            r = float(pcm[size_t(pcmPos) * 2 + 1]) / 32768.0f;
            ++pcmPos;
            return true;
        }
        if (pendingAt >= pending.size() && xa) { // one XA sector at a time
            if (xaSector >= xa->sectors && loop) {
                xaSector = 0;
                xaDec = {};
            }
            if (xaSector >= xa->sectors) {
                ended = true;
                pending.clear();
                pendingAt = 0;
                return false;
            }
            xaL.clear();
            xaR.clear();
            xaDec.decodeSector(xa->data + size_t(xaSector++) * od::lang::kXaSectorBytes, xa->stereo, xaL, xaR);
            pending.resize(xaL.size() * 2);
            for (size_t i = 0; i < xaL.size(); ++i) {
                pending[i * 2] = float(xaL[i]) / 32768.0f;
                pending[i * 2 + 1] = float(xa->stereo ? xaR[i] : xaL[i]) / 32768.0f;
            }
            pendingAt = 0;
        }
        if (pendingAt >= pending.size()) {
            pending.resize(1152 * 2);
            std::vector<float> tmp(1152 * channels);
            drmp3_uint64 n = ok ? drmp3_read_pcm_frames_f32(&dec, 1152, tmp.data()) : 0;
            if (n == 0 && loop && ok) {
                drmp3_seek_to_pcm_frame(&dec, 0);
                n = drmp3_read_pcm_frames_f32(&dec, 1152, tmp.data());
            }
            if (n == 0) {
                ended = true;
                pending.clear();
                pendingAt = 0;
                return false;
            }
            pending.resize(size_t(n) * 2);
            for (size_t i = 0; i < n; ++i) {
                pending[i * 2] = tmp[i * channels];
                pending[i * 2 + 1] = tmp[i * channels + (channels > 1 ? 1 : 0)];
            }
            pendingAt = 0;
        }
        l = pending[pendingAt];
        r = pending[pendingAt + 1];
        pendingAt += 2;
        return true;
    }
};

Mixer::Mixer(std::filesystem::path sndDir) : sndDir_(std::move(sndDir)) {}

Mixer::~Mixer() {
    if (stream_) SDL_DestroyAudioStream(stream_);
}

bool Mixer::open() {
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("audio: SDL audio init failed: %s", SDL_GetError());
        return false;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, kRate};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Mixer::callback, this);
    if (!stream_) {
        SDL_Log("audio: no playback device: %s", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream_);
    SDL_Log("audio: device open (%d Hz stereo float mix)", kRate);
    return true;
}

void Mixer::callback(void* user, SDL_AudioStream* s, int additional, int) {
    auto* self = static_cast<Mixer*>(user);
    const int frames = additional / int(sizeof(float) * 2);
    if (frames <= 0) return;
    std::lock_guard<std::mutex> g(self->lock_);
    self->scratch_.assign(size_t(frames) * 2, 0.0f);
    self->mix(self->scratch_.data(), frames);
    SDL_PutAudioStreamData(s, self->scratch_.data(), frames * int(sizeof(float) * 2));
}

void Mixer::advance(double seconds) {
    const int frames = int(seconds * kRate);
    if (frames <= 0) return;
    std::lock_guard<std::mutex> g(lock_);
    scratch_.assign(size_t(frames) * 2, 0.0f);
    mix(scratch_.data(), frames);
}

void Mixer::setMasterGain(float g) {
    std::lock_guard<std::mutex> l(lock_);
    master_ = std::clamp(g, 0.0f, 1.0f);
}

std::array<float, 2> Mixer::takePeak() {
    std::lock_guard<std::mutex> l(lock_);
    const auto p = peak_;
    peak_ = {0.0f, 0.0f};
    return p;
}

Mixer::Stats Mixer::stats() {
    std::lock_guard<std::mutex> l(lock_);
    Stats s;
    s.buffers = int(buffers_.size());
    for (const auto& [h, b] : buffers_) s.playingBuffers += b.playing ? 1 : 0;
    s.streams = int(streams_.size());
    for (const auto& [h, st] : streams_) s.playingStreams += (st->playing && !st->ended) ? 1 : 0;
    return s;
}

void Mixer::mix(float* out, int frames) {
    for (auto& [h, b] : buffers_) {
        if (!b.playing || b.paused || b.pcm.empty()) continue;
        const double step = double(b.freq) / kRate;
        const float gl = b.gain * b.pan[0], gr = b.gain * b.pan[1];
        const int64_t n = int64_t(b.pcm.size());
        for (int i = 0; i < frames; ++i) {
            int64_t k = int64_t(b.pos);
            if (k >= n) {
                if (b.loopStart >= 0 && b.loopStart < n) {
                    b.pos = double(b.loopStart) + (b.pos - double(n));
                    k = int64_t(b.pos);
                } else {
                    b.playing = false;
                    break;
                }
            }
            const float frac = float(b.pos - double(k));
            const int64_t k1 = k + 1 < n ? k + 1 : (b.loopStart >= 0 ? b.loopStart : k);
            const float v = (float(b.pcm[size_t(k)]) * (1.0f - frac) + float(b.pcm[size_t(k1)]) * frac) / 32768.0f;
            out[i * 2] += v * gl;
            out[i * 2 + 1] += v * gr;
            b.pos += step;
        }
    }
    for (auto& [h, st] : streams_) {
        if (!st->playing || st->paused || st->ended) continue;
        const double step = double(st->rate) / kRate;
        // linear interpolation between consecutive source frames a / b (voices are 18.9 / 22.05 / 48 kHz: the
        // old loop output silence on the frames where no new source sample arrived when upsampling)
        bool ok = true;
        if (!st->primed) {
            ok = st->next(st->al, st->ar) && st->next(st->bl, st->br);
            st->primed = true;
        }
        for (int i = 0; i < frames && ok; ++i) {
            while (st->pos >= 1.0) {
                st->al = st->bl;
                st->ar = st->br;
                if (!(ok = st->next(st->bl, st->br))) break;
                st->pos -= 1.0;
            }
            if (!ok) break;
            const float f = float(st->pos);
            out[i * 2] += (st->al + (st->bl - st->al) * f) * st->gain;
            out[i * 2 + 1] += (st->ar + (st->br - st->ar) * f) * st->gain;
            st->pos += step;
        }
        if (!ok) st->playing = false;
    }
    for (int i = 0; i < frames * 2; ++i) {
        const float v = std::clamp(out[i] * master_, -1.0f, 1.0f);
        out[i] = v;
        peak_[size_t(i & 1)] = std::max(peak_[size_t(i & 1)], std::fabs(v));
    }
}

// FUN_0040a800: v == 0 -> -10000 cB, else ftol(v * (1/127) * 3000 - 3000)
float Mixer::volumeGain(int32_t v) {
    if (v <= 0) return 0.0f;
    const float cb = float(int32_t(float(float(float(v) * 0.007874015718698502f) * 3000.0f) - 3000.0f));
    return std::pow(10.0f, cb / 2000.0f);
}
// FUN_0040a860: ftol(p * (1/64) * 10000 - 10000) cB; DirectSound pan attenuates the other side
std::array<float, 2> Mixer::panGains(int32_t p) {
    const float cb = float(int32_t(float(float(float(p) * 0.015625f) * 10000.0f) - 10000.0f));
    const float l = cb > 0.0f ? -cb : 0.0f, r = cb < 0.0f ? cb : 0.0f;
    return {std::pow(10.0f, std::max(l, -10000.0f) / 2000.0f), std::pow(10.0f, std::max(r, -10000.0f) / 2000.0f)};
}

namespace {
// A RIFF chunk of a wave of the loaded files (an SFX bank's sample): its id and its size (the RIFF header is one, its
// size the file's), and the "fmt " chunk's channels and bits per sample
struct RiffChunk {
    explicit RiffChunk(uint32_t at) : data(loadedFiles().reader(at, 8)) {}
    FileDataReader data;
    uint32_t id = data.read<uint32_t>(0);
    uint32_t size = data.read<uint32_t>(4);
    uint16_t formatChannels() const { return data.read<uint16_t>(10); }
    uint16_t formatBits() const { return data.read<uint16_t>(22); }
};
} // namespace

uint32_t Mixer::createBuffer(uint32_t wav, int32_t loopOfs) {
    try {
        const RiffChunk riff(wav);
        if (riff.id != 0x46464952u /*RIFF*/) return 0;
        const uint32_t end = wav + 8 + riff.size;
        uint32_t p = wav + 12, bits = 16, channels = 1;
        Buffer b;
        while (p + 8 <= end) {
            const RiffChunk chunk(p);
            const uint32_t id = chunk.id, size = chunk.size;
            if (id == 0x20746d66u) { // "fmt "
                channels = chunk.formatChannels();
                bits = chunk.formatBits();

            } else if (id == 0x61746164u) { // "data"
                if (bits != 16 || channels != 1) return 0;
                b.pcm.resize(size / 2);
                const FileDataReader pcm = loadedFiles().reader(p + 8, uint32_t(b.pcm.size() * 2));
                for (size_t i = 0; i < b.pcm.size(); ++i) b.pcm[i] = pcm.read<int16_t>(i * 2);
                break;
            }
            p += 8 + size + (size & 1);
        }
        if (b.pcm.empty()) return 0;
        b.loopStart = loopOfs < 0 ? -1 : int64_t(loopOfs / 2);
        std::lock_guard<std::mutex> g(lock_);
        const uint32_t h = nextBuffer_++;
        buffers_[h] = std::move(b);
        return h;
    } catch (const std::exception&) {
        return 0;
    }
}

void Mixer::releaseBuffer(uint32_t h) {
    std::lock_guard<std::mutex> g(lock_);
    buffers_.erase(h);
}

bool Mixer::play(uint32_t h, uint32_t freq) {
    std::lock_guard<std::mutex> g(lock_);
    const auto it = buffers_.find(h);
    if (it == buffers_.end() || freq == 0) return false;
    it->second.freq = freq;
    it->second.pos = 0.0;
    it->second.playing = true;
    it->second.paused = false;
    // exe 0x40a6f0: started either way, but its answer is the buffer only when it has a loop part (0 for a one-shot),
    // so a one-shot leaves its voice free for the next request (a "true" kept every voice busy: the SFX died out)
    return it->second.loopStart >= 0;
}

void Mixer::setVolume(uint32_t h, int32_t v) {
    std::lock_guard<std::mutex> g(lock_);
    if (auto it = buffers_.find(h); it != buffers_.end()) it->second.gain = volumeGain(v);
}

void Mixer::setPan(uint32_t h, int32_t p) {
    std::lock_guard<std::mutex> g(lock_);
    if (auto it = buffers_.find(h); it != buffers_.end()) it->second.pan = panGains(p);
}

void Mixer::setFrequency(uint32_t h, uint32_t f) {
    std::lock_guard<std::mutex> g(lock_);
    if (auto it = buffers_.find(h); it != buffers_.end() && f != 0) it->second.freq = f;
}

void Mixer::stop(uint32_t h) {
    std::lock_guard<std::mutex> g(lock_);
    if (auto it = buffers_.find(h); it != buffers_.end()) it->second.playing = false;
}

void Mixer::pause(uint32_t h) {
    std::lock_guard<std::mutex> g(lock_);
    if (auto it = buffers_.find(h); it != buffers_.end()) it->second.paused = true;
}

void Mixer::resume(uint32_t h) {
    std::lock_guard<std::mutex> g(lock_);
    if (auto it = buffers_.find(h); it != buffers_.end()) it->second.paused = false;
}

uint32_t Mixer::createStream(std::span<const uint8_t> mp3, uint32_t loop) {
    auto s = std::make_unique<Stream>();
    s->data.assign(mp3.begin(), mp3.end());
    s->loop = loop != 0;
    if (!s->init()) SDL_Log("audio: stream of %zu bytes not decodable", mp3.size());
    else if (s->loop && !s->xa && !s->ps1) {
        if (const MusicLoop* ml = findMusicLoop(mp3)) {
            drmp3_config cfg{};
            drmp3_uint64 frames = 0;
            drmp3_int16* p = drmp3_open_memory_and_read_pcm_frames_s16(mp3.data(), mp3.size(), &cfg, &frames, nullptr);
            if (p && cfg.channels == 2 && frames == ml->frames && ml->end <= frames && ml->start < ml->end) {
                s->pcm.assign(p, p + size_t(frames) * 2);
                s->loopStart = ml->start;
                s->loopEnd = ml->end;
                SDL_Log("audio: music loop [%u, %u) of %llu frames", ml->start, ml->end, (unsigned long long)frames);
            } else {
                SDL_Log("audio: music loop entry skipped (%llu frames, %u ch, table %u)", (unsigned long long)frames,
                        unsigned(cfg.channels), unsigned(ml->frames));
            }
            if (p) drmp3_free(p, nullptr);
        }
    }
    std::lock_guard<std::mutex> g(lock_);
    const uint32_t id = nextStream_++;
    streams_[id] = std::move(s);
    return id;
}

void Mixer::destroyStream(uint32_t s) {
    std::lock_guard<std::mutex> g(lock_);
    streams_.erase(s);
}

void Mixer::streamPlay(uint32_t s) {
    std::lock_guard<std::mutex> g(lock_);
    if (auto it = streams_.find(s); it != streams_.end()) {
        it->second->rewind(); // FUN_0040ae40 plays the prepared stream from its start
        it->second->playing = true;
        it->second->paused = false;
    }
}

void Mixer::streamStop(uint32_t s) {
    std::lock_guard<std::mutex> g(lock_);
    if (auto it = streams_.find(s); it != streams_.end()) it->second->playing = false;
}

void Mixer::streamVolume(uint32_t s, int32_t v) {
    std::lock_guard<std::mutex> g(lock_);
    if (auto it = streams_.find(s); it != streams_.end()) it->second->gain = volumeGain(v);
}

void Mixer::streamPause(uint32_t s) {
    std::lock_guard<std::mutex> g(lock_);
    if (auto it = streams_.find(s); it != streams_.end()) it->second->paused = true;
}

void Mixer::streamResume(uint32_t s) {
    std::lock_guard<std::mutex> g(lock_);
    if (auto it = streams_.find(s); it != streams_.end()) it->second->paused = false;
}

bool Mixer::streamPlaying(uint32_t s) {
    std::lock_guard<std::mutex> g(lock_);
    const auto it = streams_.find(s);
    return it != streams_.end() && it->second->playing && !it->second->ended;
}

// snd\<file>: u32 offsets[n + 1] (offsets[0] = header size, offsets[n] = file size), track i = [o[i], o[i+1])
void Mixer::setVoiceDirs(std::vector<std::filesystem::path> dirs) {
    std::lock_guard<std::mutex> g(lock_);
    voiceDirs_ = std::move(dirs);
}

void Mixer::setVoiceSource(VoiceSource source, bool afterDirs) {
    std::lock_guard<std::mutex> g(lock_);
    voiceSource_ = std::move(source);
    voiceSourceAfterDirs_ = afterDirs;
}

// the language pack's voice source, its voice dirs in order (same pack layout), then the game's snd/
std::vector<uint8_t> Mixer::readVoiceTrack(const std::string& file, uint32_t track) {
    std::string lower = file;
    for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
    std::vector<std::filesystem::path> voiceDirs;
    VoiceSource source;
    bool afterDirs;
    {
        std::lock_guard<std::mutex> g(lock_);
        voiceDirs = voiceDirs_;
        source = voiceSource_;
        afterDirs = voiceSourceAfterDirs_;
    }
    if (source && !afterDirs)
        if (std::vector<uint8_t> t = source(lower, track); !t.empty()) return t;
    auto read = [&](const std::filesystem::path& path) -> std::vector<uint8_t> {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) return {};
        const auto size = f.tellg();
        if (size < std::streamoff(8)) return {};
        uint8_t header[8]{};
        f.seekg(0);
        if (!f.read(reinterpret_cast<char*>(header), 4)) return {};
        auto word = [](const uint8_t* b) {
            return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
        };
        const uint32_t first = word(header);
        if (first < 8 || first % 4 || uint64_t(first) > uint64_t(size) || track >= first / 4 - 1) return {};
        f.seekg(std::streamoff(track) * 4);
        if (!f.read(reinterpret_cast<char*>(header), 8)) return {};
        const uint32_t a = word(header), b = word(header + 4);
        if (a < first || b <= a || uint64_t(b) > uint64_t(size)) return {};
        std::vector<uint8_t> bytes(b - a);
        f.seekg(a);
        if (!f.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()))) return {};
        return bytes;
    };
    auto fromDir = [&](const std::filesystem::path& dir) {
        auto bytes = read(dir / lower);
        if (bytes.empty() && file != lower) bytes = read(dir / file);
        return bytes;
    };
    for (const auto& dir : voiceDirs)
        if (!dir.empty())
            if (auto bytes = fromDir(dir); !bytes.empty()) return bytes;
    if (source && afterDirs)
        if (auto bytes = source(lower, track); !bytes.empty()) return bytes;
    return sndDir_.empty() ? std::vector<uint8_t>() : fromDir(sndDir_);
}

} // namespace od::audio
