// The sound system's native state in the exe's layout (game/audio_logic): the request queue, the context, the voices,
// the loops, the music channels, the fades, the banks, the MP3 slots, the voice clip, the voice tables and the
// listener's pointers.
#include <stdexcept>

#include "game/audio_logic.hpp"
#include "game/player/player_state.hpp"
#include "translator/layout.hpp"

namespace od::test {

using namespace od::port;

namespace {

// the camera block's eye and view angles (the translator's layout of game/camera_state), the player's position and
// rotation (its layout: player_layout)
constexpr uint32_t kCameraEye = 0xa6bca0 + 0x2c, kCameraAngles = 0xa6bca0 + 0x7c;
constexpr uint32_t kPlayerPosition = kPlayer + 0x10, kPlayerRotation = kPlayer + 0x28;

void listenerRef(LayoutVisitor& v, uint32_t address, ListenerRef& r) {
    uint32_t a = 0;
    switch (r.kind) {
    case ListenerRef::Kind::None: a = 0; break;
    case ListenerRef::Kind::CameraEye: a = kCameraEye; break;
    case ListenerRef::Kind::CameraAngles: a = kCameraAngles; break;
    case ListenerRef::Kind::PlayerPosition: a = kPlayerPosition; break;
    case ListenerRef::Kind::PlayerRotation: a = kPlayerRotation; break;
    }
    v.bytes(address, &a, 4);
    if (a == 0) r = ListenerRef::none();
    else if (a == kCameraEye) r = ListenerRef::cameraEye();
    else if (a == kCameraAngles) r = ListenerRef::cameraAngles();
    else if (a == kPlayerPosition) r = ListenerRef::playerPosition();
    else if (a == kPlayerRotation) r = ListenerRef::playerRotation();
    else throw std::runtime_error("translator: a sound listener pointer at no point the native state names");
}

void request(LayoutVisitor& v, uint32_t a, SoundRequest& r) {
    v.field(a + 0x0, r.kind);
    v.field(a + 0x2, r.id);
    v.field(a + 0x4, r.unread04);
    v.field(a + 0x8, r.x);
    v.field(a + 0xa, r.y);
    v.field(a + 0xc, r.z);
    v.field(a + 0xe, r.mode);
    v.field(a + 0xf, r.modeHigh);
}

} // namespace

void audioLayout(LayoutVisitor& v) {
    SoundListener& l = soundListener();
    listenerRef(v, 0xa6d2fc, l.position);
    listenerRef(v, 0xa79fa4, l.rotation);

    SoundQueue& q = soundQueue();
    for (size_t i = 0; i < kQueueEntries; ++i) request(v, 0xa676c0 + uint32_t(i) * 16, q.entries[i]);
    v.field(0xa7223c, q.count);
    v.field(0xa6d33c, q.flags);

    SoundContext& c = soundContext();
    v.field(0xa71760, c.head);
    v.field(0xa71764, c.resetVolumes);
    v.field(0xa7176c, c.unread0c);
    v.field(0xa71790, c.masters.sfx);
    v.field(0xa71791, c.masters.bgm);
    v.field(0xa71792, c.unread32);
    v.field(0xa71793, c.listenerFormat);
    v.field(0xa71794, c.fadeVolumes);
    v.field(0xa717a0, c.unread40);
    v.field(0xa717a2, c.stageFlags);
    v.field(0xa717a3, c.battleMusic);
    request(v, 0xa717a4, c.request);

    v.field(0xa676a0, soundLoopState().holdLoops);
    v.field(0xa6ab60, voiceTables().stage);
    v.field(0xa72240, voiceTables().second);

    SoundVoices& sv = soundVoices();
    for (size_t i = 0; i < kVoices; ++i) {
        SfxVoice& s = sv.voices[i];
        const uint32_t a = 0xa71580 + uint32_t(i) * 16;
        v.field(a + 0x0, s.active);
        v.field(a + 0x1, s.entry);
        v.field(a + 0x2, s.frequency);
        v.field(a + 0x4, s.priority);
        v.field(a + 0x5, s.channel);
        v.field(a + 0x6, s.distance);
        v.field(a + 0x8, s.pan);
        v.field(a + 0x9, s.volume);
        v.field(a + 0xa, s.playing);
        v.field(a + 0xb, s.loop);
        v.field(a + 0xc, s.buffer);
    }

    LoopVoices& lv = loopVoices();
    for (size_t i = 0; i < kLoopVoices; ++i) {
        LoopVoice& s = lv.voices[i];
        const uint32_t a = 0xa71140 + uint32_t(i) * 12;
        v.field(a + 0x0, s.keep);
        v.field(a + 0x1, s.voice);
        v.field(a + 0x2, s.volume);
        v.field(a + 0x3, s.mode);
        v.field(a + 0x4, s.point);
        v.field(a + 0xa, s.pitch);
    }
    v.field(0xa72598, lv.allocated);
    v.field(0xa6c3a0, lv.sounding);

    MusicState& m = musicState();
    for (size_t i = 0; i < kMusicChannels; ++i) {
        MusicChannel& s = m.channels[i];
        const uint32_t a = 0xa6bc60 + uint32_t(i) * 8;
        v.field(a + 0x0, s.index);
        v.field(a + 0x1, s.priority);
        v.field(a + 0x2, s.loop);
        v.field(a + 0x3, s.unread03);
        v.field(a + 0x4, s.stream);
    }
    v.field(0xa6d340, m.playing);
    v.field(0xa72594, m.done);
    v.field(0x8886d0, m.currentStream);

    SoundFades& f = soundFades();
    for (size_t i = 0; i < kFades; ++i) {
        SoundFade& s = f.fades[i];
        const uint32_t a = 0xa717c0 + uint32_t(i) * 12;
        v.field(a + 0x0, s.active);
        v.field(a + 0x1, s.channel);
        v.field(a + 0x2, s.type);
        v.field(a + 0x3, s.state);
        v.field(a + 0x4, s.step);
        v.field(a + 0x6, s.target);
        v.field(a + 0x8, s.delay);
        v.field(a + 0xa, s.unread0a);
    }

    SfxBanks& b = sfxBanks();
    for (size_t i = 0; i < kBanks; ++i) {
        const uint32_t a = 0xa73c60 + uint32_t(i) * 16;
        v.field(a + 0x0, b.banks[i].bankId);
        v.field(a + 0x4, b.banks[i].data);
        v.field(a + 0x8, b.banks[i].entries);
        v.field(a + 0xc, b.banks[i].samples);
    }

    Mp3Slots& mp = mp3Slots();
    for (size_t i = 0; i < kMp3Slots; ++i) {
        const uint32_t a = 0xa6e360 + uint32_t(i) * 16;
        v.field(a + 0x0, mp.slots[i].marker);
        v.field(a + 0x4, mp.slots[i].size);
        v.field(a + 0x8, mp.slots[i].loop);
        v.field(a + 0xc, mp.slots[i].stream);
    }

    VoiceClipState& vc = voiceClip();
    v.field(0x91be3c, vc.stream);
    v.field(0xa5eed0, vc.volume);
    v.field(0xa5eed4, vc.step);
    v.field(0xa5eed8, vc.data);
    v.field(0xa5eedc, vc.fading);
}

} // namespace od::test
