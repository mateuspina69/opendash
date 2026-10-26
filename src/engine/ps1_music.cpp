#include "engine/ps1_music.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>

#include "engine/dat.hpp"
#include "engine/ps1_container.hpp"
#include "engine/ps1_sfx.hpp"

namespace od {
namespace {
[[noreturn]] void fail(const char* what) { throw std::runtime_error(std::string("PS1 music: ") + what); }
void append32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (i * 8)));
}
constexpr uint32_t kRate = 44100;
constexpr size_t kPacketHeader = 16;
constexpr size_t kMaxSequence = 4 * 1024 * 1024;

struct Event {
    uint64_t frame = 0;
    uint32_t value = 0;
    uint8_t status = 0, a = 0, b = 0;
};
struct Sequence {
    std::vector<Event> events;
    Ps1MusicInfo info;
    size_t loopEvent = 0, endLoopEvent = 0;
};
Sequence readSequence(std::span<const uint8_t> s) {
    if (s.size() < 12 || rd32(s.data()) != s.size() || s.size() > kMaxSequence)
        fail("invalid sequence length");
    uint32_t tempo = rd32(s.data() + 4);
    const uint16_t ppqn = rd16(s.data() + 8);
    if (!tempo || !ppqn || ppqn > 0x7fff) fail("invalid sequence tempo / resolution");
    Sequence result;
    size_t p = 12;
    uint8_t status = 0;
    long double at = 0;
    bool ended = false, loopPending = false;
    uint64_t loopAt = 0;
    auto byte = [&]() {
        if (p >= s.size()) fail("truncated sequence event");
        return s[p++];
    };
    auto delta = [&]() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const uint8_t b = byte(); v = (v << 7) | (b & 127);
            if (!(b & 128)) return v;
        }
        fail("invalid sequence delta");
    };
    while (p < s.size()) {
        Event e;
        e.frame = uint64_t(std::llround(at));
        uint8_t b = byte();
        if (b & 128) { status = b; b = byte(); }
        e.status = status; e.a = b;
        if (status == 0xff) {
            if (b == 0x2f) {
                result.info.endFrame = e.frame; ended = true;
                result.events.push_back(e); break;
            }
            if (b != 0x51) fail("unsupported sequence meta event");
            // SEQ omits SMF's meta-event length byte.
            tempo = uint32_t(byte()) << 16; tempo |= uint32_t(byte()) << 8; tempo |= byte();
            if (!tempo) fail("zero sequence tempo");
            e.value = tempo;
        } else {
            const int cmd = status >> 4;
            if (cmd < 8 || cmd > 14 || b > 127) fail("invalid sequence status");
            if (cmd != 12 && cmd != 13) {
                e.b = byte();
                if (e.b > 127) fail("invalid sequence data byte");
            }
            if (cmd == 11 && e.a == 99) {
                loopPending = e.b == 20;
                if (loopPending) loopAt = e.frame;
                if (e.b == 30 && result.info.hasLoop) {
                    result.info.loopEndFrame = e.frame;
                    result.endLoopEvent = result.events.size();
                }
            }
            if (cmd == 11 && e.a == 6 && loopPending) {
                result.info.hasLoop = true;
                result.info.loopRepeats = e.b;
                result.info.loopStartFrame = loopAt;
                result.loopEvent = result.events.size() + 1;
                loopPending = false;
            }
        }
        result.events.push_back(e);
        const uint32_t ticks = delta();
        at += static_cast<long double>(ticks) * tempo * kRate / (static_cast<long double>(ppqn) * 1000000);
        if (at > static_cast<long double>(kRate) * 60 * 60) fail("sequence exceeds one hour");
    }
    if (!ended) fail("sequence has no end event");
    if (result.info.hasLoop && result.info.loopEndFrame <= result.info.loopStartFrame)
        fail("invalid sequence loop");
    result.info.events = uint32_t(result.events.size());
    return result;
}

struct Sample {
    std::vector<int16_t> pcm;
    size_t loop = 0;
    bool repeats = false;
};
struct Tone {
    uint32_t sample = 0;
    uint16_t adsr1 = 0, adsr2 = 0;
    uint8_t priority = 0, mode = 0, volume = 0, pan = 64, center = 60, fine = 0;
    uint8_t low = 0, high = 127, bendDown = 0, bendUp = 0;
};
struct Program {
    uint8_t volume = 127, pan = 64;
    std::vector<Tone> tones;
};
struct Bank {
    std::vector<Program> programs;
    std::vector<Sample> samples;
    uint8_t volume = 127, pan = 64;
};
Bank readBank(std::span<const uint8_t> b) {
    if (b.size() < 48 || (rd32(b.data()) != 5 && rd32(b.data()) != 14)) fail("invalid instrument bank");
    const uint32_t tab = rd32(b.data() + 0x14), bodyBytes = rd32(b.data() + 0x18);
    const uint32_t base = rd32(b.data() + 0x10);
    const uint16_t nprog = rd16(b.data() + 0x1c);
    if (!nprog || nprog > 128 || tab != uint32_t(nprog) * (8 + 16 * 20) || size_t(tab) + 48 > b.size())
        fail("invalid instrument table");
    const size_t body = ((48 + size_t(tab) + 2047) / 2048) * 2048 + 16;
    if (body > b.size() || bodyBytes > b.size() - body) fail("truncated sample body");
    Bank bank;
    bank.volume = b[0x1e]; bank.pan = b[0x1f];
    if (bank.volume > 127 || bank.pan > 127) fail("invalid bank volume / pan");
    std::map<uint32_t, uint32_t> sampleIds;
    for (uint16_t p = 0; p < nprog; ++p) {
        const size_t ph = 48 + size_t(p) * 8;
        if (b[ph] > 16 || b[ph + 1] > 127 || b[ph + 3] > 127) fail("invalid program parameters");
        Program prog; prog.volume = b[ph + 1]; prog.pan = b[ph + 3];
        for (uint8_t t = 0; t < b[ph]; ++t) {
            const uint8_t* r = b.data() + 48 + size_t(nprog) * 8 + (size_t(p) * 16 + t) * 20;
            if (rd16(r + 18) == 255) continue; // explicitly disabled VAB tone
            const uint32_t addr = rd32(r);
            uint32_t id;
            if (const auto it = sampleIds.find(addr); it != sampleIds.end()) id = it->second;
            else {
                if (addr < base || uint64_t(addr - base) + 16 > bodyBytes) fail("sample address outside bank");
                const size_t start = body + addr - base;
                size_t end = start, loop = start;
                while (end + 16 <= body + bodyBytes) {
                    if (b[end + 1] & 4) loop = end;
                    if (b[end + 1] & 1) break;
                    end += 16;
                }
                if (end + 16 > body + bodyBytes) fail("sample has no end frame");
                for (size_t a = start; a <= end; a += 16)
                    if ((b[a] >> 4) > 4) fail("invalid ADPCM filter");
                Sample sample;
                sample.pcm = decodeSpuAdpcm(b.subspan(start, end + 16 - start));
                sample.repeats = (b[end + 1] & 2) != 0;
                sample.loop = (loop - start) / 16 * 28;
                id = uint32_t(bank.samples.size()); bank.samples.push_back(std::move(sample)); sampleIds[addr] = id;
            }
            Tone tone{id, rd16(r + 14), rd16(r + 16), r[4], r[5], r[6], r[7], r[8], r[9],
                      r[10], r[11], r[12], r[13]};
            if (tone.volume > 127 || tone.pan > 127 || tone.center > 127 || tone.fine > 127 || tone.low > tone.high)
                fail("invalid tone parameters");
            prog.tones.push_back(tone);
        }
        bank.programs.push_back(std::move(prog));
    }
    return bank;
}

// SPU ADSR envelope. Parameters are the original hardware register bits, and envelope time runs
// at 44.1 kHz independently of the sample pitch. Exponential release/decay scale the step by ENVX.
// Sony's Sound Artist Tool and the hardware envelope algorithm are linked in docs/ps1_music.md.
struct Envelope {
    enum Phase { Attack, Decay, Sustain, Release, Off } phase = Attack;
    uint16_t a1 = 0, a2 = 0;
    int level = 0;
    uint32_t wait = 0;
    void release() { if (phase != Off && phase != Release) { phase = Release; wait = 0; } }
    float next() {
        if (phase == Off) return 0;
        if (wait) { --wait; return float(level) / 32768; }
        int shift, step;
        bool exponential = false, decreasing = false;
        switch (phase) {
        case Attack: shift = (a1 >> 10) & 31; step = 7 - ((a1 >> 8) & 3); exponential = (a1 & 0x8000) != 0; break;
        case Decay: shift = (a1 >> 4) & 15; step = -8; exponential = decreasing = true; break;
        case Sustain:
            shift = (a2 >> 8) & 31; decreasing = (a2 & 0x4000) != 0; exponential = (a2 & 0x8000) != 0;
            step = decreasing ? -8 + ((a2 >> 6) & 3) : 7 - ((a2 >> 6) & 3);
            // The all-ones sustain rate disables envelope changes on the SPU.
            if (((a2 >> 6) & 127) == 127) return float(level) / 32768;
            break;
        case Release: shift = a2 & 31; step = -8; exponential = (a2 & 32) != 0; decreasing = true; break;
        default: return 0;
        }
        uint32_t cycles = uint32_t(1) << std::max(0, shift - 11);
        step *= 1 << std::max(0, 11 - shift);
        if (exponential && !decreasing && level > 0x6000) cycles *= 4;
        if (exponential && decreasing) step = (step * level) >> 15;
        level = std::clamp(level + step, 0, 32767);
        wait = cycles - 1;
        if (phase == Attack && level == 32767) {
            phase = (a1 & 15) == 15 ? Sustain : Decay; wait = 0;
        }
        else if (phase == Decay && level <= std::min(32767, ((a1 & 15) + 1) * 0x800)) { phase = Sustain; wait = 0; }
        else if ((phase == Release || (phase == Sustain && decreasing)) && level == 0) phase = Off;
        return float(level) / 32768;
    }
};

// The game's actual reverb configuration: SLUS_011.40 sound reset 0x8001d6a0 selects
// record 1 at 0x800696bc (mode 3, output depth 0x4080). Mode 3 is Studio Medium;
// the 32 hardware registers at 0x80071928 + 3*68 + 4 match this preset.
// One stereo reverb unit runs at 22050 Hz, with the SPU's 39-tap input/output FIRs.
struct Reverb {
    static constexpr std::array<uint16_t, 32> regs{
        0x00b1, 0x007f, 0x70f0, 0x4fa8, 0xbce0, 0x4510, 0xbef0, 0xb4c0,
        0x5280, 0x4ec0, 0x0904, 0x076b, 0x0824, 0x065f, 0x07a2, 0x0616,
        0x076c, 0x05ed, 0x05ec, 0x042e, 0x050f, 0x0305, 0x0462, 0x02b7,
        0x042f, 0x0265, 0x0264, 0x01b2, 0x0100, 0x0080, 0x8000, 0x8000};
    static constexpr std::array<int, 39> fir{
        -1, 0, 2, 0, -10, 0, 35, 0, -103, 0, 266, 0, -616, 0, 1332, 0,
        -2960, 0, 10246, 16384, 10246, 0, -2960, 0, 1332, 0, -616, 0, 266, 0,
        -103, 0, 35, 0, -10, 0, 2, 0, -1};
    std::array<int16_t, 0x4840 / 2> memory{};
    std::array<std::array<float, 39>, 2> input{}, output{};
    size_t position = 0, firAt = 0;
    uint64_t frames = 0;
    bool used = false;
    static int clamp(int v) { return std::clamp(v, -32768, 32767); }
    static int mul(int v, int reg) { return int((int64_t(v) * int16_t(regs[size_t(reg)])) >> 15); }
    int read(int relative) const {
        const int n = int(memory.size());
        return memory[size_t((int(position) + relative % n + n) % n)];
    }
    void write(int relative, int v) {
        memory[(position + size_t(relative)) % memory.size()] = int16_t(clamp(v));
    }
    static int addr(int reg) { return int(regs[size_t(reg)]) * 4; } // address registers count 8-byte units
    std::array<float, 2> calculate(float left, float right) {
        const int lin = mul(clamp(int(left * 32768)), 30), rin = mul(clamp(int(right * 32768)), 31);
        auto reflection = [&](int inputLevel, int dest, int source) {
            const int previous = read(addr(dest) - 1);
            write(addr(dest), mul(inputLevel + mul(read(addr(source)), 7) - previous, 2) + previous);
        };
        reflection(lin, 10, 16); reflection(rin, 11, 17);
        reflection(lin, 18, 25); reflection(rin, 19, 24);
        std::array<float, 2> result{};
        for (int ch = 0; ch < 2; ++ch) {
            int v = mul(read(addr(12 + ch)), 3) + mul(read(addr(14 + ch)), 4) +
                    mul(read(addr(20 + ch)), 5) + mul(read(addr(22 + ch)), 6);
            for (int ap = 0; ap < 2; ++ap) {
                const int dest = addr(26 + ap * 2 + ch), delay = addr(ap), coef = 8 + ap;
                const int delayed = read(dest - delay);
                v = clamp(v - mul(delayed, coef)); write(dest, v);
                v = clamp(mul(v, coef) + delayed);
            }
            result[size_t(ch)] = float(v) / 32768;
        }
        position = (position + 1) % memory.size();
        return result;
    }
    std::array<float, 2> next(float l, float r) {
        if (l != 0 || r != 0) used = true;
        input[0][firAt] = l; input[1][firAt] = r;
        std::array<float, 2> wet{};
        if (!(frames & 1)) {
            std::array<float, 2> down{};
            for (size_t ch = 0; ch < 2; ++ch)
                for (size_t tap = 0; tap < fir.size(); ++tap)
                    if (fir[tap]) down[ch] += input[ch][(firAt + fir.size() - tap) % fir.size()] * float(fir[tap]) / 32768;
            wet = calculate(down[0], down[1]);
        }
        for (size_t ch = 0; ch < 2; ++ch) output[ch][firAt] = wet[ch];
        std::array<float, 2> result{};
        for (size_t ch = 0; ch < 2; ++ch)
            for (size_t tap = 0; tap < fir.size(); ++tap)
                if (fir[tap]) result[ch] += output[ch][(firAt + fir.size() - tap) % fir.size()] *
                                         float(fir[tap]) / 16384 * (float(0x4080) / 32768);
        firAt = (firAt + 1) % fir.size(); ++frames;
        return result;
    }
};
} // namespace

bool isPs1Music(std::span<const uint8_t> b) { return b.size() >= 4 && std::memcmp(b.data(), "ODSM", 4) == 0; }

std::vector<std::vector<uint8_t>> ps1MusicTracks(std::span<const uint8_t> collection,
                                               std::span<const uint8_t> bank) {
    if (collection.size() < 48 || bank.size() < 48) fail("truncated music blocks");
    const uint32_t type = rd32(collection.data()), bytes = rd32(collection.data() + 4);
    const uint32_t count = rd32(collection.data() + 0x18);
    if (type != 8 && type != 9 && type != 15 && type != 16) fail("not a sequence collection");
    if (!bytes || bytes > kMaxSequence || !count || count > 32) fail("invalid sequence collection");
    std::vector<uint8_t> raw;
    if (type == 9 || type == 16) {
        const uint32_t bits = rd32(collection.data() + 0x10);
        if (!bits || bits % 4 || size_t(bits) + 48 > collection.size()) fail("invalid sequence compression table");
        raw = ps1Decompress(collection, bits, bytes);
    } else {
        if (size_t(bytes) + 48 > collection.size()) fail("truncated raw sequence collection");
        raw.assign(collection.begin() + 48, collection.begin() + 48 + bytes);
    }
    std::vector<std::vector<uint8_t>> result;
    size_t at = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (at + 12 > raw.size()) fail("truncated sequence collection");
        const uint32_t n = rd32(raw.data() + at);
        if (n < 12 || n > raw.size() - at) fail("invalid sequence in collection");
        const auto seq = std::span<const uint8_t>(raw).subspan(at, n);
        readSequence(seq); // reject corruption before a stream is handed to the mixer
        std::vector<uint8_t> packet{'O', 'D', 'S', 'M'};
        append32(packet, 1); append32(packet, n); append32(packet, uint32_t(bank.size()));
        packet.insert(packet.end(), seq.begin(), seq.end());
        packet.insert(packet.end(), bank.begin(), bank.end());
        result.push_back(std::move(packet)); at += n;
    }
    if (at != raw.size()) fail("sequence collection has unaccounted data");
    return result;
}

struct Ps1MusicPlayer::Impl {
    struct Channel { uint8_t program = 0, volume = 127, pan = 64, expression = 127; uint16_t bend = 8192; bool hold = false; };
    struct Voice {
        const Tone* tone = nullptr;
        const Program* program = nullptr;
        const Sample* sample = nullptr;
        Envelope env;
        uint8_t channel = 0, key = 0, velocity = 0;
        bool held = false;
        uint64_t age = 0;
        double position = 0, pitch = 1;
        float left = 0, right = 0;
        bool active() const { return tone && env.phase != Envelope::Off; }
    };
    Sequence seq;
    Bank bank;
    std::array<Channel, 16> channels{};
    std::array<Voice, 24> voices{};
    Reverb reverb;
    size_t event = 0;
    uint64_t clock = 0, cycleOffset = 0, age = 0;
    uint8_t repeats = 0;
    uint32_t tailFrames = 0;
    bool loop = false, ended = false;

    Impl(std::span<const uint8_t> packet, bool repeat) : loop(repeat) {
        if (!isPs1Music(packet) || packet.size() < kPacketHeader || rd32(packet.data() + 4) != 1)
            fail("invalid stream packet");
        const uint32_t ns = rd32(packet.data() + 8), nb = rd32(packet.data() + 12);
        if (uint64_t(ns) + nb + kPacketHeader != packet.size()) fail("invalid stream packet lengths");
        seq = readSequence(packet.subspan(kPacketHeader, ns));
        bank = readBank(packet.subspan(kPacketHeader + ns, nb));
        seq.info.samples = uint32_t(bank.samples.size());
        // Validate every selected program and audible note now, avoiding surprises on the audio callback.
        std::array<uint8_t, 16> programs{};
        for (const auto& e : seq.events) {
            const int cmd = e.status >> 4, ch = e.status & 15;
            if (cmd == 12) {
                if (e.a >= bank.programs.size()) fail("sequence program outside bank");
                programs[ch] = e.a;
            }
            if (cmd == 9 && e.b && programs[ch] >= bank.programs.size()) fail("missing sequence program");
        }
        rewind();
    }
    void rewind() {
        channels = {}; voices = {}; event = 0; clock = cycleOffset = age = 0;
        repeats = seq.info.loopRepeats; ended = false; tailFrames = 0; reverb = {};
    }
    void gains(Voice& v) {
        const auto& c = channels[v.channel]; const auto& t = *v.tone;
        // VAB/channel pan offsets add around center 64, and channel/velocity/program/tone/bank
        // levels multiply. The hardware's voice register volume is linear.
        const int pan = std::clamp(int(c.pan) + int(t.pan) + int(v.program->pan) + int(bank.pan) - 3 * 64, 0, 127);
        const float gain = float(v.velocity) / 127 * (float(c.volume) / 127) * (float(c.expression) / 127) *
                           (float(t.volume) / 127) * (float(v.program->volume) / 127) * (float(bank.volume) / 127);
        v.left = gain * float(std::min(128 - pan, 64)) / 64;
        v.right = gain * float(std::min(pan, 64)) / 64;
        const double bend = (int(c.bend) - 8192) / 8192.0;
        const double semitones = int(v.key) - int(t.center) + t.fine / 128.0 + bend * (bend < 0 ? t.bendDown : t.bendUp);
        v.pitch = std::clamp(std::pow(2.0, semitones / 12), 1.0 / 4096, 16383.0 / 4096);
    }
    void keyOff(uint8_t ch, uint8_t key) {
        for (auto& v : voices) if (v.active() && v.channel == ch && v.key == key) {
            if (channels[ch].hold) v.held = true;
            else v.env.release();
        }
    }
    void keyOn(uint8_t ch, uint8_t key, uint8_t velocity) {
        const auto& program = bank.programs[channels[ch].program];
        for (const auto& t : program.tones) {
            if (key < t.low || key > t.high || !t.volume || !program.volume) continue;
            auto chosen = std::find_if(voices.begin(), voices.end(), [](const Voice& v) { return !v.active(); });
            if (chosen == voices.end()) {
                chosen = std::min_element(voices.begin(), voices.end(), [](const Voice& a, const Voice& b) {
                    if (a.tone->priority != b.tone->priority) return a.tone->priority < b.tone->priority;
                    if (a.env.level != b.env.level) return a.env.level < b.env.level;
                    return a.age < b.age;
                });
                if (chosen->tone->priority > t.priority) continue;
            }
            Voice& v = *chosen; v = {};
            v.tone = &t; v.program = &program; v.sample = &bank.samples[t.sample];
            v.channel = ch; v.key = key; v.velocity = velocity; v.age = ++age;
            v.env.a1 = t.adsr1; v.env.a2 = t.adsr2;
            gains(v);
        }
    }
    void execute(const Event& e) {
        const uint8_t ch = e.status & 15;
        auto& c = channels[ch];
        switch (e.status >> 4) {
        case 8: keyOff(ch, e.a); break;
        case 9: if (e.b) keyOn(ch, e.a, e.b); else keyOff(ch, e.a); break;
        case 11:
            switch (e.a) {
            case 7: c.volume = e.b; break;
            case 10: c.pan = e.b; break;
            case 11: c.expression = e.b; break;
            case 64:
                c.hold = e.b >= 64;
                if (!c.hold) for (auto& v : voices) if (v.active() && v.channel == ch && v.held) {
                    v.env.release(); v.held = false;
                }
                break;
            case 120: for (auto& v : voices) if (v.channel == ch) v.env.phase = Envelope::Off; break;
            case 123: for (auto& v : voices) if (v.channel == ch) v.env.release(); break;
            default: break; // NRPN loops are handled by the sequence scheduler
            }
            for (auto& v : voices) if (v.active() && v.channel == ch) gains(v);
            break;
        case 12: c.program = e.a; break;
        case 14:
            c.bend = uint16_t(e.a | e.b << 7);
            for (auto& v : voices) if (v.active() && v.channel == ch) gains(v);
            break;
        case 15:
            if (e.a == 0x2f) {
                if (loop && !seq.info.hasLoop && seq.info.endFrame) {
                    cycleOffset += seq.info.endFrame; event = 0;
                } else { ended = true; for (auto& v : voices) v.env.release(); }
            }
            break;
        default: break;
        }
    }
    // The voices' sum, brought to the level of the PC's recordings of the same tracks (their RMS over the first
    // minute of 16 tracks: the synth's is 7.9 dB louder), so the music sits under the effects and voices as there
    static constexpr float kLevel = 0.4f;
    bool next(float& l, float& r) {
        l = r = 0;
        while (!ended && event < seq.events.size() && seq.events[event].frame + cycleOffset <= clock) {
            if (loop && seq.info.hasLoop && event == seq.endLoopEvent && repeats) {
                if (repeats != 127) --repeats;
                cycleOffset += seq.info.loopEndFrame - seq.info.loopStartFrame;
                event = seq.loopEvent;
                continue;
            }
            const Event e = seq.events[event++]; execute(e);
        }
        bool active = false;
        float sendL = 0, sendR = 0;
        for (auto& v : voices) {
            if (!v.active()) continue;
            const auto& s = *v.sample;
            if (v.position >= double(s.pcm.size())) {
                if (!s.repeats) { v.env.phase = Envelope::Off; continue; }
                v.position = double(s.loop) + std::fmod(v.position - double(s.loop), double(s.pcm.size() - s.loop));
            }
            const size_t p = size_t(v.position);
            const size_t q = p + 1 < s.pcm.size() ? p + 1 : s.repeats ? s.loop : p;
            const float f = float(v.position - double(p));
            const float sample = (float(s.pcm[p]) + (float(s.pcm[q]) - s.pcm[p]) * f) / 32768 * v.env.next();
            l += sample * v.left; r += sample * v.right;
            if (v.tone->mode & 4) { sendL += sample * v.left; sendR += sample * v.right; }
            v.position += v.pitch; active = true;
        }
        if (reverb.used || sendL != 0 || sendR != 0) {
            const auto wet = reverb.next(sendL, sendR); l += wet[0]; r += wet[1];
        }
        // The Studio Medium response is below PCM16 precision after this tail window. Streams
        // retain their own tail so stopping/replacing one slot cannot corrupt another slot's music.
        if (ended && !active && reverb.used) ++tailFrames;
        ++clock;
        l = std::clamp(l * kLevel, -1.0f, 1.0f); r = std::clamp(r * kLevel, -1.0f, 1.0f);
        return !ended || active || (reverb.used && tailFrames < kRate * 3);
    }
};

Ps1MusicPlayer::Ps1MusicPlayer(std::span<const uint8_t> b, bool loop) : impl_(std::make_unique<Impl>(b, loop)) {}
Ps1MusicPlayer::~Ps1MusicPlayer() = default;
Ps1MusicPlayer::Ps1MusicPlayer(Ps1MusicPlayer&&) noexcept = default;
Ps1MusicPlayer& Ps1MusicPlayer::operator=(Ps1MusicPlayer&&) noexcept = default;
bool Ps1MusicPlayer::next(float& l, float& r) { return impl_->next(l, r); }
void Ps1MusicPlayer::rewind() { impl_->rewind(); }
const Ps1MusicInfo& Ps1MusicPlayer::info() const { return impl_->seq.info; }
} // namespace od
