// A player's state on the network (see net_player.hpp).
#include "game/player/net_player.hpp"
#include "game/player/player_downed.hpp"

#include <algorithm>

#include "game/animation_data.hpp"
#include "game/actor_state.hpp"
#include "game/player/player_animation.hpp"
#include "game/player/player_state.hpp"

namespace od::port {

namespace {

constexpr uint32_t kKeyBytes = 4;
constexpr uint16_t kX = 1 << 0, kY = 1 << 1, kZ = 1 << 2, kYaw = 1 << 3, kPitch = 1 << 4, kLean = 1 << 5, kFlags = 1 << 6, kLight = 1 << 7,
                   kMaskStageAnims = 1 << 8, kFace = 1 << 9, kLook = 1 << 10, kChannel0 = 1 << 11, kChannel1 = 1 << 12, kFight = 1 << 13;
constexpr uint8_t kChannelAge = 1, kChannelKey = 2, kChannelHeld = 4;

struct Out {
    std::vector<uint8_t> b;
    void u8(uint8_t v) { b.push_back(v); }
    void u16(uint16_t v) {
        u8(uint8_t(v));
        u8(uint8_t(v >> 8));
    }
    void u32(uint32_t v) {
        u16(uint16_t(v));
        u16(uint16_t(v >> 16));
    }
    void i16(int16_t v) { u16(uint16_t(v)); }
};
struct In {
    std::span<const uint8_t> b;
    size_t at = 0;
    bool ok = true;
    uint8_t u8() {
        if (at >= b.size()) {
            ok = false;
            return 0;
        }
        return b[at++];
    }
    uint16_t u16() {
        const uint16_t lo = u8();
        return uint16_t(lo | uint16_t(u8()) << 8);
    }
    uint32_t u32() {
        const uint32_t lo = u16();
        return lo | uint32_t(u16()) << 16;
    }
    int16_t i16() { return int16_t(u16()); }
};

uint16_t maskOf(const NetPlayerFields& n, const NetPlayerFields& o) {
    uint16_t m = 0;
    if (n.pos[0] != o.pos[0]) m |= kX;
    if (n.pos[1] != o.pos[1]) m |= kY;
    if (n.pos[2] != o.pos[2]) m |= kZ;
    if (n.yaw != o.yaw) m |= kYaw;
    if (n.pitch != o.pitch || n.yawOfs != o.yawOfs) m |= kPitch;
    if (n.lean != o.lean || n.lean2 != o.lean2) m |= kLean;
    if (n.drawFlags != o.drawFlags || n.channelMix != o.channelMix || n.aimFlags != o.aimFlags || n.armed != o.armed ||
        n.weaponSlot != o.weaponSlot || n.busterSlot != o.busterSlot)
        m |= kFlags;
    if (n.light != o.light) m |= kLight;
    if (n.stageAnims != o.stageAnims) m |= kMaskStageAnims;
    if (n.face != o.face) m |= kFace;
    if (!(n.look == o.look)) m |= kLook;
    if (!(n.channels[0] == o.channels[0])) m |= kChannel0;
    if (!(n.channels[1] == o.channels[1])) m |= kChannel1;
    if (n.status != o.status || n.charge != o.charge || n.specialFiring != o.specialFiring || n.downed != o.downed) m |= kFight;
    return m;
}

} // namespace

NetPlayerFields NetPlayerWatch::see(const Player& p, uint32_t tick) {
    NetPlayerFields& f = fields_;
    f.pos = {p.pos.x.whole, p.pos.y.whole, p.pos.z.whole};
    f.pitch = p.rotation.x;
    f.yaw = p.rotation.y;
    f.yawOfs = p.directionOffset.y;
    f.lean = p.lean;
    f.lean2 = p.headTurn;
    f.drawFlags = {p.flags, p.runFlags};
    f.channelMix = p.channelMix;
    f.light = p.ambientColour;
    f.stageAnims = p.stageAnims;
    f.aimFlags = uint8_t(p.armFlags);
    f.armed = uint8_t(p.carryFlags);
    f.weaponSlot = p.activeSpecial;
    f.busterSlot = p.activeBuster;
    f.face = {p.idleFaces[0], p.idleFaces[1]};
    f.look = {p.type, uint8_t(p.helmetPart), uint8_t(p.legPart), p.specialWeapon};
    f.status = uint8_t(p.status);
    f.charge = uint8_t(p.charge);
    f.downed = playerIsDowned(p) ? 1 : 0;
    f.specialFiring = uint8_t(p.specialFiring);
    const std::array<const AnimChannel*, 2> channels = {&p.body, &p.upper};
    for (size_t i = 0; i < 2; ++i) {
        const AnimChannel& ch = *channels[i];
        NetPlayerChannel& out = f.channels[i];
        Place& was = last_[i];
        const uint32_t script = ch.script.value(), keyAt = ch.keyAt.value();
        const uint8_t ticks = ch.key.ticks;
        const auto mark = [&](bool held) {
            out.anim = ch.anim;
            out.tick = tick;
            out.key = uint8_t((keyAt - script) / kKeyBytes);
            out.ticks = ticks;
            out.keyed = true;
            out.held = held;
        };
        if (script != 0) {
            if (was.script != script || was.anim != ch.anim) {
                mark(false);
            } else {
                const bool stayed = keyAt == was.keyAt && ticks == was.ticks;
                bool stepped = false;
                if (was.ticks != 1) {
                    stepped = keyAt == was.keyAt && ticks == uint8_t(was.ticks - 1);
                } else {
                    const AnimationKeyData key = animationKey(was.keyAt);
                    uint32_t next = was.keyAt + kKeyBytes;
                    uint8_t nextTicks = 0;
                    if (key.flags == kKeyHold) {
                        next = was.keyAt;
                        nextTicks = key.ticks;
                    } else if (key.flags & kKeyJump) {
                        next = script + uint32_t(key.flags & 0x7f) * kKeyBytes;
                        nextTicks = animationKey(next).ticks;
                    } else {
                        nextTicks = animationKey(next).ticks;
                    }
                    stepped = keyAt == next && ticks == nextTicks;
                }
                if (stepped) {
                    if (out.held) mark(false);
                } else if (stayed) {
                    if (!out.held) mark(true);
                } else {
                    mark(false);
                }
            }
        }
        was = {script, keyAt, ticks, ch.anim};
    }
    return f;
}

std::vector<uint8_t> NetPlayerSender::update(uint8_t playerId, uint32_t tick, const NetPlayerFields& now) {
    Out o;
    o.u8(playerId);
    const uint16_t sequence = next_++;
    o.u16(sequence);
    o.u32(tick);
    const uint16_t mask = uint16_t(maskOf(now, acked_) | maskOf(now, sent_));
    o.u16(mask);
    if (mask & kX) o.i16(now.pos[0]);
    if (mask & kY) o.i16(now.pos[1]);
    if (mask & kZ) o.i16(now.pos[2]);
    if (mask & kYaw) o.i16(now.yaw);
    if (mask & kPitch) {
        o.i16(now.pitch);
        o.i16(now.yawOfs);
    }
    if (mask & kLean) {
        o.i16(now.lean);
        o.i16(now.lean2);
    }
    if (mask & kFlags) {
        o.u8(now.drawFlags[0]);
        o.u8(now.drawFlags[1]);
        o.u8(uint8_t(now.channelMix));
        o.u8(now.aimFlags);
        o.u8(now.armed);
        o.u8(uint8_t(now.weaponSlot));
        o.u8(uint8_t(now.busterSlot));
    }
    if (mask & kLight) o.u32(now.light);
    if (mask & kMaskStageAnims) o.u32(now.stageAnims);
    if (mask & kFace) {
        o.u8(uint8_t(now.face[0]));
        o.u8(uint8_t(now.face[1]));
    }
    if (mask & kLook) {
        o.u8(now.look.kind);
        o.u8(now.look.costume);
        o.u8(now.look.costumeVariant);
        o.u8(uint8_t(now.look.specialWeapon));
    }
    for (size_t i = 0; i < 2; ++i) {
        if (!(mask & (i == 0 ? kChannel0 : kChannel1))) continue;
        const NetPlayerChannel& c = now.channels[i];
        const uint32_t age = c.tick == kNetPlayerNoTick ? 0 : std::min<uint32_t>(tick - c.tick, 0xff);
        const uint8_t bits = uint8_t((age != 0 ? kChannelAge : 0) | (c.keyed ? kChannelKey : 0) | (c.held ? kChannelHeld : 0));
        o.u8(c.anim);
        o.u8(bits);
        if (bits & kChannelAge) o.u8(uint8_t(age));
        if (bits & kChannelKey) {
            o.u8(c.key);
            o.u8(c.ticks);
        }
    }
    if (mask & kFight) {
        o.u8(now.status);
        o.u8(now.charge);
        o.u8(now.downed);
        o.u8(now.specialFiring);
    }
    inFlight_.push_back({sequence, now});
    while (inFlight_.size() > 64) inFlight_.pop_front();
    sent_ = now;
    return std::move(o.b);
}

void NetPlayerSender::acknowledge(uint16_t sequence) {
    while (!inFlight_.empty() && int16_t(uint16_t(inFlight_.front().sequence - sequence)) <= 0) {
        if (inFlight_.front().sequence == sequence) acked_ = inFlight_.front().fields;
        inFlight_.pop_front();
    }
}

std::optional<uint8_t> netPlayerIdOf(std::span<const uint8_t> bytes) {
    return bytes.empty() ? std::nullopt : std::optional(bytes[0]);
}

std::optional<NetPlayerHeard> NetPlayerReceiver::apply(std::span<const uint8_t> bytes) {
    In r{bytes};
    NetPlayerHeard h;
    h.playerId = r.u8();
    h.sequence = r.u16();
    h.tick = r.u32();
    const uint16_t mask = r.u16();
    if (!r.ok || (any_ && int16_t(uint16_t(h.sequence - last_)) <= 0)) return std::nullopt;
    NetPlayerFields f = fields_;
    if (mask & kX) f.pos[0] = r.i16();
    if (mask & kY) f.pos[1] = r.i16();
    if (mask & kZ) f.pos[2] = r.i16();
    if (mask & kYaw) f.yaw = r.i16();
    if (mask & kPitch) {
        f.pitch = r.i16();
        f.yawOfs = r.i16();
    }
    if (mask & kLean) {
        f.lean = r.i16();
        f.lean2 = r.i16();
    }
    if (mask & kFlags) {
        f.drawFlags[0] = r.u8();
        f.drawFlags[1] = r.u8();
        f.channelMix = int8_t(r.u8());
        f.aimFlags = r.u8();
        f.armed = r.u8();
        f.weaponSlot = int8_t(r.u8());
        f.busterSlot = int8_t(r.u8());
    }
    if (mask & kLight) f.light = r.u32();
    if (mask & kMaskStageAnims) f.stageAnims = r.u32();
    if (mask & kFace) {
        f.face[0] = int8_t(r.u8());
        f.face[1] = int8_t(r.u8());
    }
    if (mask & kLook) {
        f.look.kind = r.u8();
        f.look.costume = r.u8();
        f.look.costumeVariant = r.u8();
        f.look.specialWeapon = int8_t(r.u8());
    }
    for (size_t i = 0; i < 2; ++i) {
        if (!(mask & (i == 0 ? kChannel0 : kChannel1))) continue;
        NetPlayerChannel& c = f.channels[i];
        c.anim = r.u8();
        const uint8_t bits = r.u8();
        const uint32_t age = (bits & kChannelAge) ? r.u8() : 0u;
        c.tick = h.tick - age;
        c.keyed = (bits & kChannelKey) != 0;
        c.held = (bits & kChannelHeld) != 0;
        if (c.keyed) {
            c.key = r.u8();
            c.ticks = r.u8();
        }
    }
    if (mask & kFight) {
        f.status = r.u8();
        f.charge = r.u8();
        f.downed = r.u8();
        f.specialFiring = r.u8();
    }
    if (!r.ok || r.at != bytes.size()) return std::nullopt;
    fields_ = f;
    last_ = h.sequence;
    any_ = true;
    return h;
}

void NetPlayerTrack::enterKey(uint32_t key) {
    keyAt_ = key;
    const AnimationKeyData data = animationKey(key);
    frame_ = data.frame;
    ticks_ = data.ticks;
    if ((data.flags & kKeyBlendMask) == kKeyBlend) {
        blendSteps_ = uint8_t((data.flags & 0xf) | 0x80);
        uint32_t p = key;
        do p += kKeyBytes;
        while (animationKey(p).frame == data.frame);
        blendFrame_ = animationKey(p).frame;
    }
}

void NetPlayerTrack::stepOnce() {
    if (held_ || script_ == 0) return;
    ticks_ = uint8_t(ticks_ - 1u);
    if (ticks_ != 0) return;
    const AnimationKeyData key = animationKey(keyAt_);
    if (key.flags & kKeyJump) {
        if (key.flags == kKeyHold) {
            ticks_ = key.ticks;
            return;
        }
        blendSteps_ = 0;
        keyAt_ = script_ + uint32_t(key.flags & 0x7f) * kKeyBytes;
        const AnimationKeyData target = animationKey(keyAt_);
        frame_ = target.frame;
        ticks_ = target.ticks;
        return;
    }
    enterKey(keyAt_ + kKeyBytes);
}

void NetPlayerTrack::step(const NetPlayerChannel& channel, uint32_t stageAnims, uint32_t tick) {
    if (channel.tick == kNetPlayerNoTick) return;
    if (channel.tick != started_ && int32_t(channel.tick - tick) <= 0) {
        started_ = channel.tick;
        anim_ = channel.anim;
        script_ = playerAnimScriptAddress(stageAnims, channel.anim);
        frameData_ = loadedScalar<uint8_t>(script_ - kKeyBytes);
        blendSteps_ = 0;
        blendFrame_ = 0;
        enterKey(script_ + uint32_t(channel.keyed ? channel.key : uint8_t(0)) * kKeyBytes);
        if (channel.keyed) ticks_ = channel.ticks;
        held_ = channel.held;
        for (uint32_t i = 0, n = std::min<uint32_t>(tick - channel.tick, 512); i < n; ++i) stepOnce();
        return;
    }
    stepOnce();
}

PlayerSnapshot netPlayerSnapshot(const NetPlayerFields& f, const std::array<NetPlayerTrack, 2>& tracks) {
    PlayerSnapshot s;
    s.look = f.look;
    s.drawFlags[0] = f.drawFlags[0];
    s.drawFlags[1] = f.drawFlags[1];
    for (size_t i = 0; i < 3; ++i) s.pos[i] = int32_t(uint32_t(uint16_t(f.pos[i])) << 16);
    s.pitch = f.pitch;
    s.yaw = f.yaw;
    s.yawOfs = f.yawOfs;
    for (size_t i = 0; i < 2; ++i) {
        s.animId[i] = tracks[i].anim();
        s.animStep[i] = tracks[i].frameData();
        s.animFrame[i] = tracks[i].frame();
        s.blend[i * 2] = uint8_t(tracks[i].blendFrame());
        s.blend[i * 2 + 1] = tracks[i].blendSteps();
        s.face[i] = f.face[i];
    }
    s.channelMix = f.channelMix;
    s.light = f.light;
    s.stageAnimBase = f.stageAnims;
    s.lean = f.lean;
    s.lean2 = f.lean2;
    s.aimFlags = f.aimFlags;
    s.armed = f.armed;
    s.weaponSlot = f.weaponSlot;
    s.busterSlot = f.busterSlot;
    return s;
}

} // namespace od::port
