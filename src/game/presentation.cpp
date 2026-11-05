// What a session's world shows and plays (see presentation.hpp).
#include "game/presentation.hpp"

#include <utility>

#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/camera_state.hpp"
#include "game/fade.hpp"
#include "game/message.hpp"

namespace od::port {

namespace {

enum class Record : uint8_t { WindowOpen = 1, WindowClose = 2, Fade = 3, Sound = 4, Pad = 5 };
PresentationBatch* g_batch = nullptr;

void put16(std::vector<uint8_t>& o, uint16_t v) {
    o.push_back(uint8_t(v));
    o.push_back(uint8_t(v >> 8));
}
void put32(std::vector<uint8_t>& o, uint32_t v) {
    put16(o, uint16_t(v));
    put16(o, uint16_t(v >> 16));
}

} // namespace

void setPresentationBatch(PresentationBatch* batch) { g_batch = batch; }
PresentationBatch* presentationBatch() { return g_batch; }

void PresentationBatch::windowOpened(int8_t win, uint32_t bank, int16_t id) {
    bytes_.push_back(uint8_t(Record::WindowOpen));
    bytes_.push_back(uint8_t(win));
    put32(bytes_, bank);
    put16(bytes_, uint16_t(id));
}
void PresentationBatch::windowClosed(int8_t win, int how) {
    bytes_.push_back(uint8_t(Record::WindowClose));
    bytes_.push_back(uint8_t(win));
    bytes_.push_back(uint8_t(how));
}
void PresentationBatch::fadeStarted(uint32_t request, uint8_t param) {
    bytes_.push_back(uint8_t(Record::Fade));
    put32(bytes_, request);
    bytes_.push_back(param);
}
void PresentationBatch::sound(const SoundRequest& r, uint8_t owner) {
    bytes_.push_back(uint8_t(Record::Sound));
    put16(bytes_, r.kind);
    put16(bytes_, r.id);
    put16(bytes_, uint16_t(r.x));
    put16(bytes_, uint16_t(r.y));
    put16(bytes_, uint16_t(r.z));
    bytes_.push_back(r.mode);
    bytes_.push_back(r.modeHigh);
    bytes_.push_back(owner);
}
void PresentationBatch::pad(uint16_t bits) {
    bytes_.push_back(uint8_t(Record::Pad));
    put16(bytes_, bits);
}
std::vector<uint8_t> PresentationBatch::take() { return std::exchange(bytes_, {}); }

void applyPresentation(std::span<const uint8_t> b, FadeTask& fade, uint8_t me, uint16_t& scenePad) {
    size_t at = 0;
    const auto left = [&] { return b.size() - at; };
    const auto u8 = [&] { return b[at++]; };
    const auto u16_ = [&] {
        const uint16_t v = uint16_t(b[at] | b[at + 1] << 8);
        at += 2;
        return v;
    };
    while (left() > 0) {
        switch (Record(u8())) {
        case Record::WindowOpen: {
            if (left() < 7) return;
            const int8_t win = int8_t(u8());
            const uint32_t low = u16_(), high = u16_();
            windowOpen(win, low | high << 16, int16_t(u16_()));
            break;
        }
        case Record::WindowClose: {
            if (left() < 2) return;
            const int8_t win = int8_t(u8());
            windowClose(win, WindowClose(int(u8())));
            break;
        }
        case Record::Fade: {
            if (left() < 5) return;
            const uint32_t low = u16_(), high = u16_();
            fadeStart(fade, low | high << 16, u8());
            break;
        }
        case Record::Sound: {
            if (left() < 13) return;
            SoundRequest r;
            r.kind = u16_();
            r.id = u16_();
            r.x = int16_t(u16_());
            r.y = int16_t(u16_());
            r.z = int16_t(u16_());
            r.mode = u8();
            r.modeHigh = u8();
            if (u8() != me) soundRequestReplay(r);
            break;
        }
        case Record::Pad:
            if (left() < 2) return;
            scenePad = u16_();
            break;
        default: return;
        }
    }
}

SceneView sceneViewNow() {
    const CameraState& cam = cameraState();
    const CameraViewArgs a = cameraViewArgs();
    return {cam.target.x, cam.target.y, cam.target.z, int16_t(a.distance), int16_t(a.yaw), int16_t(a.pitch), int16_t(a.roll)};
}

void sceneViewShow(const SceneView& v) {
    CameraState& cam = cameraState();
    cam.target.x = v.x;
    cam.target.y = v.y;
    cam.target.z = v.z;
    cameraBuildView(v.distance, v.yaw, v.pitch, uint16_t(v.roll));
}

std::vector<uint8_t> encode(const SceneView& v) {
    std::vector<uint8_t> o;
    put32(o, uint32_t(v.x));
    put32(o, uint32_t(v.y));
    put32(o, uint32_t(v.z));
    put16(o, uint16_t(v.distance));
    put16(o, uint16_t(v.yaw));
    put16(o, uint16_t(v.pitch));
    put16(o, uint16_t(v.roll));
    return o;
}

bool decodeSceneView(std::span<const uint8_t> b, SceneView& v) {
    if (b.size() != 20) return false;
    const auto word = [&](size_t at) { return int32_t(uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24); };
    const auto half = [&](size_t at) { return int16_t(uint16_t(b[at] | b[at + 1] << 8)); };
    v = {word(0), word(4), word(8), half(12), half(14), half(16), half(18)};
    return true;
}

} // namespace od::port
