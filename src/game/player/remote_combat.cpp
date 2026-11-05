// The fight between a world and a player's owner (see remote_combat.hpp).
#include <utility>
#include "game/player/remote_combat.hpp"

#include "game/actor_pools.hpp"
#include "game/player/player_state.hpp"

namespace od::port {

namespace {
std::vector<NetGift> g_gifts;
} // namespace

void noteGift(const NetGift& gift) { g_gifts.push_back(gift); }
std::vector<NetGift> takeGifts() { return std::exchange(g_gifts, {}); }

std::vector<uint8_t> encode(const NetGift& v) {
    return {v.player, uint8_t(v.kind), uint8_t(uint16_t(v.amount)), uint8_t(uint16_t(v.amount) >> 8)};
}

std::optional<NetGift> decodeGift(std::span<const uint8_t> b) {
    if (b.size() != 4 || b[1] > uint8_t(NetGiftKind::SpecialWeapon)) return std::nullopt;
    return NetGift{b[0], NetGiftKind(b[1]), int16_t(uint16_t(b[2] | b[3] << 8))};
}

namespace {

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
    bool done() const { return ok && at == b.size(); }
};

NetHitResult resultOf(const HitBox& box) {
    return {box.result, {box.contact.x, box.contact.y, box.contact.z, box.contactKind}};
}

void intoBox(HitBox& box, const NetHitResult& hit) {
    box.result |= hit.result;
    box.contact.x = hit.contact[0];
    box.contact.y = hit.contact[1];
    box.contact.z = hit.contact[2];
    box.contactKind = hit.contact[3];
}

void put(Out& o, const NetHitResult& v) {
    o.u32(v.result);
    for (const int16_t c : v.contact) o.i16(c);
}
NetHitResult get(In& r) {
    NetHitResult v;
    v.result = r.u32();
    for (int16_t& c : v.contact) c = r.i16();
    return v;
}

} // namespace

std::optional<NetHitResult> takeRemotePlayerHit(const Player& p) {
    if (p.hitBox.result == 0) return std::nullopt;
    return resultOf(p.hitBox);
}

void applyPlayerHit(Player& p, const NetHitResult& hit) {
    if ((p.invulnerable | uint8_t(p.immunity)) != 0) return; // it would not have been in the hit list here
    intoBox(p.hitBox, hit);
}

NetFire netFireOf(const ShotActor& shot, uint8_t clientSlot, uint16_t serial) {
    NetFire f;
    f.clientSlot = clientSlot;
    f.serial = serial;
    f.type = shot.type;
    f.variant = shot.variant;
    f.loopFlags = shot.loopFlags;
    f.flags = uint8_t(shot.flags & (kActorLive | kActorDrawn));
    for (size_t i = 0; i < 4; ++i) f.params[i] = shot.params[i];
    f.aimPoint = {shot.aimPoint.x, shot.aimPoint.y, shot.aimPoint.z, shot.aimPoint.spare};
    f.target = shot.target ? shot.target->slot : uint8_t(0);
    return f;
}

ShotActor* startFiredShot(size_t playerId, const NetFire& fire) {
    const uint32_t address = allocActor10();
    if (address == 0) return nullptr;
    ShotActor& shot = shotActorAt(address);
    shot.ownerPlayer = uint8_t(playerId);
    shot.flags = fire.flags;
    shot.type = fire.type;
    shot.variant = fire.variant;
    shot.loopFlags = fire.loopFlags;
    for (size_t i = 0; i < 4; ++i) shot.params[i] = fire.params[i];
    shot.aimPoint.x = fire.aimPoint[0];
    shot.aimPoint.y = fire.aimPoint[1];
    shot.aimPoint.z = fire.aimPoint[2];
    shot.aimPoint.spare = fire.aimPoint[3];
    shot.target = nullptr;
    if (fire.target != 0) {
        ActorHeader& target = actorHeaderAt(actorBySlot(fire.target));
        if (target.flags & kActorLive) shot.target = &target;
    }
    return &shot;
}

NetPlayerReport netPlayerReportOf(const Player& p) {
    return {p.hitPoints, p.maxHitPoints, (p.invulnerable | uint8_t(p.immunity)) != 0};
}

void applyPlayerReport(Player& p, const NetPlayerReport& report) {
    p.hitPoints = report.hitPoints;
    p.maxHitPoints = report.maxHitPoints;
    p.invulnerable = report.immune ? uint8_t(1) : uint8_t(0);
}

std::vector<uint8_t> netEquipmentOf(const Player& p) {
    Out o;
    o.u8(uint8_t(p.helmetPart));
    o.u8(uint8_t(p.legPart));
    o.u8(uint8_t(p.armourPart));
    o.u8(uint8_t(p.busterWeapon));
    o.u8(uint8_t(p.specialWeapon));
    for (size_t i = 0; i < p.busterParts.size(); ++i) o.u8(uint8_t(p.busterParts[i]));
    for (size_t i = 0; i < p.weaponLevels.size(); ++i) o.u8(uint8_t(p.weaponLevels[i]));
    return std::move(o.b);
}

void applyEquipment(Player& p, std::span<const uint8_t> bytes) {
    if (bytes.size() != netEquipmentOf(p).size()) return;
    In r{bytes};
    p.helmetPart = int8_t(r.u8());
    p.legPart = int8_t(r.u8());
    p.armourPart = int8_t(r.u8());
    p.busterWeapon = int8_t(r.u8());
    p.specialWeapon = int8_t(r.u8());
    for (size_t i = 0; i < p.busterParts.size(); ++i) p.busterParts[i] = int8_t(r.u8());
    for (size_t i = 0; i < p.weaponLevels.size(); ++i) p.weaponLevels[i] = int8_t(r.u8());
}

void applyShotHit(ShotActor& shot, const NetHitResult& hit) { intoBox(shot.hitBox, hit); }

std::vector<uint8_t> encode(const NetHitResult& v) {
    Out o;
    put(o, v);
    return std::move(o.b);
}
std::optional<NetHitResult> decodeHitResult(std::span<const uint8_t> b) {
    In r{b};
    const NetHitResult v = get(r);
    return r.done() ? std::optional(v) : std::nullopt;
}

std::vector<uint8_t> encode(const NetFire& v) {
    Out o;
    o.u8(v.clientSlot);
    o.u16(v.serial);
    o.u8(v.type);
    o.u8(v.variant);
    o.u8(v.loopFlags);
    o.u8(v.flags);
    for (const uint8_t p : v.params) o.u8(p);
    for (const int16_t c : v.aimPoint) o.i16(c);
    o.u8(v.target);
    return std::move(o.b);
}
std::optional<NetFire> decodeFire(std::span<const uint8_t> b) {
    In r{b};
    NetFire v;
    v.clientSlot = r.u8();
    v.serial = r.u16();
    v.type = r.u8();
    v.variant = r.u8();
    v.loopFlags = r.u8();
    v.flags = r.u8();
    for (uint8_t& p : v.params) p = r.u8();
    for (int16_t& c : v.aimPoint) c = r.i16();
    v.target = r.u8();
    return r.done() ? std::optional(v) : std::nullopt;
}

std::vector<uint8_t> encode(const NetPlayerReport& v) {
    Out o;
    o.i16(v.hitPoints);
    o.i16(v.maxHitPoints);
    o.u8(v.immune ? 1 : 0);
    return std::move(o.b);
}
std::optional<NetPlayerReport> decodeReport(std::span<const uint8_t> b) {
    In r{b};
    NetPlayerReport v;
    v.hitPoints = r.i16();
    v.maxHitPoints = r.i16();
    v.immune = r.u8() != 0;
    return r.done() ? std::optional(v) : std::nullopt;
}

std::vector<uint8_t> encode(const NetShotHit& v) {
    Out o;
    o.u8(v.clientSlot);
    o.u16(v.serial);
    put(o, v.hit);
    return std::move(o.b);
}
std::optional<NetShotHit> decodeShotHit(std::span<const uint8_t> b) {
    In r{b};
    NetShotHit v;
    v.clientSlot = r.u8();
    v.serial = r.u16();
    v.hit = get(r);
    return r.done() ? std::optional(v) : std::nullopt;
}

} // namespace od::port
