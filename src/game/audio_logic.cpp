// The sound logic (see audio_logic.hpp), one routine per exe routine. The x87 steps (the pan ratio, the distance, the
// angle, the loop voices' pitch) are evaluated as the game's single-precision FPU does: each arithmetic result rounded
// to a float, integer loads exact, __ftol truncates.
#include "game/loaded_data.hpp"
#include "game/audio_logic.hpp"

#include <cmath>
#include <cstdio>
#include <stdexcept>

#include "engine/x87.hpp"
#include "game/angles.hpp"
#include "game/camera_state.hpp"
#include "game/game_state.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/vectors.hpp"

namespace od::port {

namespace {

SoundBackend* B = nullptr;
SoundBackend gNull;

SoundBackend& be() { return B ? *B : gNull; }

constexpr const char* kAudioPort = "audio port"; // the area of its port-gap errors

// ---- the state ------------------------------------------------------------------------------------------------------

// the request kinds (the dispatcher's table 0x8d3420)
enum class SoundRequestKind : uint16_t {
    None = 0,
    Sound = 1,      // a sound id: its descriptor's handler
    Descriptor = 2, // a descriptor's address in the payload
    Ret = 3,        // a bare ret (the drain stops: its answer is the kind)
    Loop = 4,       // a loop voice (a sound handler's descriptor with its loop bit)
    StopLoops = 5,  // every loop voice stops next update
    Music = 6,      // a music with its fade arguments
};
// the request's mode (how its voice is panned)
enum class SoundMode : uint8_t {
    Flat = 0,     // centred, at the descriptor's volume
    Panned = 1,   // an explicit pan / attenuation
    AtPoint = 2,  // at a point (3D pan, by the distance)
    AtListener = 3,
};
// the listener's position format (SoundContext::listenerFormat)
enum class SoundListenerFormat : uint8_t {
    Fixed = 0,   // three 16.16 dwords
    Integer = 1, // three integer dwords (<< 16)
    Short = 2,   // three shorts (<< 16)
};

// (the request queue: SoundQueue, audio_logic.hpp)


// A bank's data: its header (the sample table's offset), its entries (8 bytes) and its sample table (8 bytes each)
struct BankHeader : DataRecord {
    static constexpr uint32_t kBytes = 0x4;
    uint32_t sampleTable = 0; // +0x0
    template <class R> static BankHeader load(const R& r) {
        BankHeader v;
        v.sampleTable = r.template read<uint32_t>(0x0);
        return v;
    }
};
struct BankEntry : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    static constexpr uint32_t kBufferOffset = 0x4; // where the bank load writes the backend's buffer
    uint8_t sample = 0; // +0x0: its sample (0: none)
    uint8_t volume = 0; // +0x1
    uint16_t frequency = 0; // +0x2
    uint32_t buffer = 0; // +0x4: the backend's buffer
    template <class R> static BankEntry load(const R& r) {
        BankEntry v;
        v.sample = r.template read<uint8_t>(0x0);
        v.volume = r.template read<uint8_t>(0x1);
        v.frequency = r.template read<uint16_t>(0x2);
        v.buffer = r.template read<uint32_t>(0x4);
        return v;
    }
};
struct SampleEntry : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    uint32_t offset = 0; // +0x0: the RIFF / WAVE's offset from the sample table (0: none)
    int32_t loopOffset = 0; // +0x4
    template <class R> static SampleEntry load(const R& r) {
        SampleEntry v;
        v.offset = r.template read<uint32_t>(0x0);
        v.loopOffset = r.template read<int32_t>(0x4);
        return v;
    }
};
constexpr uint32_t kBankEntrySize = 8, kBankHeaderSize = 4;

// A sound's descriptor (exe data, 8 bytes): +0 its handler (>> 4), its extra voices (>> 2 & 3), kDescriptorLoop,
// kDescriptorFlat; +1 the bank slot / id; +2 the entry's high part; +3 the priority (& 7), kDescriptorNoSteal, the
// entry's low part / the music channel (>> 4); +4 the first voice (& 0x1f) or kDescriptorAnyVoice / the MP3 slot
struct SoundDescriptor : DataRecord {
    static constexpr uint32_t kBytes = 0x5;
    uint8_t flags = 0; // +0x0
    uint8_t bank = 0; // +0x1
    uint8_t entryHigh = 0; // +0x2
    uint8_t mix = 0; // +0x3
    uint8_t voice = 0; // +0x4
    template <class R> static SoundDescriptor load(const R& r) {
        SoundDescriptor v;
        v.flags = r.template read<uint8_t>(0x0);
        v.bank = r.template read<uint8_t>(0x1);
        v.entryHigh = r.template read<uint8_t>(0x2);
        v.mix = r.template read<uint8_t>(0x3);
        v.voice = r.template read<uint8_t>(0x4);
        return v;
    }
};
constexpr uint8_t kDescriptorLoop = 0x01, kDescriptorFlat = 0x02;
constexpr uint8_t kDescriptorNoSteal = 0x08, kDescriptorAnyVoice = 0x20;
constexpr uint8_t kDescriptorPriority = 0x07;
constexpr uint8_t kDescriptorHandlerShift = 4;     // flags >> this: the handler (SoundHandler)
constexpr uint8_t kDescriptorHandlerLowBits = 0x30; // the handler's two low bits in place
constexpr uint8_t kDescriptorMusic = 0x20; // flags & kDescriptorHandlerLowBits at least this: a music
enum class SoundHandler : uint8_t { BankSlot = 0, BankId = 1, Music = 3 };
// the descriptors (exe tables): the sounds 0..0x52, the musics 0x80.., one shared for 0x53..0x7f
constexpr int32_t kLastSoundId = 0x52, kLastSharedId = 0x7f;

// A voice's entry (4 bytes of the voice tables): its file and track
struct VoiceEntry : DataRecord {
    static constexpr uint32_t kBytes = 0x4;
    uint16_t file = 0; // +0x0
    uint16_t track = 0; // +0x2
    template <class R> static VoiceEntry load(const R& r) {
        VoiceEntry v;
        v.file = r.template read<uint16_t>(0x0);
        v.track = r.template read<uint16_t>(0x2);
        return v;
    }
};
constexpr uint32_t kSecondVoiceTable = 0x8000, kVoiceIndexMask = 0x7fff, kBuiltInVoiceFirst = 0x7f00;
constexpr size_t kMaxVoiceFileName = 32;
// the voice files' names by file (exe data 0x91be40: pointers to C strings of at most 32 characters; null: none)
struct VoiceFileNames : DataRecord {
    static constexpr size_t kFiles = 0x100;
    static constexpr uint32_t kBytes = kFiles * 4;
    std::array<std::string, kFiles> names;
    std::array<bool, kFiles> present{};
    static VoiceFileNames load(const TableReader& r) {
        VoiceFileNames t;
        for (size_t i = 0; i < kFiles; ++i) {
            t.present[i] = r.u32(i * 4) != 0;
            t.names[i] = r.followString(i * 4, kMaxVoiceFileName);
        }
        return t;
    }
};
const DataTable<VoiceFileNames> voiceFileNames{TableId::VoiceFileNames};
constexpr int32_t kVoiceVolumeBase = 100;

// the stereo pan law's constants (exe 0x5d4960 / 0x5d4b80)
constexpr int32_t kPanSide = 0x2a3c, kPanSpread = 0x15c3, kPanVolumeOne = 0x3fff, kHearingRange = 15000;
constexpr int32_t kStealDistance = 0x9c4;

float f32(double v) { return float(v); }
// __ftol of an x87 value (truncation; out of range = integer indefinite)
int32_t ftol(double v) {
    if (!(v > -2147483649.0 && v < 2147483648.0)) return int32_t(0x80000000u);
    return int32_t(v);
}
int32_t toFixed(int16_t v) { return int32_t(uint32_t(int32_t(v)) << 16); }

// ---- small helpers ---------------------------------------------------------------------------------------------

// exe 0x5d4930: the descriptor of a sound id
SoundDescriptor descriptor(uint32_t id) {
    if (int32_t(id) > kLastSharedId) return dataRecord<SoundDescriptor>(stateAddress(TableId::AudioMusicDescriptors) + id * 8, SoundDescriptor::kBytes);
    if (int32_t(id) > kLastSoundId) return dataRecord<SoundDescriptor>(stateAddress(TableId::AudioUnusedDescriptor), SoundDescriptor::kBytes);
    return dataRecord<SoundDescriptor>(stateAddress(TableId::AudioSoundDescriptors) + id * 8, SoundDescriptor::kBytes);
}

// exe 0x5d5220(kind): a free queue entry (the queue full: -1), its kind, the count + 1, the queue pending
int queueAlloc(SoundRequestKind kind) {
    SoundQueue& q = soundQueue();
    const uint32_t n = q.count;
    if (n == kQueueEntries) return -1;
    auto&& e = q.entries[n];
    e.kind = uint16_t(kind);
    q.count = q.count + 1;
    q.flags |= kQueuePending;
    return int(n);
}
// a request's entry, the queue's oldest dropped when it is full (the forced requests)
int queueAllocForced(SoundRequestKind kind) {
    int i = queueAlloc(kind);
    if (i < 0) {
        soundQueuePopFront();
        i = queueAlloc(kind);
    }
    return i;
}

// the listener's position as its format reads it: three dwords, or three shorts (the dwords' halves, in order)
struct ListenerWords {
    int32_t words[3];
    int16_t shortAt(int i) const { return int16_t(uint16_t(uint32_t(words[i / 2]) >> (16 * (i & 1)))); }
};
ListenerWords listenerWords(const ListenerRef& r) {
    if (r.kind == ListenerRef::Kind::CameraEye) {
        const CameraVec& e = cameraState().eye;
        return {{e.x, e.y, e.z}};
    }
    const Fixed16Vec3& p = player().pos;
    return {{p.x, p.y, p.z}};
}
// exe 0x5d4e20(ctx, out): the listener's position as three 16.16 ints (by its format)
void listener(int32_t out[3]) {
    const ListenerRef& p = soundListener().position;
    if (p.kind == ListenerRef::Kind::None) {
        out[0] = out[1] = out[2] = 0;
        return;
    }
    const ListenerWords pt = listenerWords(p);
    switch (SoundListenerFormat(soundContext().listenerFormat)) {
    case SoundListenerFormat::Fixed:
        out[0] = pt.words[0];
        out[1] = pt.words[1];
        out[2] = pt.words[2];
        break;
    case SoundListenerFormat::Integer:
        out[0] = int32_t(uint32_t(pt.words[0]) << 16);
        out[1] = int32_t(uint32_t(pt.words[1]) << 16);
        out[2] = int32_t(uint32_t(pt.words[2]) << 16);
        break;
    case SoundListenerFormat::Short:
        out[0] = toFixed(pt.shortAt(0));
        out[1] = toFixed(pt.shortAt(1));
        out[2] = toFixed(pt.shortAt(2));
        break;
    default: break; // the original leaves the caller's buffer as it was
    }
}
// the listener's facing yaw (the rotation's second short)
uint16_t listenerYaw() {
    const ListenerRef& r = soundListener().rotation;
    if (r.kind == ListenerRef::Kind::CameraAngles) return uint16_t(cameraState().viewAngles.y);
    return uint16_t(player().rotation.y);
}

// exe 0x5d4db0(x, y, z): |src - listener| on the 16.16 high halves (exe 0x40b980: fild, fsqrt, __ftol), -1 beyond the
// hearing range
int32_t listenerDistance(int32_t x, int32_t y, int32_t z) {
    int32_t l[3] = {0, 0, 0};
    listener(l);
    const int32_t dx = int32_t(uint32_t(x) - uint32_t(l[0])) >> 16;
    const int32_t dy = int32_t(uint32_t(y) - uint32_t(l[1])) >> 16;
    const int32_t dz = int32_t(uint32_t(z) - uint32_t(l[2])) >> 16;
    const int32_t n = int32_t(uint32_t(dx) * uint32_t(dx) + uint32_t(dy) * uint32_t(dy) + uint32_t(dz) * uint32_t(dz));
    const int32_t d = n < 0 ? int32_t(0x80000000u) : ftol(double(f32(std::sqrt(double(n)))));
    return d > kHearingRange ? -1 : d;
}

// exe 0x5e3bc0(a, b): atan2(b.x - a.x, b.z - a.z) as floats, exe 0x40b960 (fpatan, * 2048.0, * the scale, __ftol),
// & 0xfff
uint32_t angleTo(const int32_t a[3], const int32_t b[3]) {
    const float dz = float(int32_t(uint32_t(b[2]) - uint32_t(a[2])));
    const float dx = float(int32_t(uint32_t(b[0]) - uint32_t(a[0])));
    return uint32_t(x87::atanAngle(atanScale(), dx, dz)) & 0xfff; // engine/x87
}

// exe 0x5d4af0(voice, l, r): the pan from the two sides' volumes, the volume the louder one's, by the SFX master
void setVoicePanVolume(SfxVoice& v, int32_t l, int32_t r) {
    if (l == 0 && r == 0) {
        v.pan = kCentrePan;
        v.volume = 0;
    } else if (l >= r) {
        v.volume = uint8_t(l);
        v.pan = uint8_t(ftol(double(f32(double(f32(double(r) / double(l))) * 64.0))));
    } else {
        v.volume = uint8_t(r);
        v.pan = uint8_t(ftol(double(f32(double(f32(double(f32(double(l) / double(r))) * 64.0)) + 63.0))));
    }
    v.volume = uint8_t(int32_t(v.volume) * int32_t(soundMasters().sfx) / kSoundMasterFull);
}

// exe 0x5d4960(voice, vol, pan -127..127, atten 0..127): the explicit pan (mono: both sides at the volume)
void explicitPan(SfxVoice& v, int32_t vol, int32_t p, int32_t atten) {
    int32_t l = vol, r = vol;
    if (gameOptions().mono == 0) {
        if (p < 1) {
            l = (kPanSide - (p * kPanSpread) / 0x7f) * vol / kPanVolumeOne;
            r = (((p * 0x385 + 0x1befb) * 0xc) / 0x7f) * vol / kPanVolumeOne;
        } else {
            l = (((0x7f - p) * kPanSide) / 0x7f) * vol / kPanVolumeOne;
            r = ((p * kPanSpread) / 0x7f + kPanSide) * vol / kPanVolumeOne;
        }
    }
    setVoicePanVolume(v, ((0x7f - atten) * l) / 0x7f, ((0x7f - atten) * r) / 0x7f);
}

// x86 idiom (x + (x >> 31 & 63)) >> 6: division by 64 rounding toward zero
int32_t div64(int32_t x) { return (x + ((x >> 31) & 0x3f)) >> 6; }

// exe 0x5d4b80(voice, vol, src (3 x 16.16), dist): the 3D pan and volume (by the angle from the listener's facing and
// the distance; mono: the distance only)
void pan3d(SfxVoice& v, int32_t vol, const int32_t src[3], int32_t dist) {
    int32_t a = 0, b = 0; // exe 0x5d4af0(voice, a, b)
    if (dist >= 0) {
        if (gameOptions().mono != 0) {
            a = b = vol;
            if (dist != 0) {
                if (uint32_t(dist) < uint32_t(kHearingRange)) a = b = (kHearingRange - dist) * vol / kHearingRange;
                else a = b = 0;
            }
        } else {
            a = b = (vol * kPanSide) / kPanVolumeOne;
            int32_t l[3] = {0, 0, 0};
            listener(l);
            if (dist != 0) {
                const uint32_t ang = (angleTo(l, src) - listenerYaw()) & 0xfff;
                const int32_t s = (int32_t(trig64Tables().sine[ang >> 6]) << 6) >> 12;
                if (int32_t(ang) > 0x800) {
                    a = div64((s * 0x385 + 0xe140) * 0xc) * vol / kPanVolumeOne;
                    b = (kPanSide - div64(s * kPanSpread)) * vol / kPanVolumeOne;
                } else {
                    b = div64((0x40 - s) * kPanSide) * vol / kPanVolumeOne;
                    a = (div64(s * kPanSpread) + kPanSide) * vol / kPanVolumeOne;
                }
                if (uint32_t(dist) < uint32_t(kHearingRange)) {
                    a = (kHearingRange - dist) * a / kHearingRange;
                    b = (kHearingRange - dist) * b / kHearingRange;
                } else {
                    a = b = 0;
                }
            }
        }
    }
    setVoicePanVolume(v, a, b);
}

// ---- voices ------------------------------------------------------------------------------------------------------

// The request being started (the exe's stack struct): its voice, bank slot, entry, priority, mode and distance
struct Start {
    uint8_t voice, slot, entry, prio, mode;
    int32_t dist;
};

// exe 0x5d46d0(start, mix): 0 = the voice can take the request (stopped if it played), -1 = busy
int voiceSteal(Start& s, uint8_t mix) {
    auto&& v = soundVoices().voices[s.voice];
    if (v.loop != 0) return -1;
    if (v.playing == 0) return 0;
    const uint32_t old = v.priority, prio = mix & kDescriptorPriority;
    if (old < prio) {
        stopSfxVoice(s.voice);
        return 0;
    }
    if (old != prio || (mix & kDescriptorNoSteal)) return -1;
    if (s.mode >= uint8_t(SoundMode::AtPoint)) {
        if (s.dist - int32_t(v.distance) > kStealDistance) return -1;
        v.distance = int16_t(s.dist);
    }
    stopSfxVoice(s.voice);
    return 0;
}

// exe 0x5d4760(ctx, start): the voice started on its bank entry (a loop request: its loop voice sounding, silent)
void startVoice(const Start& s) {
    const SfxBank& bank = sfxBanks().banks[s.slot];
    auto&& v = soundVoices().voices[s.voice];
    if (dataRecord<BankEntry>(bank.entries + uint32_t(s.entry) * kBankEntrySize, BankEntry::kBytes).buffer == 0) return;
    v.active = 1;
    v.entry = s.entry;
    v.priority = s.prio;
    const BankEntry e = dataRecord<BankEntry>(bank.entries + uint32_t(v.entry) * kBankEntrySize, BankEntry::kBytes);
    v.frequency = e.frequency;
    v.buffer = e.buffer;
    const uint8_t vol = e.volume;
    SoundContext& ctx = soundContext();
    if (ctx.request.kind == uint16_t(SoundRequestKind::Loop)) {
        LoopVoices& loops = loopVoices();
        const uint32_t k = uint32_t(s.voice) - kFirstLoopVoice;
        loops.sounding |= 0x80000000u >> (k & 0x1f);
        auto&& lv = loops.voices[k];
        lv.keep = 1;
        lv.volume = int8_t(vol);
        lv.mode = int8_t(s.mode);
        v.loop = 1;
        v.playing = be().play(v.buffer, v.frequency) ? 1 : 0;
        be().setVolume(v.buffer, 0);
        return;
    }
    switch (SoundMode(s.mode)) {
    case SoundMode::Flat:
        v.volume = vol;
        v.pan = kCentrePan;
        break;
    case SoundMode::Panned: explicitPan(v, vol, ctx.request.x, ctx.request.z); break;
    case SoundMode::AtPoint: {
        const int32_t src[3] = {toFixed(ctx.request.x), toFixed(ctx.request.y), toFixed(ctx.request.z)};
        pan3d(v, vol, src, s.dist);
        break;
    }
    case SoundMode::AtListener: {
        int32_t l[3] = {0, 0, 0};
        listener(l);
        pan3d(v, vol, l, 0);
        break;
    }
    default: break;
    }
    be().setVolume(v.buffer, int32_t(v.volume) * int32_t(soundMasters().sfx) >> 7);
    be().setPan(v.buffer, v.pan);
    v.loop = 0;
    v.playing = be().play(v.buffer, v.frequency) ? 1 : 0;
}

// exe 0x5d3f20(ctx, desc, slot): the request's voices (its first voice and the extra ones, stolen if they may be; or
// the free voices from 16)
int startSfxRequest(const SoundDescriptor& d, uint8_t slot) {
    const SoundContext& ctx = soundContext();
    Start s{};
    s.dist = -1;
    s.prio = d.mix & kDescriptorPriority;
    s.slot = slot;
    s.entry = uint8_t(d.entryHigh * 16 + (d.mix >> 4));
    if (d.flags & kDescriptorFlat) {
        s.mode = uint8_t(SoundMode::Flat);
    } else {
        s.mode = ctx.request.mode;
        if (s.mode == uint8_t(SoundMode::AtPoint)) {
            s.dist = listenerDistance(toFixed(ctx.request.x), toFixed(ctx.request.y), toFixed(ctx.request.z));
            if (ctx.request.kind != uint16_t(SoundRequestKind::Loop) && s.dist < 0) return 0;
        } else if (s.mode == uint8_t(SoundMode::AtListener)) {
            s.dist = 0;
        }
    }
    uint32_t n = ((d.flags >> 2) & 3) + 1;
    if (!(d.voice & kDescriptorAnyVoice)) {
        s.voice = d.voice & 0x1f;
        for (; n; --n) {
            if (voiceSteal(s, d.mix) == 0) startVoice(s);
            ++s.entry;
            ++s.voice;
        }
        return 0;
    }
    for (uint32_t voice = kFirstLoopVoice; voice < kVoices; ++voice) {
        if (n == 0) return 0;
        if (soundVoices().voices[voice].playing == 0) {
            s.voice = uint8_t(voice);
            startVoice(s);
            ++s.entry;
            --n;
        }
    }
    return 0;
}

// exe 0x5d3ed0 (handler 0): an SFX by its bank slot; the stage's banks 1 / 2 muted by its flags
int handlerSfxSlot(const SoundDescriptor& d) {
    const uint8_t slot = d.bank;
    if (sfxBanks().banks[slot].bankId == kNoBank) return 0;
    if ((slot == 1 || slot == 2) && (soundContext().stageFlags & kSoundStageBanksMuted)) return 0;
    return startSfxRequest(d, slot);
}
// exe 0x5d4080 (handler 1): an SFX by its bank id (searched in the slots)
int handlerSfxBank(const SoundDescriptor& d) {
    uint32_t i = 0;
    for (; i < kBanks; ++i)
        if (sfxBanks().banks[i].bankId == d.bank) break;
    if (i == kBanks) return 0;
    return startSfxRequest(d, uint8_t(i));
}

// exe 0x40dc90 / 0x40dd00(ch, mp3Slot, ...): the slot's stream on the channel, faded in
void startMusicChannel(uint32_t ch, uint32_t slot) {
    MusicState& m = musicState();
    auto&& c = m.channels[ch];
    const Mp3Slot& mp3 = mp3Slots().slots[slot];
    c.stream = mp3.stream;
    c.loop = uint8_t(mp3.loop);
    const uint32_t bit = 1u << (ch & 0x1f);
    m.playing |= bit;
    be().streamVolume(c.stream, 0);
    be().streamPlay(c.stream);
    soundFadeIn(bit, kMusicFadeInStep, 0);
    m.currentStream = c.stream;
}

// exe 0x5d40c0 (handler 3): a music on its channel (a lower priority one stopped); a music request's fade arguments
// (y: its fade entry, the flags: 1 full volume, 2 a held dip)
int handlerMusic(const SoundDescriptor& d) {
    MusicState& m = musicState();
    SoundContext& ctx = soundContext();
    const uint32_t ch = d.mix >> 4;
    const uint32_t prio = d.mix & kDescriptorPriority;
    auto&& c = m.channels[ch];
    const uint32_t idx = c.index;
    if ((1u << (idx & 0x1f)) & m.playing) {
        const uint32_t old = c.priority;
        if (old > prio) return 0;
        if (old == prio && (d.mix & kDescriptorNoSteal)) return 0;
        stopMusicChannel(idx);
    }
    soundFades().fades[ch].active = 0;
    m.done |= 1u << (ch & 0x1f);
    c.priority = uint8_t(prio);
    if (ctx.request.kind == uint16_t(SoundRequestKind::Music)) {
        const int32_t k = ctx.request.y;
        const uint32_t flags = ctx.request.payloadHigh();
        auto&& f = soundFades().fades[size_t(uint32_t(k))];
        ctx.fadeVolumes[size_t(uint32_t(k))] = (flags & kMusicRequestFullVolume) ? kSoundVolumeFull : 0;
        if (flags & kMusicRequestHeldDip) {
            f.active = 1;
            f.type = uint8_t(SoundFadeType::Dip);
            f.state = kDipHold;
            m.done &= ~(uint32_t(k) << 8 | uint32_t(k));
            m.done |= 0x20u << (f.channel & 0x1f);
        }
    }
    startMusicChannel(c.index, d.voice);
    ctx.fadeVolumes[c.index] = 0;
    return 0;
}

int callHandler(const SoundDescriptor& d) {
    switch (SoundHandler(d.flags >> 4)) {
    case SoundHandler::BankSlot: return handlerSfxSlot(d);
    case SoundHandler::BankId: return handlerSfxBank(d);
    case SoundHandler::Music: return handlerMusic(d);
    default: notPorted(kAudioPort, "sound descriptor handler (null entry of 0x8d3448)");
    }
}

// the request kinds (table 0x8d3420); the handler's answer (non-zero stops the drain for this frame)
int dispatch(uint32_t kind) {
    const SoundContext& ctx = soundContext();
    switch (SoundRequestKind(kind)) {
    case SoundRequestKind::None: return 0; // exe 0x444070
    case SoundRequestKind::Sound:          // exe 0x5d3e10
    case SoundRequestKind::Music: return callHandler(descriptor(ctx.request.id));
    case SoundRequestKind::Descriptor: callHandler(dataRecord<SoundDescriptor>(ctx.request.payloadLow(), SoundDescriptor::kBytes)); return 0; // exe 0x5d3e40
    case SoundRequestKind::Ret: return int(kind);                                          // exe 0x52ef00: a bare ret
    case SoundRequestKind::Loop: {                                                          // exe 0x5d3e60
        const SoundDescriptor d = descriptor(ctx.request.id);
        // an SFX handler's loop (the exe's flags & 0xf0 <= 0x10)
        if ((d.flags >> kDescriptorHandlerShift) <= uint8_t(SoundHandler::BankId) && (d.flags & kDescriptorLoop))
            return callHandler(d);
        return 0;
    }
    case SoundRequestKind::StopLoops: { // exe 0x5d3ea0: every loop voice stops at the next update
        LoopVoices& loops = loopVoices();
        size_t i = 0;
        for (uint32_t m = loops.sounding; m; m <<= 1, ++i) {
            if (int32_t(m) >= 0) continue;
            auto&& lv = loops.voices[i];
            lv.keep = 0;
        }
        soundLoopState().holdLoops = 0;
        return 0;
    }
    default: notPorted(kAudioPort, "sound request kind > 6");
    }
}

// exe 0x5d42c0(ch, v): a fade entry's volume applied (the music channels' streams; the SFX group's voices 15.., a
// silent one stopped)
void applyChannelVolume(uint32_t ch, int32_t v) {
    if (ch < kMusicChannels) {
        be().streamVolume(musicState().channels[ch].stream, (int32_t(soundMasters().bgm) * v) >> 14);
        return;
    }
    if (ch != kSfxGroupFade) return;
    for (uint32_t voice = kFirstStopVoice; voice < kVoices; ++voice) {
        const SfxVoice& sv = soundVoices().voices[voice];
        if (sv.buffer == 0 || sv.loop != 0) continue;
        const int32_t vol = (int32_t(soundMasters().sfx) * v) >> 14;
        if (vol == 0) stopSfxVoice(voice);
        else be().setVolume(sv.buffer, vol);
    }
}

// exe 0x5d4500(ctx): the loop voices: without their keep-alive (and without the hold) stopped; else their pitch,
// their pan / volume by their mode, the voice fade's and the master's volume; the keep-alive cleared for the next frame
void updateLoopVoices() {
    LoopVoices& loops = loopVoices();
    SoundContext& ctx = soundContext();
    const uint32_t keep = soundLoopState().holdLoops;
    uint32_t mask = loops.sounding;
    if (mask == 0) return;
    for (size_t i = 0; mask; mask <<= 1, ++i) {
        if (int32_t(mask) >= 0) continue;
        auto&& lv = loops.voices[i];
        auto&& v = soundVoices().voices[kFirstLoopVoice + i];
        if ((uint32_t(lv.keep) | uint32_t(soundLoopState().holdLoops)) == 0) {
            const uint32_t bit = 0x80000000u >> ((lv.voice - kFirstLoopVoice) & 0x1f);
            loops.sounding ^= bit;
            loops.allocated ^= bit;
            stopSfxVoice(lv.voice);
            continue;
        }
        const int32_t pitch = lv.pitch;
        uint32_t freq = v.frequency;
        if (pitch != 0) {
            const double k = pitch > 0 ? 0.007874015718698502 : 0.00390625; // [0x831838] / [0x831558] (floats)
            const float r = f32(double(f32(double(pitch) * k)) + 1.0);
            freq = uint32_t(ftol(double(f32(double(r) * double(int32_t(freq))))));
        }
        const int32_t src[3] = {toFixed(lv.point.x), toFixed(lv.point.y), toFixed(lv.point.z)};
        const int32_t dist = listenerDistance(src[0], src[1], src[2]);
        switch (SoundMode(uint8_t(lv.mode))) {
        case SoundMode::Panned: explicitPan(v, lv.volume, lv.point.x, lv.point.z); break;
        case SoundMode::AtPoint: pan3d(v, lv.volume, src, dist); break;
        case SoundMode::AtListener: {
            int32_t l[3] = {0, 0, 0};
            listener(l);
            pan3d(v, lv.volume, l, 0);
            break;
        }
        default: break;
        }
        if (soundFades().fades[kVoiceClipFadeEntry].active != 0)
            v.volume = uint8_t((int32_t(ctx.fadeVolumes[kVoiceClipFadeEntry]) * int32_t(v.volume)) >> 14);
        v.volume = uint8_t((int32_t(soundMasters().sfx) * int32_t(v.volume)) >> 7);
        be().setFrequency(v.buffer, freq);
        be().setPan(v.buffer, v.pan);
        be().setVolume(v.buffer, (int32_t(v.volume) * int32_t(soundMasters().sfx)) >> 7);
        lv.keep = uint8_t(keep);
    }
}

// the fade types' steps (table 0x8d343c)
// a fade's delay counted down (its state moves to `next` at 0); true while it counts
bool fadeDelay(SoundFade& f, uint8_t next) {
    const int16_t d = f.delay;
    f.delay = int16_t(d - 1);
    if (d == 0) f.state = next;
    return true;
}
void fadeOutStep(SoundFade& f) { // exe 0x5d4210
    SoundContext& ctx = soundContext();
    const uint32_t ch = f.channel;
    auto&& vp = ctx.fadeVolumes[ch];
    if (f.state == 0) {
        fadeDelay(f, 1);
        return;
    }
    vp = int16_t(uint16_t(vp) + uint16_t(f.step));
    if (int32_t(vp) <= -int32_t(f.step)) {
        vp = 0;
        f.active = 0;
        if (f.channel < kMusicChannels) {
            stopMusicChannel(f.channel);
        } else {
            dispatch(uint32_t(SoundRequestKind::StopLoops));
            updateLoopVoices();
            applyChannelVolume(f.channel, 0);
        }
        musicState().done |= 1u << (f.channel & 0x1f);
        return;
    }
    applyChannelVolume(f.channel, vp);
}
void fadeInStep(SoundFade& f) { // exe 0x5d4350
    SoundContext& ctx = soundContext();
    MusicState& m = musicState();
    auto&& vp = ctx.fadeVolumes[f.channel];
    if (f.state == 0) {
        fadeDelay(f, 1);
        return;
    }
    vp = int16_t(uint16_t(vp) + uint16_t(f.step));
    if (int32_t(vp) >= kSoundVolumeFull - int32_t(f.step)) {
        vp = kSoundVolumeFull;
        f.active = 0;
        m.done |= 1u << (f.channel & 0x1f);
    }
    be().streamVolume(m.channels[f.channel].stream,
                      int16_t(uint16_t(uint32_t((int32_t(vp) * int32_t(soundMasters().bgm)) >> 14))));
}
void fadeDipStep(SoundFade& f) { // exe 0x5d43e0
    SoundContext& ctx = soundContext();
    MusicState& m = musicState();
    auto&& vp = ctx.fadeVolumes[f.channel];
    switch (f.state) {
    case kDipDelay: fadeDelay(f, kDipDown); return;
    case kDipDown:
        vp = int16_t(uint16_t(vp) - uint16_t(f.step));
        if (int32_t(vp) > int32_t(f.step) + int32_t(f.target)) break;
        vp = f.target;
        f.state = kDipDown;
        m.done |= 0x20u << (f.channel & 0x1f);
        break;
    case kDipRestoreDelay: fadeDelay(f, kDipUp); return;
    case kDipUp:
        vp = int16_t(uint16_t(vp) + uint16_t(f.step));
        if (int32_t(vp) < kSoundVolumeFull - int32_t(f.step)) break;
        vp = kSoundVolumeFull;
        f.active = 0;
        m.done |= 1u << (f.channel & 0x1f);
        break;
    default: return; // kDipHold
    }
    applyChannelVolume(f.channel, vp);
}

// exe 0x6011a0: the voice clip's fade-out step
void voiceClipUpdate() {
    VoiceClipState& c = voiceClip();
    if (c.fading == 0 || c.stream == kNoStream) return;
    int32_t v = c.volume - c.step;
    c.volume = v;
    if (v <= 0) {
        c.volume = 0;
        c.fading = 0;
        be().streamStop(c.stream);
        v = 0;
    }
    be().streamVolume(c.stream, v);
}

} // namespace

// ---- backend plumbing ------------------------------------------------------------------------------------------

void setSoundBackend(SoundBackend* b) { B = b; }
SoundBackend* soundBackend() { return B; }

uint32_t RecordingSoundBackend::createBuffer(uint32_t wav, int32_t loopOfs) {
    return next ? next->createBuffer(wav, loopOfs) : 1;
}
void RecordingSoundBackend::releaseBuffer(uint32_t h) {
    calls.push_back({SoundCall::Release, h, 0});
    if (next) next->releaseBuffer(h);
}
bool RecordingSoundBackend::play(uint32_t h, uint32_t freq) {
    calls.push_back({SoundCall::Play, h, freq});
    if (next) return next->play(h, freq); // the mixer's answer: the voice stays "playing" only for a looped buffer
    return h != 0 && freq != 0;
}
void RecordingSoundBackend::setVolume(uint32_t h, int32_t v) {
    calls.push_back({SoundCall::Volume, h, uint32_t(v)});
    if (next) next->setVolume(h, v);
}
void RecordingSoundBackend::setPan(uint32_t h, int32_t p) {
    calls.push_back({SoundCall::Pan, h, uint32_t(p)});
    if (next) next->setPan(h, p);
}
void RecordingSoundBackend::setFrequency(uint32_t h, uint32_t f) {
    calls.push_back({SoundCall::Frequency, h, f});
    if (next) next->setFrequency(h, f);
}
void RecordingSoundBackend::stop(uint32_t h) {
    calls.push_back({SoundCall::Stop, h, 0});
    if (next) next->stop(h);
}
void RecordingSoundBackend::pause(uint32_t h) {
    calls.push_back({SoundCall::Pause, h, 0});
    if (next) next->pause(h);
}
void RecordingSoundBackend::resume(uint32_t h) {
    calls.push_back({SoundCall::Resume, h, 0});
    if (next) next->resume(h);
}
uint32_t RecordingSoundBackend::createStream(std::span<const uint8_t> mp3, uint32_t loop) {
    return next ? next->createStream(mp3, loop) : 0;
}
void RecordingSoundBackend::destroyStream(uint32_t s) {
    calls.push_back({SoundCall::StreamDestroy, s, 0});
    if (next) next->destroyStream(s);
}
void RecordingSoundBackend::streamPlay(uint32_t s) {
    calls.push_back({SoundCall::StreamPlay, s, 0});
    if (next) next->streamPlay(s);
}
void RecordingSoundBackend::streamStop(uint32_t s) {
    calls.push_back({SoundCall::StreamStop, s, 0});
    if (next) next->streamStop(s);
}
void RecordingSoundBackend::streamVolume(uint32_t s, int32_t v) {
    calls.push_back({SoundCall::StreamVolume, s, uint32_t(v)});
    if (next) next->streamVolume(s, v);
}
void RecordingSoundBackend::streamPause(uint32_t s) {
    calls.push_back({SoundCall::StreamPause, s, 0});
    if (next) next->streamPause(s);
}
void RecordingSoundBackend::streamResume(uint32_t s) {
    calls.push_back({SoundCall::StreamResume, s, 0});
    if (next) next->streamResume(s);
}
bool RecordingSoundBackend::streamPlaying(uint32_t s) { return next ? next->streamPlaying(s) : false; }
std::vector<uint8_t> RecordingSoundBackend::readVoiceTrack(const std::string& file, uint32_t track) {
    return next ? next->readVoiceTrack(file, track) : std::vector<uint8_t>{};
}

std::string soundCallText(const SoundCall& c) {
    static const char* names[] = {"?", "play", "volume", "pan", "freq", "stop", "stream-volume", "stream-play",
                                  "stream-stop", "release", "stream-destroy", "pause", "resume", "stream-pause",
                                  "stream-resume"};
    char b[80];
    std::snprintf(b, sizeof b, "%s %u %d", c.op < 15 ? names[c.op] : "?", c.a, int32_t(c.b));
    return b;
}

// ---- state -------------------------------------------------------------------------------------------------------

// ---- requests ----------------------------------------------------------------------------------------------------

int soundPlayFlat(uint16_t id) {
    const int i = queueAlloc(SoundRequestKind::Sound);
    if (i < 0) return -1;
    auto&& q = soundQueue().entries[size_t(i)];
    q.setPayloadLow(0);
    q.id = id;
    q.setPayloadHigh(0);
    return 0;
}

int soundPlayFlatForced(uint16_t id) {
    auto&& q = soundQueue().entries[size_t(queueAllocForced(SoundRequestKind::Sound))];
    q.setPayloadLow(0);
    q.id = id;
    q.setPayloadHigh(0);
    return 0;
}

int soundPlayMusicIfIdle(uint16_t id) {
    const SoundDescriptor d = descriptor(id);
    if ((d.flags & kDescriptorHandlerLowBits) < kDescriptorMusic) return -1;
    if ((1u << ((d.mix >> 4) & 0x1f)) & musicState().playing) return -1;
    return soundPlayFlatForced(id);
}

int soundPlayMusicWithFade(uint16_t id, uint16_t channelArg, uint16_t fadeEntry, uint32_t flags) {
    auto&& q = soundQueue().entries[size_t(queueAllocForced(SoundRequestKind::Music))];
    q.id = id;
    q.x = int16_t(channelArg);
    q.y = int16_t(fadeEntry);
    q.setPayloadHigh(flags);
    return 0;
}

int soundPlayPanned(uint16_t id, int16_t pan, int16_t atten) {
    const int i = queueAlloc(SoundRequestKind::Sound);
    if (i < 0) return -1;
    auto&& q = soundQueue().entries[size_t(i)];
    q.id = id;
    q.mode = uint8_t(SoundMode::Panned);
    q.x = pan;
    q.z = atten;
    return 0;
}

int soundPlayAtPos(uint16_t id, const Fixed16Vec3& pos) {
    const int i = queueAlloc(SoundRequestKind::Sound);
    if (i < 0) return -1;
    auto&& q = soundQueue().entries[size_t(i)];
    q.id = id;
    q.x = pos.x.whole;
    q.y = pos.y.whole;
    q.z = pos.z.whole;
    q.mode = uint8_t(SoundMode::AtPoint);
    return 0;
}

int soundPlayAtPos(uint16_t id, uint32_t pos) {
    if (pos != 0) throw std::logic_error("sound: a point by its address (the callers pass the point itself)");
    const int i = queueAlloc(SoundRequestKind::Sound);
    if (i < 0) return -1;
    auto&& q = soundQueue().entries[size_t(i)];
    q.id = id;
    q.mode = uint8_t(SoundMode::AtListener);
    return 0;
}

// the listener is on the player (its position pointer at the player's position, as exe 0x5d53e0 tests it: the camera's
// eye never matches)
bool listenerOnPlayer() { return soundListener().position.kind == ListenerRef::Kind::PlayerPosition; }

int soundPlayAtPlayer(uint16_t id) {
    if (listenerOnPlayer()) return soundPlayAtPos(id, 0u);
    return soundPlayAtPos(id, player().pos);
}

int soundPlayAtPosS16(uint16_t id, const ShortVec3& pos) {
    const int i = queueAlloc(SoundRequestKind::Sound);
    if (i < 0) return -1;
    auto&& q = soundQueue().entries[size_t(i)];
    q.id = id;
    q.x = pos.x;
    q.y = pos.y;
    q.z = pos.z;
    q.mode = uint8_t(SoundMode::AtPoint);
    return 0;
}

int soundPlayAtPosS16(uint16_t id, uint32_t pos) {
    if (pos != 0) throw std::logic_error("sound: a point by its address (the callers pass the point itself)");
    const int i = queueAlloc(SoundRequestKind::Sound);
    if (i < 0) return -1;
    auto&& q = soundQueue().entries[size_t(i)];
    q.id = id;
    q.mode = uint8_t(SoundMode::AtListener);
    return 0;
}

int soundPlayAtXyz(uint16_t id, int16_t x, int16_t y, int16_t z) {
    const int i = queueAlloc(SoundRequestKind::Sound);
    if (i < 0) return -1;
    auto&& q = soundQueue().entries[size_t(i)];
    q.id = id;
    q.x = x;
    q.y = y;
    q.z = z;
    q.mode = uint8_t(SoundMode::AtPoint);
    return 0;
}

namespace {
// a loop sound's point: a 16.16 point's integer parts, or none (the listener's)
struct LoopPoint {
    bool set;
    int16_t x, y, z;
};
LoopPoint loopPointAt(uint32_t pos) {
    if (pos != 0) throw std::logic_error("sound: a loop's point by its address (the callers pass the point itself)");
    return {false, 0, 0, 0};
}
// a started loop voice's point: the point, or zero (none: z, y, x cleared)
void setLoopPoint(LoopVoice& lv, const LoopPoint& pos) {
    if (pos.set) {
        lv.point.x = pos.x;
        lv.point.y = pos.y;
        lv.point.z = pos.z;
    } else {
        lv.point.z = 0;
        lv.point.y = 0;
        lv.point.x = 0;
    }
}
// the loop's handle: its extra voices << 6 | its first loop voice + 1
constexpr uint32_t kHandleExtraShift = 6, kHandleVoiceMask = 0x3f;
} // namespace

namespace {
uint32_t startLoop(uint16_t id, const LoopPoint& pos, uint16_t param) {
    LoopVoices& loops = loopVoices();
    const SoundDescriptor d = descriptor(id);
    const uint32_t first = (d.voice & 0x1f) - kFirstLoopVoice;
    if (int32_t(loops.allocated << (first & 0x1f)) < 0) return 0;
    const int i = queueAlloc(SoundRequestKind::Loop);
    if (i < 0) return 0;
    auto&& q = soundQueue().entries[size_t(i)];
    q.id = id;
    if (!pos.set) {
        q.mode = uint8_t(SoundMode::AtListener);
    } else {
        q.x = pos.x;
        q.y = pos.y;
        q.z = pos.z;
        q.mode = uint8_t(SoundMode::AtPoint);
    }
    const uint32_t extra = (d.flags >> 2) & 3;
    for (uint32_t c = first; int32_t(c) <= int32_t(extra + first); ++c) {
        auto&& lv = loops.voices[c];
        loops.allocated |= 0x80000000u >> (c & 0x1f);
        lv.keep = 1;
        setLoopPoint(lv, pos);
        lv.pitch = int16_t(param);
    }
    return (extra << kHandleExtraShift) | (first + 1);
}
} // namespace

uint32_t soundStartLoop(uint16_t id, uint32_t pos, uint16_t param) { return startLoop(id, loopPointAt(pos), param); }
uint32_t soundStartLoop(uint16_t id, const Fixed16Vec3& pos, uint16_t param) {
    return startLoop(id, LoopPoint{true, pos.x.whole, pos.y.whole, pos.z.whole}, param);
}
uint32_t soundStartLoopAtXyz(uint16_t id, int16_t x, int16_t y, int16_t z, uint16_t param) {
    return startLoop(id, LoopPoint{true, x, y, z}, param);
}

uint32_t soundStartLoopAtPlayer(uint16_t id, uint16_t param) {
    if (listenerOnPlayer()) return soundStartLoop(id, 0, param);
    return soundStartLoop(id, player().pos, param);
}

namespace {
int updateLoop(uint32_t handle, const LoopPoint& pos, uint16_t param) {
    if (handle == 0) return 0;
    LoopVoices& loops = loopVoices();
    const uint32_t extra = (handle >> kHandleExtraShift) & 3;
    uint32_t c = (handle & kHandleVoiceMask) - 1;
    // a loop voice kept alive and moved (its point's x / y / z stores in this order)
    auto refresh = [&](uint32_t k) {
        auto&& lv = loops.voices[k];
        lv.keep = 1;
        if (pos.set) {
            lv.point.x = pos.x;
            lv.point.y = pos.y;
            lv.point.z = pos.z;
        } else {
            lv.point.x = 0;
            lv.point.y = 0;
            lv.point.z = 0;
        }
        lv.pitch = int16_t(param);
    };
    if (extra == 0) {
        if (int32_t(loops.sounding << (c & 0x1f)) >= 0) return 0;
        refresh(c);
        return -1;
    }
    const uint32_t last = extra + c;
    for (; c <= last; ++c) {
        if (int32_t(loops.sounding << (c & 0x1f)) >= 0) continue;
        refresh(c);
    }
    return -1;
}
} // namespace

int soundUpdateLoop(uint32_t handle, uint32_t pos, uint16_t param) { return updateLoop(handle, loopPointAt(pos), param); }
int soundUpdateLoop(uint32_t handle, const Fixed16Vec3& pos, uint16_t param) {
    return updateLoop(handle, LoopPoint{true, pos.x.whole, pos.y.whole, pos.z.whole}, param);
}
int soundUpdateLoopAtXyz(uint32_t handle, int16_t x, int16_t y, int16_t z, uint16_t param) {
    return updateLoop(handle, LoopPoint{true, x, y, z}, param);
}

int soundUpdateLoopAtPlayer(uint32_t handle, uint16_t param) {
    if (listenerOnPlayer()) return soundUpdateLoop(handle, 0, param);
    return soundUpdateLoop(handle, player().pos, param);
}

void soundStopLoop(uint32_t handle) {
    if (handle == 0) return;
    LoopVoices& loops = loopVoices();
    uint32_t c = (handle & kHandleVoiceMask) - 1;
    const uint32_t last = ((handle >> kHandleExtraShift) & 3) + c;
    for (; c <= last; ++c) {
        if (int32_t(loops.sounding << (c & 0x1f)) >= 0) continue;
        const uint32_t bit = 0x80000000u >> (c & 0x1f);
        loops.allocated ^= bit;
        loops.sounding ^= bit;
        stopSfxVoice(c + kFirstLoopVoice);
    }
}

int soundStopAllLoops() { return queueAlloc(SoundRequestKind::StopLoops) >= 0 ? 0 : -1; }

void soundQueueClear() {
    SoundQueue& q = soundQueue();
    for (size_t i = 0; i < kQueueEntries; ++i) {
        auto&& e = q.entries[i];
        e.kind = 0;
    }
    q.count = 0;
    q.flags &= ~kQueuePending;
}

void soundQueuePopFront() {
    SoundQueue& q = soundQueue();
    for (size_t i = 0; i + 1 < kQueueEntries; ++i) {
        auto&& e = q.entries[i];
        e.copyWords(q.entries[i + 1]);
    }
    auto&& last = q.entries[kQueueEntries - 1];
    last.kind = 0;
    if (q.count != 0) q.count = q.count - 1;
    if (q.entries[0].kind == 0) q.flags &= ~kQueuePending;
}

int soundFadeOut(uint32_t mask, int16_t step, int16_t delay) {
    for (size_t i = 0; i < kFades; ++i) {
        if (!(mask & (1u << i))) continue;
        auto&& f = soundFades().fades[i];
        f.active = 1;
        f.setTypeWord(uint8_t(SoundFadeType::Out));
        f.step = int16_t(-int32_t(step));
        f.delay = delay;
    }
    musicState().done &= ~mask;
    return 0;
}

int soundFadeIn(uint32_t mask, int16_t step, int16_t delay) {
    for (size_t i = 0; i < kFades; ++i) {
        if (!(mask & (1u << i))) continue;
        auto&& f = soundFades().fades[i];
        f.active = 1;
        f.setTypeWord(uint8_t(SoundFadeType::In));
        f.step = step;
        f.delay = delay;
    }
    return 0;
}

int soundFadeDip(uint32_t mask, int16_t step, int16_t target, int16_t delay) {
    for (size_t i = 0; i < kFades; ++i) {
        if (!(mask & (1u << i))) continue;
        auto&& f = soundFades().fades[i];
        f.active = 1;
        f.setTypeWord(uint8_t(SoundFadeType::Dip));
        f.step = step;
        f.target = target;
        f.delay = delay;
    }
    musicState().done &= ~(mask << 8 | mask);
    return 0;
}

int soundFadeRestore(uint32_t mask, int16_t step, int16_t delay) {
    for (size_t i = 0; i < kFades; ++i) {
        auto&& f = soundFades().fades[i];
        if (!(mask & (1u << i)) || f.active == 0 || f.type != uint8_t(SoundFadeType::Dip)) continue;
        f.state = kDipRestoreDelay;
        f.step = step;
        f.delay = delay;
    }
    musicState().done &= ~(mask << 8);
    return 0;
}

bool soundDone(uint32_t mask) { return (musicState().done & mask) == mask; }
void soundMarkFadesDone(uint32_t mask) { musicState().done |= mask; }


bool musicChannelPlaying(uint32_t channel) { return (musicState().playing & (1u << channel)) != 0; }

void stageSoundFlagsSet(uint32_t flags) {
    SoundContext& ctx = soundContext();
    ctx.stageFlags = uint8_t(flags);
    if (flags & kSoundStageBattleMusic) ctx.battleMusic = uint8_t(flags >> 8);
}

void stageSoundStart(uint32_t musicId, uint32_t flags) {
    LoopVoices& loops = loopVoices();
    if (musicId != kNoMusic) soundPlayFlatForced(uint16_t(musicId));
    else flags |= kSoundStageSilent;
    stageSoundFlagsSet(flags);
    loops.allocated = 0;
    loops.sounding = 0;
    soundLoopState().holdLoops = 0;
    // without kSoundStageSkipEmptyCall: exe 0x5d4f30(1), a bare ret
}

void soundSetListener(ListenerRef pos, ListenerRef rot, uint8_t format) {
    SoundListener& l = soundListener();
    l.position = pos;
    l.rotation = rot;
    soundContext().listenerFormat = format;
}

void stopSfxVoice(uint32_t i) {
    auto&& v = soundVoices().voices[i];
    if (v.buffer != 0) {
        be().stop(v.buffer);
        v.clearWords();
    }
}

void stopMusicChannel(uint32_t ch) {
    MusicState& m = musicState();
    const uint32_t bit = 1u << (ch & 0x1f);
    if (!(bit & m.playing)) return;
    m.playing ^= bit;
    auto&& f = soundFades().fades[ch];
    if (f.active != 0) {
        f.active = 0;
        m.done |= bit;
    }
    const uint32_t n = kFirstLoopVoice - (uint32_t(soundContext().stageFlags) >> 7);
    for (uint32_t i = 0; i < n; ++i) {
        auto&& v = soundVoices().voices[i];
        if (v.channel == ch) {
            v.active = 0;
            v.distance = 0;
        }
    }
    be().streamStop(m.channels[ch].stream);
    soundContext().fadeVolumes[ch] = 0;
    m.currentStream = kNoStream;
}

namespace {
// the stops' first voice (voice 15 with kSoundStageVoice15Sfx)
uint32_t firstStopVoice() {
    return (soundContext().stageFlags & kSoundStageVoice15Sfx) ? kFirstStopVoice : kFirstLoopVoice;
}
// the loop voices dropped (the keep-alive, the masks, each one's keep)
void dropLoopVoices() {
    LoopVoices& loops = loopVoices();
    soundLoopState().holdLoops = 0;
    loops.allocated = 0;
    loops.sounding = 0;
    for (size_t i = 0; i < kLoopVoices; ++i) {
        auto&& lv = loops.voices[i];
        lv.keep = 0;
    }
}
} // namespace

void soundStopAll() {
    soundQueueClear();
    soundContext().head = 0;
    voiceClipStop();
    for (uint32_t ch = 0; ch < kMusicChannels; ++ch) stopMusicChannel(ch);
    for (uint32_t v = kFirstStopVoice; v < kVoices; ++v) stopSfxVoice(v);
    for (size_t i = 0; i < kFades; ++i) {
        auto&& f = soundFades().fades[i];
        f.active = 0;
    }
    dropLoopVoices();
    musicState().done = kAllFadesDone;
}

void soundStopSfxVoices() {
    for (uint32_t v = firstStopVoice(); v < kVoices; ++v) stopSfxVoice(v);
    dropLoopVoices();
}

void soundStopSfxVoicesKeep18() {
    for (uint32_t v = firstStopVoice(); v < kVoices - 1; ++v)
        if (v != kKeptVoice) stopSfxVoice(v);
    dropLoopVoices();
}

void soundPauseAll() {
    for (uint32_t i = kFirstStopVoice; i < kVoices; ++i) {
        const SfxVoice& v = soundVoices().voices[i];
        if (v.buffer) be().pause(v.buffer);
    }
    if (musicState().currentStream != kNoStream) be().streamPause(musicState().currentStream);
    if (voiceClip().stream != kNoStream) be().streamPause(voiceClip().stream);
}

void soundResumeAll() {
    for (uint32_t i = kFirstStopVoice; i < kVoices; ++i) {
        const SfxVoice& v = soundVoices().voices[i];
        if (v.buffer) be().resume(v.buffer);
    }
    if (musicState().currentStream != kNoStream) be().streamResume(musicState().currentStream);
    if (voiceClip().stream != kNoStream) be().streamResume(voiceClip().stream);
}

void soundApplyBgmMaster() {
    for (size_t i = 0; i < kMusicChannels; ++i) {
        const MusicChannel& c = musicState().channels[i];
        if (c.stream) be().streamVolume(c.stream, soundMasters().bgm);
    }
}

// the reset's value of the context's volumes 2 / 3 (0 / 1 get kSoundVolumeFull; the readers are not traced)
constexpr uint16_t kResetVolumeHigh = 0x7fff;

void soundReset() {
    SoundContext& ctx = soundContext();
    SoundQueue& q = soundQueue();
    MusicState& m = musicState();
    LoopVoices& loops = loopVoices();
    const int8_t sfx = soundMasters().sfx, bgm = soundMasters().bgm;
    ctx.clearBytes(); // exe 0x40dc40 of each block
    sfxBanks().clearBytes();
    m.clearBytes();
    mp3Slots().clearBytes();
    q.clearBytes();
    soundVoices().clearBytes();
    soundFades().clearBytes();
    loops.clearBytes();
    ctx.resetVolumes[2] = kResetVolumeHigh;
    ctx.resetVolumes[3] = kResetVolumeHigh;
    q.flags = 0;
    m.done = kAllFadesDone;
    m.playing = 0;
    q.count = 0;
    ctx.resetVolumes[0] = uint16_t(kSoundVolumeFull);
    ctx.resetVolumes[1] = uint16_t(kSoundVolumeFull);
    soundMasters().bgm = bgm;
    soundMasters().sfx = sfx;
    for (size_t i = 0; i < kBanks; ++i) {
        auto&& b = sfxBanks().banks[i];
        b.bankId = kNoBank;
    }
    for (size_t i = 0; i < kMusicChannels; ++i) {
        auto&& c = m.channels[i];
        c.index = uint8_t(i);
    }
    for (size_t i = 0; i < kLoopVoices; ++i) {
        auto&& lv = loops.voices[i];
        lv.keep = 0;
        lv.voice = uint8_t(kFirstLoopVoice + i);
    }
    ctx.setFadeVolumeWord(0, 0);
    loops.sounding = 0;
    ctx.setFadeVolumeWord(1, 0);
    loops.allocated = 0;
    soundLoopState().holdLoops = 0;
    ctx.setFadeVolumeWord(2, 0);
    for (size_t i = 0; i < kFades; ++i) {
        auto&& f = soundFades().fades[i];
        f.active = 0;
        f.channel = uint8_t(i);
    }
    ctx.fadeVolumes[kSfxGroupFade] = kSoundVolumeFull;
    ctx.fadeVolumes[kVoiceClipFadeEntry] = kSoundVolumeFull;
}

// ---- data ----------------------------------------------------------------------------------------------------------

void unloadSfxBank(uint32_t slot) {
    auto&& b = sfxBanks().banks[slot];
    if (b.data == 0) return;
    const uint32_t end = b.samples;
    for (uint32_t at = b.entries; at < end; at += kBankEntrySize) {
        const BankEntry e = dataRecord<BankEntry>(at, BankEntry::kBytes);
        if (e.buffer == 0) continue;
        for (uint32_t i = kFirstStopVoice; i < kVoices; ++i) {
            auto&& v = soundVoices().voices[i];
            if (v.buffer == e.buffer) v.buffer = 0;
        }
        be().releaseBuffer(e.buffer);
    }
    b.clearWords();
}

void loadSfxBank(std::span<const uint8_t> seg, uint32_t slot, uint32_t bankId) {
    unloadSfxBank(slot);
    if (seg.size() > kSfxBankStride) throw std::runtime_error("SFX bank larger than its OpenDash slot");
    const uint32_t data = kSfxBankMem + slot * kSfxBankStride;
    loadedFiles().map(data, seg); // the bank's bytes, a loaded file at its slot's address
    auto&& b = sfxBanks().banks[slot];
    b.data = data;
    b.bankId = bankId;
    b.entries = data + kBankHeaderSize;
    const uint32_t samples = data + dataRecord<BankHeader>(data, BankHeader::kBytes).sampleTable;
    b.samples = samples;
    for (uint32_t at = data + kBankHeaderSize; at < samples; at += kBankEntrySize) {
        const BankEntry e = dataRecord<BankEntry>(at, BankEntry::kBytes);
        const uint32_t k = e.sample;
        const SampleEntry sample = dataRecord<SampleEntry>(samples + k * kBankEntrySize, SampleEntry::kBytes);
        // the entry's buffer written into the bank's bytes, where startVoice reads it
        if (k != 0 && sample.offset != 0)
            storeLoaded(at + BankEntry::kBufferOffset, be().createBuffer(samples + sample.offset, sample.loopOffset));
    }
}

void unloadMp3Slot(uint32_t slot) {
    auto&& s = mp3Slots().slots[slot];
    if (s.marker == 0) return;
    be().destroyStream(s.stream);
    s.clearWords();
}

void loadMp3Slot(std::span<const uint8_t> seg, uint32_t slot, uint32_t loop) {
    unloadMp3Slot(slot);
    auto&& s = mp3Slots().slots[slot];
    s.marker = kMp3Marker + slot * 16;
    s.size = uint32_t(seg.size());
    s.loop = loop;
    s.stream = be().createStream(seg, loop);
}

// ---- voice clips ---------------------------------------------------------------------------------------------------

void voiceClipStop() {
    VoiceClipState& c = voiceClip();
    if (c.stream == kNoStream) return;
    be().destroyStream(c.stream);
    c.stream = kNoStream;
    c.data = 0;
}

void voiceClipPlay(uint32_t id) {
    const uint32_t i = id & kVoiceIndexMask;
    uint32_t e = 0;
    const VoiceTables& tables = voiceTables();
    if (!(id & kSecondVoiceTable)) {
        if (i >= kBuiltInVoiceFirst || tables.stage == 0) return;
        e = tables.stage + i * 4;
    } else if (i < kBuiltInVoiceFirst) {
        if (tables.second == 0) return;
        e = tables.second + i * 4;
    } else {
        e = stateAddress(TableId::AudioBuiltInVoices) + i * 4;
    }
    voiceClipStop();
    const VoiceEntry entry = dataRecord<VoiceEntry>(e, VoiceEntry::kBytes);
    const VoiceFileNames& files = *voiceFileNames;
    if (entry.file >= VoiceFileNames::kFiles) tableIndexError(entry.file, VoiceFileNames::kFiles);
    if (!files.present[entry.file]) return;
    const std::string& name = files.names[entry.file];
    const std::vector<uint8_t> track = be().readVoiceTrack(name, entry.track);
    if (track.empty()) return; // exe 0x40b410 failed
    VoiceClipState& c = voiceClip();
    c.data = kVoiceClipMarker;
    const uint32_t s = be().createStream(track, 0);
    c.stream = s;
    const int32_t vol = (((int32_t(id) >> 16) + kVoiceVolumeBase) * int32_t(soundMasters().sfx)) >> 7;
    c.volume = vol;
    be().streamVolume(s, vol);
    be().streamPlay(c.stream);
    c.fading = 0;
}

void voiceClipFadeOut(uint32_t step) {
    VoiceClipState& c = voiceClip();
    c.fading = 1;
    c.step = int32_t(step);
}

bool voiceClipPlaying() {
    if (voiceClip().stream == kNoStream) return false;
    return be().streamPlaying(voiceClip().stream);
}

void buildSin64Tables() {
    Trig64Tables& t = trig64Tables();
    for (size_t i = 0; i < 64; ++i) { // the exe's sine table (game/angles), every 64th angle
        t.sine[i] = fixedSin(int(i * 64));
        t.cosine[i] = fixedCos(int(i * 64));
    }
}

void soundPanProbe(int vol, int sx, int sy, int sz, int out[3]) {
    const int32_t src[3] = {int32_t(uint32_t(sx) << 16), int32_t(uint32_t(sy) << 16), int32_t(uint32_t(sz) << 16)};
    const int32_t d = listenerDistance(src[0], src[1], src[2]);
    auto&& v = soundVoices().voices[0];
    v.clearWords();
    pan3d(v, vol, src, d);
    out[0] = d;
    out[1] = v.pan;
    out[2] = v.volume;
}

void soundExplicitPanProbe(int vol, int pan, int atten, int out[3]) {
    auto&& v = soundVoices().voices[0];
    v.clearWords();
    explicitPan(v, vol, pan, atten);
    out[0] = 0;
    out[1] = v.pan;
    out[2] = v.volume;
}

void voiceClipPause() {
    if (voiceClip().stream != kNoStream) be().streamPause(voiceClip().stream);
}

void voiceClipResume() {
    if (voiceClip().stream != kNoStream) be().streamResume(voiceClip().stream);
}

// ---- per frame -----------------------------------------------------------------------------------------------------

void soundUpdate() {
    SoundQueue& q = soundQueue();
    SoundContext& ctx = soundContext();
    voiceClipUpdate();
    while (q.entries[0].kind != 0) {
        ctx.request.copyFrom(q.entries[0]);
        soundQueuePopFront();
        if (dispatch(ctx.request.kind) != 0) break;
    }
    if (loopVoices().sounding != 0) updateLoopVoices();
    for (size_t i = 0; i < kFades; ++i) {
        auto&& f = soundFades().fades[i];
        if (f.active == 0) continue;
        switch (SoundFadeType(f.type)) {
        case SoundFadeType::Out: fadeOutStep(f); break;
        case SoundFadeType::In: fadeInStep(f); break;
        case SoundFadeType::Dip: fadeDipStep(f); break;
        default: notPorted(kAudioPort, "fade type > 2");
        }
    }
}

} // namespace od::port
