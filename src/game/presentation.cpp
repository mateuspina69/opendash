// What a session's world shows and plays (see presentation.hpp).
#include "game/presentation.hpp"

#include <map>
#include <utility>

#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/camera_state.hpp"
#include "game/fade.hpp"
#include "game/message.hpp"
#include "game/actor_pools.hpp"
#include "game/actors.hpp"
#include "game/hud.hpp"

namespace od::port {

namespace {

enum class Record : uint8_t { WindowOpen = 1, WindowClose = 2, Fade = 3, Sound = 4, LoopStart = 6, LoopMove = 7, LoopStop = 8, Shake = 9, Banner = 10, White = 11, MusicFade = 12 };
struct HeldLoop {
    uint32_t local = 0;
    bool hasPoint = false;
    int16_t x = 0, y = 0, z = 0;
    uint16_t param = 0;
};
std::map<uint32_t, HeldLoop> g_loops;
bool g_white = false;
std::vector<NetScreenClass> g_screenClasses;
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
void PresentationBatch::loopStart(uint32_t handle, uint16_t id, bool hasPoint, int16_t x, int16_t y, int16_t z, uint16_t param) {
    bytes_.push_back(uint8_t(Record::LoopStart));
    put32(bytes_, handle);
    put16(bytes_, id);
    bytes_.push_back(hasPoint ? 1 : 0);
    put16(bytes_, uint16_t(x));
    put16(bytes_, uint16_t(y));
    put16(bytes_, uint16_t(z));
    put16(bytes_, param);
}
void PresentationBatch::loopMove(uint32_t handle, bool hasPoint, int16_t x, int16_t y, int16_t z, uint16_t param) {
    bytes_.push_back(uint8_t(Record::LoopMove));
    put32(bytes_, handle);
    bytes_.push_back(hasPoint ? 1 : 0);
    put16(bytes_, uint16_t(x));
    put16(bytes_, uint16_t(y));
    put16(bytes_, uint16_t(z));
    put16(bytes_, param);
}
void PresentationBatch::loopStop(uint32_t handle) {
    bytes_.push_back(uint8_t(Record::LoopStop));
    put32(bytes_, handle);
}
void PresentationBatch::shake(uint32_t amount, uint16_t decay, bool force) {
    bytes_.push_back(uint8_t(Record::Shake));
    put32(bytes_, amount);
    put16(bytes_, decay);
    bytes_.push_back(force ? 1 : 0);
}
void PresentationBatch::banner(uint16_t x, uint16_t y, uint8_t style) {
    bytes_.push_back(uint8_t(Record::Banner));
    put16(bytes_, x);
    put16(bytes_, y);
    bytes_.push_back(style);
}
void PresentationBatch::white(bool on) {
    bytes_.push_back(uint8_t(Record::White));
    bytes_.push_back(on ? 1 : 0);
}
void PresentationBatch::musicFade(MusicFade kind, uint32_t mask, int16_t step, int16_t target, int16_t delay) {
    bytes_.push_back(uint8_t(Record::MusicFade));
    bytes_.push_back(uint8_t(kind));
    put16(bytes_, uint16_t(mask));
    put16(bytes_, uint16_t(mask >> 16));
    put16(bytes_, uint16_t(step));
    put16(bytes_, uint16_t(target));
    put16(bytes_, uint16_t(delay));
}
std::vector<uint8_t> PresentationBatch::take() { return std::exchange(bytes_, {}); }

void applyPresentation(std::span<const uint8_t> b, FadeTask& fade, uint8_t me) {
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
        case Record::LoopStart: {
            if (left() < 15) return;
            const uint32_t low = u16_(), high = u16_();
            const uint16_t id = u16_();
            HeldLoop l;
            l.hasPoint = u8() != 0;
            l.x = int16_t(u16_());
            l.y = int16_t(u16_());
            l.z = int16_t(u16_());
            l.param = u16_();
            l.local = l.hasPoint ? soundStartLoopAtXyz(id, l.x, l.y, l.z, l.param) : soundStartLoop(id, kSoundAtListener, l.param);
            g_loops[low | high << 16] = l;
            break;
        }
        case Record::LoopMove: {
            if (left() < 13) return;
            const uint32_t low = u16_(), high = u16_();
            const bool hasPoint = u8() != 0;
            const int16_t x = int16_t(u16_()), y = int16_t(u16_()), z = int16_t(u16_());
            const uint16_t param = u16_();
            if (const auto it = g_loops.find(low | high << 16); it != g_loops.end()) {
                it->second.hasPoint = hasPoint;
                it->second.x = x;
                it->second.y = y;
                it->second.z = z;
                it->second.param = param;
            }
            break;
        }
        case Record::LoopStop: {
            if (left() < 4) return;
            const uint32_t low = u16_(), high = u16_();
            if (const auto it = g_loops.find(low | high << 16); it != g_loops.end()) {
                soundStopLoop(it->second.local);
                g_loops.erase(it);
            }
            break;
        }
        case Record::Shake: {
            if (left() < 7) return;
            const uint32_t low = u16_(), high = u16_();
            const uint16_t decay = u16_();
            cameraShake(u8() != 0, low | high << 16, decay);
            break;
        }
        case Record::Banner: {
            if (left() < 5) return;
            const uint16_t x = u16_(), y = u16_();
            hudBannerShow(x, y, HudBannerStyle(u8()));
            break;
        }
        case Record::White:
            if (left() < 1) return;
            g_white = u8() != 0;
            break;
        case Record::MusicFade: {
            if (left() < 11) return;
            const auto kind = PresentationBatch::MusicFade(u8());
            const uint32_t low = u16_(), high = u16_();
            const uint32_t mask = low | high << 16;
            const int16_t step = int16_t(u16_()), target = int16_t(u16_()), delay = int16_t(u16_());
            switch (kind) {
            case PresentationBatch::MusicFade::Out: soundFadeOut(mask, step, delay); break;
            case PresentationBatch::MusicFade::In: soundFadeIn(mask, step, delay); break;
            case PresentationBatch::MusicFade::Dip: soundFadeDip(mask, step, target, delay); break;
            case PresentationBatch::MusicFade::Restore: soundFadeRestore(mask, step, delay); break;
            default: return;
            }
            break;
        }
        default: return;
        }
    }
}

void presentationLoopsTick() {
    for (auto& [handle, l] : g_loops) {
        if (l.hasPoint) soundUpdateLoopAtXyz(l.local, l.x, l.y, l.z, l.param);
        else soundUpdateLoop(l.local, kSoundAtListener, l.param);
    }
}

void presentationLoopsClear() {
    for (auto& [handle, l] : g_loops) soundStopLoop(l.local);
    g_loops.clear();
    g_white = false;
}

bool presentationWhiteWanted() { return g_white; }

void registerNetScreenClass(const NetScreenClass& c) { g_screenClasses.push_back(c); }

// per controller: the class's index, its type bytes, its state, what the class holds
std::vector<uint8_t> netScreenCapture() {
    std::vector<uint8_t> out;
    for (size_t k = 0; k < g_screenClasses.size(); ++k) {
        const NetScreenClass& cls = g_screenClasses[k];
        for (ControllerActor* c : controllersRunning(cls.cls)) {
            if (!(c->flags & kActorLive)) continue;
            out.push_back(uint8_t(k));
            out.push_back(c->type);
            out.push_back(c->subtype);
            out.push_back(c->variant);
            out.push_back(c->loopFlags);
            out.push_back(c->state);
            const size_t at = out.size();
            out.resize(at + cls.bytes);
            cls.read(*c, out.data() + at);
        }
    }
    return out;
}

void netScreenApply(std::span<const uint8_t> b) {
    // what this game holds of the world's controllers goes, then what the world holds now is back
    ControllerPool& pool = controllerPool();
    for (size_t i = 0; i < ControllerPool::size(); ++i) {
        ControllerActor& c = pool[i];
        if (!(c.flags & kActorLive)) continue;
        for (const NetScreenClass& cls : g_screenClasses) {
            RoutineRef h = actorHandler(c, ActorPool::Controllers, false);
            if (!h) h = actorHandler(c, ActorPool::ControllersLate, false);
            if (h.mapped() && h.is(cls.cls)) c.flags = 0;
        }
    }
    size_t at = 0;
    while (at + 6 <= b.size()) {
        const size_t k = b[at];
        if (k >= g_screenClasses.size() || at + 6 + g_screenClasses[k].bytes > b.size()) return;
        const uint32_t address = allocActorE0();
        if (address == 0) return;
        ControllerActor& c = controllerActorAt(address);
        c.flags = kActorLive;
        c.runFlags = uint8_t(c.runFlags & ~kActorRun);
        c.type = b[at + 1];
        c.subtype = b[at + 2];
        c.variant = b[at + 3];
        c.loopFlags = b[at + 4];
        c.state = b[at + 5];
        g_screenClasses[k].write(c, b.data() + at + 6);
        at += 6 + g_screenClasses[k].bytes;
    }
}

} // namespace od::port
