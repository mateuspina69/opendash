// The player's snapshot for posing a remote player (see player_snapshot.hpp).
#include "game/player/player_snapshot.hpp"

#include <type_traits>

#include "engine/data_source.hpp"
#include "game/game_state.hpp"
#include "game/player/player_state.hpp"
#include "game/loaded_data.hpp"

namespace od::port {

namespace {

// the player's two animation channels / blends / draw flag bytes in snapshot order (0 the body, 1 the upper body)
const AnimChannel& channel(const Player& p, uint32_t i) { return i == 0 ? p.body : p.upper; }
AnimChannel& channel(Player& p, uint32_t i) { return i == 0 ? p.body : p.upper; }

// the little-endian layout of RemotePlayerState
class Writer {
public:
    template <class T> void put(T v) {
        const auto u = static_cast<std::make_unsigned_t<T>>(v);
        for (size_t i = 0; i < sizeof(T); ++i) bytes.push_back(uint8_t(uint64_t(u) >> (8 * i)));
    }
    std::vector<uint8_t> bytes;
};
class Reader {
public:
    explicit Reader(std::span<const uint8_t> b) : bytes_(b) {}
    template <class T> T get() {
        std::make_unsigned_t<T> u = 0;
        if (bytes_.size() - pos_ < sizeof(T)) {
            ok_ = false;
            return T{};
        }
        for (size_t i = 0; i < sizeof(T); ++i) u = decltype(u)(u | (uint64_t(bytes_[pos_ + i]) << (8 * i)));
        pos_ += sizeof(T);
        return static_cast<T>(u);
    }
    bool flag() {
        const uint8_t v = get<uint8_t>();
        if (v > 1) ok_ = false;
        return v == 1;
    }
    bool ok() const { return ok_ && pos_ == bytes_.size(); }

private:
    std::span<const uint8_t> bytes_;
    size_t pos_ = 0;
    bool ok_ = true;
};

} // namespace

GhostRoom ghostRoomOf() {
    const GameState& g = gameState();
    return {g.stage, g.room, g.gameMode() == GameMode::Field && g.fieldStep() == FieldStep::Playing};
}

std::vector<uint8_t> encodePlayerState(const RemotePlayerState& r) {
    Writer w;
    const PlayerSnapshot& s = r.snapshot;
    w.put(kRemoteStateLayout);
    w.put(r.room.stage);
    w.put(r.room.sub);
    w.put(uint8_t(r.room.fieldPlay));
    w.put(r.fingerprint);
    w.put(s.look.kind);
    w.put(s.look.costume);
    w.put(s.look.costumeVariant);
    w.put(s.look.specialWeapon);
    for (const uint8_t f : s.drawFlags) w.put(f);
    for (const int32_t p : s.pos) w.put(p);
    w.put(s.pitch);
    w.put(s.yaw);
    w.put(s.yawOfs);
    for (const uint8_t v : s.animId) w.put(v);
    for (const uint8_t v : s.animStep) w.put(v);
    for (const int8_t v : s.animFrame) w.put(v);
    w.put(s.channelMix);
    for (const uint8_t v : s.blend) w.put(v);
    w.put(s.light);
    w.put(s.stageAnimBase);
    w.put(s.lean);
    w.put(s.lean2);
    w.put(s.aimFlags);
    w.put(s.armed);
    w.put(s.weaponSlot);
    w.put(s.busterSlot);
    for (const int8_t v : s.face) w.put(v);
    return std::move(w.bytes);
}

std::optional<RemotePlayerState> decodePlayerState(std::span<const uint8_t> bytes) {
    if (bytes.size() != kRemoteStateBytes) return std::nullopt;
    Reader rd(bytes);
    RemotePlayerState r;
    PlayerSnapshot& s = r.snapshot;
    if (rd.get<uint8_t>() != kRemoteStateLayout) return std::nullopt;
    r.room.stage = rd.get<uint8_t>();
    r.room.sub = rd.get<uint8_t>();
    r.room.fieldPlay = rd.flag();
    r.fingerprint = rd.get<uint64_t>();
    s.look.kind = rd.get<uint8_t>();
    s.look.costume = rd.get<uint8_t>();
    s.look.costumeVariant = rd.get<uint8_t>();
    s.look.specialWeapon = rd.get<int8_t>();
    for (uint8_t& f : s.drawFlags) f = rd.get<uint8_t>();
    for (int32_t& p : s.pos) p = rd.get<int32_t>();
    s.pitch = rd.get<int16_t>();
    s.yaw = rd.get<int16_t>();
    s.yawOfs = rd.get<int16_t>();
    for (uint8_t& v : s.animId) v = rd.get<uint8_t>();
    for (uint8_t& v : s.animStep) v = rd.get<uint8_t>();
    for (int8_t& v : s.animFrame) v = rd.get<int8_t>();
    s.channelMix = rd.get<int8_t>();
    for (uint8_t& v : s.blend) v = rd.get<uint8_t>();
    s.light = rd.get<uint32_t>();
    s.stageAnimBase = rd.get<uint32_t>();
    s.lean = rd.get<int16_t>();
    s.lean2 = rd.get<int16_t>();
    s.aimFlags = rd.get<uint8_t>();
    s.armed = rd.get<uint8_t>();
    s.weaponSlot = rd.get<int8_t>();
    s.busterSlot = rd.get<int8_t>();
    for (int8_t& v : s.face) v = rd.get<int8_t>();
    if (!rd.ok() || r.room.stage >= od::kStageCount) return std::nullopt;
    return r;
}

PlayerSnapshot capturePlayerSnapshot(const Player& p) {
    PlayerSnapshot s{};
    bool unmapped = false;
    s.look.kind = p.type;
    s.look.costume = uint8_t(p.helmetPart);
    s.look.costumeVariant = uint8_t(p.legPart);
    s.look.specialWeapon = p.specialWeapon;
    s.drawFlags[0] = p.flags;
    s.drawFlags[1] = p.runFlags;
    for (uint32_t i = 0; i < 2; ++i) {
        const AnimChannel& ch = channel(p, i);
        s.animId[i] = ch.anim;
        s.animStep[i] = ch.frameData;
        if (ch.keyAt.targetMapped()) s.animFrame[i] = dataScalar<int8_t>(ch.keyAt.value());
        else unmapped = true;
        s.face[i] = p.idleFaces[i];
    }
    s.pos[0] = p.pos.x;
    s.pos[1] = p.pos.y;
    s.pos[2] = p.pos.z;
    s.pitch = p.rotation.x;
    s.yaw = p.rotation.y;
    s.yawOfs = p.directionOffset.y;
    s.channelMix = p.channelMix;
    s.blend[0] = uint8_t(p.bodyBlend.targetFrame);
    s.blend[1] = p.bodyBlend.steps;
    s.blend[2] = uint8_t(p.upperBlend.targetFrame);
    s.blend[3] = p.upperBlend.steps;
    s.light = p.ambientColour;
    s.stageAnimBase = p.stageAnims;
    s.lean = p.lean;
    s.lean2 = p.headTurn;
    s.aimFlags = uint8_t(p.armFlags);
    s.armed = uint8_t(p.carryFlags);
    s.weaponSlot = p.activeSpecial;
    s.busterSlot = p.activeBuster;
    if (unmapped) s.drawFlags[0] = s.drawFlags[1] = 0; // frame pointer not valid yet: not drawable
    return s;
}

void applyPlayerSnapshot(Player& p, uint32_t frameBytes, const PlayerSnapshot& s) {
    p.type = s.look.kind;
    p.helmetPart = int8_t(s.look.costume);
    p.legPart = int8_t(s.look.costumeVariant);
    p.specialWeapon = s.look.specialWeapon;
    p.flags = s.drawFlags[0];
    p.runFlags = s.drawFlags[1];
    // the two frame bytes the channels' key pointers read: a block of the bound loaded files (the remote player's own)
    const uint8_t frames[2] = {uint8_t(s.animFrame[0]), uint8_t(s.animFrame[1])};
    loadedFiles().map(frameBytes, frames);
    for (uint32_t i = 0; i < 2; ++i) {
        AnimChannel& ch = channel(p, i);
        ch.anim = s.animId[i];
        ch.frameData = s.animStep[i];
        ch.keyAt.pointAt(frameBytes + i);
        p.idleFaces[i] = s.face[i];
    }
    p.pos.x = s.pos[0];
    p.pos.y = s.pos[1];
    p.pos.z = s.pos[2];
    p.rotation.x = s.pitch;
    p.rotation.y = s.yaw;
    p.directionOffset.y = s.yawOfs;
    p.channelMix = s.channelMix;
    p.bodyBlend.targetFrame = int8_t(s.blend[0]);
    p.bodyBlend.steps = s.blend[1];
    p.upperBlend.targetFrame = int8_t(s.blend[2]);
    p.upperBlend.steps = s.blend[3];
    p.ambientColour = s.light;
    p.stageAnims = s.stageAnimBase;
    p.lean = s.lean;
    p.headTurn = s.lean2;
    p.armFlags = uint16_t((p.armFlags & 0xff00) | s.aimFlags); // the low byte only (the arm and its mix bits)
    p.carryFlags = uint16_t((p.carryFlags & 0xff00) | s.armed);
    p.activeSpecial = s.weaponSlot;
    p.activeBuster = s.busterSlot;
}

} // namespace od::port
