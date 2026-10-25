// The sound logic: the request queue the game code fills and the per-frame dispatcher that drains it, the SFX voices
// (allocation, stealing, the 3D pan / volume math), the loop voices, the music channels and their fades, the SFX banks
// and the MP3 slots, the stage's sound flags, the listener, and the voice-clip (snd/xa*.dat) player. Every DirectSound
// call of the original goes to a SoundBackend instead (the game-level arguments: buffer handle, volume 0..127, pan
// 0..128, frequency Hz) -- bit-exact against tests/data/snd_*.bin.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port {

// What the original hands to DirectSound (exe 0x40a5f0..0x40afd0), at the game's level of abstraction.
// Buffers are per bank entry (one DirectSound buffer, or an intro + loop pair, per entry with a sample); streams are
// the MP3 players (music slots and the voice clip).
struct SoundBackend {
    virtual ~SoundBackend() = default;
    // exe 0x40a5f0(wav, loopOfs): the buffer(s) for a RIFF/WAVE at `wav` in the image; 0 = failed
    virtual uint32_t createBuffer(uint32_t wav, int32_t loopOfs) { (void)wav; (void)loopOfs; return 1; }
    virtual void releaseBuffer(uint32_t h) { (void)h; }                         // exe 0x40aae0
    // exe 0x40a6f0: SetFrequency + restart from 0 (+ the loop part); true = a looped buffer started (a one-shot starts and
    // answers false: the voice is not kept as playing)
    virtual bool play(uint32_t h, uint32_t freq) { return h != 0 && freq != 0; }
    virtual void setVolume(uint32_t h, int32_t v) { (void)h; (void)v; }      // exe 0x40a800: 0..127 (0 = -10000 cB)
    virtual void setPan(uint32_t h, int32_t p) { (void)h; (void)p; }         // exe 0x40a860: 0..128, 64 = centre
    virtual void setFrequency(uint32_t h, uint32_t f) { (void)h; (void)f; }  // exe 0x40a8b0
    virtual void stop(uint32_t h) { (void)h; }                               // exe 0x40a950
    virtual void pause(uint32_t h) { (void)h; }                              // exe 0x40aa00
    virtual void resume(uint32_t h) { (void)h; }                             // exe 0x40aa70
    // exe 0x40ab60(data, size, loop): an MP3 stream (music slot / voice clip); returns its index
    virtual uint32_t createStream(std::span<const uint8_t> mp3, uint32_t loop) { (void)mp3; (void)loop; return 0; }
    virtual void destroyStream(uint32_t s) { (void)s; }                      // exe 0x40afd0
    virtual void streamPlay(uint32_t s) { (void)s; }                         // exe 0x40ae40
    virtual void streamStop(uint32_t s) { (void)s; }                         // exe 0x40af10
    virtual void streamVolume(uint32_t s, int32_t v) { (void)s; (void)v; }   // exe 0x40ae80: 0..127 like setVolume
    virtual void streamPause(uint32_t s) { (void)s; }                        // exe 0x40af40
    virtual void streamResume(uint32_t s) { (void)s; }                       // exe 0x40af90
    virtual bool streamPlaying(uint32_t s) { (void)s; return false; }        // exe 0x40aed0
    // exe 0x601050's file read: track `track` of snd\<file> (u32 offset table + MP3s); empty = not found
    virtual std::vector<uint8_t> readVoiceTrack(const std::string& file, uint32_t track) {
        (void)file;
        (void)track;
        return {};
    }
};

// One backend call (tests / logs): op + arguments as the original passes them
struct SoundCall {
    enum Op : uint8_t {
        Play = 1, Volume, Pan, Frequency, Stop, StreamVolume, StreamPlay, StreamStop, Release, StreamDestroy,
        Pause, Resume, StreamPause, StreamResume,
    };
    uint8_t op = 0;
    uint32_t a = 0, b = 0;
    bool operator==(const SoundCall&) const = default;
};
// A backend that only records the calls (play() follows exe 0x40a6f0 with a valid buffer: h != 0 && freq != 0)
struct RecordingSoundBackend : SoundBackend {
    std::vector<SoundCall> calls;
    SoundBackend* next = nullptr; // optional: forward every call (the app logs + mixes)
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
};
std::string soundCallText(const SoundCall& c);

// The backend every function below talks to (process-wide like the original's DirectSound object); null = no-ops.
void setSoundBackend(SoundBackend* b);
SoundBackend* soundBackend();

// ---- the state the other systems read ----------------------------------------------------------------------------
// The loop sounds' hold: while set, the loop voices keep sounding without their keep-alive (the status sub-screen
// pauses the field's loops rather than stopping them); the frame's keep-alive otherwise
struct SoundLoopState {
    uint32_t holdLoops = 0;
};
inline SoundLoopState& soundLoopState() {
    static SoundLoopState s;
    return s;
}
// The volume masters (the options' volume row; 0..0x7f)
struct SoundMasters {
    int8_t sfx = 0;
    int8_t bgm = 0; // applied to the streams by soundApplyBgmMaster
};
constexpr int8_t kSoundMasterFull = 0x7f;
// A sound request (16 bytes: the queue's entries, the one being dispatched): its kind, the sound id and the kind's
// payload -- a point (x, y, z) and a mode; for an explicit pan x is the pan and z the attenuation; for the music the
// fade's two arguments x / y and the flags dword; for a descriptor request the descriptor's address
struct SoundRequest {
    uint16_t kind = 0;  // SoundRequestKind (0: a free entry)
    uint16_t id = 0;
    uint32_t unread04 = 0; // copied with the entry (no reader is traced)
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;
    uint8_t mode = 0;   // SoundMode
    uint8_t modeHigh = 0; // the payload's last byte (a music request's flags' high byte)

    // the payload's two dwords (+8: the point's x / y, a descriptor's address; +0xc: z / the mode, the music's flags)
    uint32_t payloadLow() const { return uint32_t(uint16_t(x)) | uint32_t(uint16_t(y)) << 16; }
    uint32_t payloadHigh() const { return uint32_t(uint16_t(z)) | uint32_t(mode) << 16 | uint32_t(modeHigh) << 24; }
    void setPayloadLow(uint32_t v) {
        x = int16_t(uint16_t(v));
        y = int16_t(uint16_t(v >> 16));
    }
    void setPayloadHigh(uint32_t v) {
        z = int16_t(uint16_t(v));
        mode = uint8_t(v >> 16);
        modeHigh = uint8_t(v >> 24);
    }
    // the kind, the id and the payload copied from another (the dispatcher's copy: two words, two dwords)
    void copyFrom(const SoundRequest& o) {
        kind = o.kind;
        id = o.id;
        setPayloadLow(o.payloadLow());
        setPayloadHigh(o.payloadHigh());
    }
    // the 16 bytes copied from another (the queue's shift)
    void copyWords(const SoundRequest& o) { *this = o; }
};
// The request queue (24 entries; the host's sound log reads its requests)
constexpr size_t kQueueEntries = 24;
struct SoundQueue {
    StateArray<SoundRequest, kQueueEntries> entries{};
    uint32_t count = 0;
    uint32_t flags = 0; // kQueuePending

    // the queue's entries cleared (the reset)
    void clearBytes() { entries.fill(SoundRequest{}); }
};
constexpr uint32_t kQueuePending = 1;
inline SoundQueue& soundQueue() {
    static SoundQueue s;
    return s;
}

// The sound context (0xa71760): its head word, the volumes the reset sets, the masters, the listener's format, the fade
// entries' volumes, the stage's sound flags and battle music (exe 0x5d3cf0 sets them at the stage load), the request
// being dispatched
struct SoundContext {
    uint32_t head = 0;                    // cleared by the stop-all (its reader is not traced)
    StateArray<uint16_t, 4> resetVolumes{}; // set by the reset (0x3fff, 0x3fff, 0x7fff, 0x7fff; their readers are not
                                          // traced)
    StateArray<uint8_t, 0x24> unread0c{}; // the bytes up to the masters (cleared with the context; no reader)
    SoundMasters masters;                 // +0x30 / +0x31
    uint8_t unread32 = 0;                 // (cleared with the context; no reader)
    uint8_t listenerFormat = 0;           // SoundListenerFormat
    StateArray<int16_t, 6> fadeVolumes{}; // the fade entries' volumes (0 .. kSoundVolumeFull)
    StateArray<uint8_t, 2> unread40{};    // (cleared with the context; no reader)
    uint8_t stageFlags = 0;               // kSoundStage*
    uint8_t battleMusic = 0;              // the field encounters' battle music (with kSoundStageBattleMusic)
    SoundRequest request;

    // the volumes' dwords k (0: entries 0 / 1, 1: 2 / 3, 2: 4 / 5) set (the reset's dword stores)
    void setFadeVolumeWord(size_t k, uint32_t v) {
        fadeVolumes[k * 2] = int16_t(uint16_t(v));
        fadeVolumes[k * 2 + 1] = int16_t(uint16_t(v >> 16));
    }
    // the context cleared (the reset's exe 0x40dc40)
    void clearBytes() { *this = SoundContext{}; }
};
inline SoundContext& soundContext() {
    static SoundContext s;
    return s;
}

// What a listener pointer names: the camera's eye / view angles (game/camera_state), the player's position / rotation
// (game/player/player_state) or none
struct ListenerRef {
    enum class Kind : uint8_t { None, CameraEye, CameraAngles, PlayerPosition, PlayerRotation };
    Kind kind = Kind::None;
    static ListenerRef none() { return {Kind::None}; }
    static ListenerRef cameraEye() { return {Kind::CameraEye}; }
    static ListenerRef cameraAngles() { return {Kind::CameraAngles}; }
    static ListenerRef playerPosition() { return {Kind::PlayerPosition}; }
    static ListenerRef playerRotation() { return {Kind::PlayerRotation}; }
};
// The listener: its position (in SoundContext::listenerFormat) and its rotation (the yaw: the second short)
struct SoundListener {
    ListenerRef position;
    ListenerRef rotation;
};
inline SoundListener& soundListener() {
    static SoundListener s;
    return s;
}
inline SoundMasters& soundMasters() { return soundContext().masters; }
// SoundContext::stageFlags (exe 0x5d3cf0's flags). Their readers in the exe: 0x50fe62 (bit 0x02), 0x5d3ed0 (0x20),
// 0x40ddc6 / 0x5d59b0 / 0x5d5a00 (0x80); bit 0x01 is set by many stages and read by none of them
constexpr uint8_t kSoundStageUnreadBit0 = 0x01;  // stored, never read
constexpr uint8_t kSoundStageSilent = 0x02;      // the stage plays no music (no battle music either)
constexpr uint8_t kSoundStageBattleMusic = 0x04; // the flags' second byte is the battle music
constexpr uint8_t kSoundStageBanksMuted = 0x20;  // the stage's SFX banks 1 / 2 muted
constexpr uint8_t kSoundStageSkipEmptyCall = 0x40; // exe 0x5d3cf0 skips its call of exe 0x5d4f30(1) (a bare ret)
constexpr uint8_t kSoundStageVoice15Sfx = 0x80;  // voice 15 is an SFX voice too (the stops include it, the music
                                                 // channels' voices exclude it)
// a stage's flags with its field encounters' battle music
constexpr uint32_t soundStageBattleMusic(uint8_t music) { return kSoundStageBattleMusic | uint32_t(music) << 8; }
// stageSoundStart's music id for none
constexpr uint32_t kNoMusic = 0xff;
// a music request's flags (soundPlayMusicWithFade; exe 0x5d40c0 reads bits 0x01 / 0x02)
constexpr uint32_t kMusicRequestFullVolume = 0x01; // its fade entry's volume full at once (else 0)
constexpr uint32_t kMusicRequestHeldDip = 0x02;    // its fade entry set to a held dip
constexpr uint32_t kMusicRequestUnreadBit5 = 0x20; // passed by some stages, never read
constexpr int16_t kSoundVolumeFull = 0x3fff;     // a fade entry's full volume
// The voice-clip tables the stage setup points (voice ids below 0x8000 / the others)
struct VoiceTables {
    uint32_t stage = 0;  // the tables' addresses (the stage setup points them at the exe's)
    uint32_t second = 0;
};
inline VoiceTables& voiceTables() {
    static VoiceTables s;
    return s;
}
// the fade entries' masks (soundFadeOut / soundDone ...): the four music channels; with the SFX group and entry 5
constexpr uint32_t kMusicFades = 0x0f;
constexpr uint32_t kAllFades = 0x3f;
// OpenDash addresses for data the original mallocs (bank copies, MP3 slot markers)
constexpr uint32_t kSfxBankMem = 0x7c000000; // + slot * kSfxBankStride
constexpr uint32_t kSfxBankStride = 0x80000;

// ---- the sound system's own state (the voices, the loops, the music channels, the fades, the banks, the MP3 slots,
// the voice clip) --------------------------------------------------------------------------------------------------

// An SFX voice
struct SfxVoice {
    uint8_t active = 0;
    uint8_t entry = 0;      // its bank entry
    uint16_t frequency = 0;
    uint8_t priority = 0;
    uint8_t channel = 0;    // the music channel it belongs to (stopMusicChannel clears it)
    int16_t distance = 0;
    uint8_t pan = 0;        // 0..0x80, 0x40 centred
    uint8_t volume = 0;
    uint8_t playing = 0;
    uint8_t loop = 0;       // a loop voice's
    uint32_t buffer = 0;    // the backend's buffer

    // the voice cleared
    void clearWords() { *this = SfxVoice{}; }
};
constexpr size_t kVoices = 24;
constexpr uint32_t kFirstLoopVoice = 0x10;     // the loop voices' and the "any voice" requests' (16..23)
constexpr uint32_t kFirstStopVoice = 0xf;      // the stops' first voice (with kSoundStageVoice15Sfx)
constexpr uint32_t kKeptVoice = 0x12;          // the voice soundStopSfxVoicesKeep18 keeps
constexpr uint8_t kCentrePan = 0x40;
struct SoundVoices {
    StateArray<SfxVoice, kVoices> voices{};

    void clearBytes() { voices.fill(SfxVoice{}); }
};
inline SoundVoices& soundVoices() {
    static SoundVoices s;
    return s;
}

// A loop voice: its keep-alive, its SFX voice, volume, mode, point and pitch
struct LoopVoice {
    uint8_t keep = 0;
    uint8_t voice = 0;
    int8_t volume = 0;
    int8_t mode = 0;
    ShortVec3 point;
    int16_t pitch = 0;  // the frequency's shift (> 0: up to * 2 at 0x7f; < 0: down by / 256)
};
constexpr size_t kLoopVoices = 8;
struct LoopVoices {
    StateArray<LoopVoice, kLoopVoices> voices{};
    uint32_t allocated = 0; // bit 31 - loop voice
    uint32_t sounding = 0;

    void clearBytes() { voices.fill(LoopVoice{}); }
};
inline LoopVoices& loopVoices() {
    static LoopVoices s;
    return s;
}

// A music channel
struct MusicChannel {
    uint8_t index = 0;
    uint8_t priority = 0;
    uint8_t loop = 0;
    uint8_t unread03 = 0; // cleared with the channels (no reader)
    uint32_t stream = 0;
};
constexpr size_t kMusicChannels = 4;
struct MusicState {
    StateArray<MusicChannel, kMusicChannels> channels{};
    uint32_t playing = 0;              // bit channel
    uint32_t done = 0;                 // the fades done (bit entry; << 8 / 0x20 << the dips' hold)
    uint32_t currentStream = 0xffffffffu; // the stream last started (kNoStream: none, the start)

    void clearBytes() { channels.fill(MusicChannel{}); }
};
inline MusicState& musicState() {
    static MusicState s;
    return s;
}
constexpr uint32_t kNoStream = 0xffffffff;
constexpr uint32_t kAllFadesDone = 0xffff;
constexpr int16_t kMusicFadeInStep = 0x333;

// A fade entry (6: 0..3 the music channels, 4 the SFX group)
struct SoundFade {
    uint8_t active = 0;
    uint8_t channel = 0;
    uint8_t type = 0;    // SoundFadeType
    uint8_t state = 0;
    int16_t step = 0;
    int16_t target = 0;
    int16_t delay = 0;
    int16_t unread0a = 0; // cleared with the fades (no reader)

    // the type t, its state cleared (the exe's word store at +2)
    void setTypeWord(uint8_t t) {
        type = t;
        state = 0;
    }
};
enum class SoundFadeType : uint8_t { Out = 0, In = 1, Dip = 2 };
// a dip's states: the delay, going down, holding, the restore's delay, going up
constexpr uint8_t kDipDelay = 0, kDipDown = 1, kDipHold = 2, kDipRestoreDelay = 3, kDipUp = 4;
constexpr size_t kFades = 6;
constexpr uint32_t kSfxGroupFade = 4, kVoiceClipFadeEntry = 5;
struct SoundFades {
    StateArray<SoundFade, kFades> fades{};

    void clearBytes() { fades.fill(SoundFade{}); }
};
inline SoundFades& soundFades() {
    static SoundFades s;
    return s;
}

// An SFX bank slot: its id (kNoBank: none), its data, entries and sample table (addresses in the game state)
struct SfxBank {
    uint32_t bankId = 0;
    uint32_t data = 0;
    uint32_t entries = 0;
    uint32_t samples = 0;

    void clearWords() { *this = SfxBank{}; }
};
constexpr size_t kBanks = 5;
constexpr uint32_t kNoBank = 0xff;
struct SfxBanks {
    StateArray<SfxBank, kBanks> banks{};

    void clearBytes() { banks.fill(SfxBank{}); }
};
inline SfxBanks& sfxBanks() {
    static SfxBanks s;
    return s;
}
// An MP3 slot
struct Mp3Slot {
    uint32_t marker = 0; // OpenDash: a non-null "malloc" pointer (kMp3Marker + slot * 16)
    uint32_t size = 0;
    uint32_t loop = 0;
    uint32_t stream = 0;

    void clearWords() { *this = Mp3Slot{}; }
};
constexpr size_t kMp3Slots = 4;
constexpr uint32_t kMp3Marker = 0x7b000000; // OpenDash: non-null "malloc" pointer of an MP3 slot / clip
constexpr uint32_t kVoiceClipMarker = kMp3Marker + 0x100; // ... of the voice clip (past the slots' markers)
struct Mp3Slots {
    StateArray<Mp3Slot, kMp3Slots> slots{};

    void clearBytes() { slots.fill(Mp3Slot{}); }
};
inline Mp3Slots& mp3Slots() {
    static Mp3Slots s;
    return s;
}

// The voice clip (one stream): its stream, its volume and fade step, its data marker, the fade running
struct VoiceClipState {
    uint32_t stream = 0xffffffffu; // kNoStream: none (the start)
    int32_t volume = 0;
    int32_t step = 0;
    uint32_t data = 0;
    uint32_t fading = 0;
};
inline VoiceClipState& voiceClip() {
    static VoiceClipState s;
    return s;
}

// ---- requests (game code side) -------------------------------------------------------------------------------
int soundPlayFlat(uint16_t id);                                // exe 0x5d51f0: a flat sound (0: queued, -1 full)
int soundPlayFlatForced(uint16_t id);                          // exe 0x5d5260: ... the queue's oldest dropped if full
// The interface sounds (the message windows, the choices, the list cursors, the menus)
enum class UiSound : uint16_t {
    Cursor = 0x80,      // the hand / the cursor moved
    Confirm = 0x81,     // circle / start on a choice
    Cancel = 0x82,      // cross on a choice
    Typing = 0x84,      // a glyph typed (once a tick)
    ItemJingle = 0xc0,  // the item get's jingle
};
inline int soundPlayFlat(UiSound s) { return soundPlayFlat(uint16_t(s)); }
int soundPlayMusicIfIdle(uint16_t id);                         // exe 0x5d52a0: a music unless its channel plays
// exe 0x5d52f0 (the music request): `channelArg` goes to the channel start (exe 0x40dd00), which does not read it;
// `fadeEntry` is the fade entry the flags (kMusicRequest*) set
int soundPlayMusicWithFade(uint16_t id, uint16_t channelArg, uint16_t fadeEntry, uint32_t flags);
int soundPlayPanned(uint16_t id, int16_t pan, int16_t atten);   // exe 0x5d5340: an explicit pan / attenuation
int soundPlayAtPos(uint16_t id, uint32_t pos32);               // exe 0x5d5380 without a point (pos32 0: the listener)
constexpr uint32_t kSoundAtListener = 0; // soundPlayAtPos / soundPlayAtPosS16's point: none (the listener's)
// ... at a 16.16 position (an actor's)
int soundPlayAtPos(uint16_t id, const Fixed16Vec3& pos);
// exe 0x5d5380 with a stack vector: the integer parts (x, y, z) given directly
int soundPlayAtXyz(uint16_t id, int16_t x, int16_t y, int16_t z);
int soundPlayAtPlayer(uint16_t id);                            // exe 0x5d53e0: at the player (or the listener's)
int soundPlayAtPosS16(uint16_t id, uint32_t pos16);            // exe 0x5d5420 without a point (pos16 0: the listener)
// ... at an s16 point (an effect's position)
int soundPlayAtPosS16(uint16_t id, const ShortVec3& pos);
uint32_t soundStartLoop(uint16_t id, uint32_t pos32, uint16_t param);      // exe 0x5d5480: a loop sound (its handle)
// ... at a 16.16 point of the native state (an actor's position)
uint32_t soundStartLoop(uint16_t id, const Fixed16Vec3& pos, uint16_t param);
uint32_t soundStartLoopAtPlayer(uint16_t id, uint16_t param);             // exe 0x5d5580
int soundUpdateLoop(uint32_t handle, uint32_t pos32, uint16_t param);     // exe 0x5d55c0: kept alive, moved
int soundUpdateLoop(uint32_t handle, const Fixed16Vec3& pos, uint16_t param);
// exe 0x5d5480 / 0x5d55c0 with a stack vector: the 16.16 point's integer parts (x, y, z) given directly
uint32_t soundStartLoopAtXyz(uint16_t id, int16_t x, int16_t y, int16_t z, uint16_t param);
int soundUpdateLoopAtXyz(uint32_t handle, int16_t x, int16_t y, int16_t z, uint16_t param);
int soundUpdateLoopAtPlayer(uint32_t handle, uint16_t param);             // exe 0x5d56b0
void soundStopLoop(uint32_t handle);                                      // exe 0x5d56f0
int soundStopAllLoops();                                                    // exe 0x5d5760 (the stop-loops request)
void soundQueueClear();                                                     // exe 0x5d51c0
void soundQueuePopFront();                                                  // exe 0x5d4ec0
// fades (mask = 1 << fade entry: 0..3 music channels, 4 = SFX group)
int soundFadeOut(uint32_t mask, int16_t step, int16_t delay);             // exe 0x5d5780
int soundFadeIn(uint32_t mask, int16_t step, int16_t delay);              // exe 0x5d57e0
int soundFadeDip(uint32_t mask, int16_t step, int16_t target, int16_t delay); // exe 0x5d5830
int soundFadeRestore(uint32_t mask, int16_t step, int16_t delay);         // exe 0x5d58a0
bool soundDone(uint32_t mask);                                      // exe 0x5d5900
// the fades of `mask` marked done (OpenDash: a host start with no music playing, the bits soundDone waits for)
void soundMarkFadesDone(uint32_t mask);

// a music channel plays (its bit of the channels' playing mask)
bool musicChannelPlaying(uint32_t channel);
// exe 0x5d3cf0: the stage's music (0xff: none) and its sound flags; the loop voices dropped
void stageSoundStart(uint32_t musicId, uint32_t flags);
// exe 0x5d3d40: the stage's sound flags set (and its battle music, with kSoundStageBattleMusic)
void stageSoundFlagsSet(uint32_t flags);
void soundSetListener(ListenerRef pos, ListenerRef rot, uint8_t format);        // exe 0x5d5a60: the listener's pointers
void soundStopAll();                                                        // exe 0x5d5920
void soundStopSfxVoices();                                                  // exe 0x5d59b0
void soundStopSfxVoicesKeep18();                                            // exe 0x5d5a00 (voice 0x12 kept)
void soundPauseAll();                                                       // exe 0x5d5130
void soundResumeAll();                                                      // exe 0x5d5160
void soundApplyBgmMaster();                                                 // exe 0x5d5190
void soundReset();                                                          // exe 0x5d3b60: the sound system reset
void stopMusicChannel(uint32_t ch);                                       // exe 0x40dd70
void stopSfxVoice(uint32_t voice);                                        // exe 0x5d44d0

// ---- data ---------------------------------------------------------------------------------------------------
// exe 0x5d5010: a DAT type-8 segment into bank slot `slot` (copied to kSfxBankMem + slot * stride), one backend buffer
// per entry with a sample (the entry's handle)
void loadSfxBank(std::span<const uint8_t> seg, uint32_t slot, uint32_t bankId);
void unloadSfxBank(uint32_t slot);                                        // exe 0x5d4f40
// exe 0x5d50c0: a type-9 segment into MP3 slot `slot` (its stream created by the backend)
void loadMp3Slot(std::span<const uint8_t> seg, uint32_t slot, uint32_t loop);
void unloadMp3Slot(uint32_t slot);                                        // exe 0x5d4fd0

// ---- voice clips (snd/xa*.dat) -------------------------------------------------------------------------------
void voiceClipPlay(uint32_t id);       // exe 0x601050
void voiceClipFadeOut(uint32_t step);  // exe 0x601220
bool voiceClipPlaying();                 // exe 0x601240
inline bool voiceClipIdle() { return !voiceClipPlaying(); } // exe 0x601260
void voiceClipPause();                   // exe 0x601200 / 0x601280 (stream pause)
void voiceClipResume();                  // exe 0x6012a0
void voiceClipStop();                    // exe 0x601010

// exe 0x54a570's loop (0x54a683): the 64-step sine tables = every 64th {s16, s16} of the exe's sine table
void buildSin64Tables();
// test hooks: the pan math on a scratch voice (voice 0): out = {dist, its pan, its volume}
void soundPanProbe(int vol, int sx, int sy, int sz, int out[3]);     // exe 0x5d4db0 + 0x5d4b80
void soundExplicitPanProbe(int vol, int pan, int atten, int out[3]); // exe 0x5d4960

// exe 0x5d3d60: the frame's sound: the voice-clip fade, the queue drained, the loop voices, the fades
void soundUpdate();

} // namespace od::port
