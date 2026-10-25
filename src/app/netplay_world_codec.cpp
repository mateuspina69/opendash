// The shared world's payloads (see netplay_world_codec.hpp).
#include "app/netplay_world_codec.hpp"

#include <algorithm>

#include "net/wire.hpp"

namespace od::app {

namespace {

using od::net::ByteReader;
using od::net::ByteWriter;

void writeRoom(ByteWriter& w, const od::port::GhostRoom& r) {
    w.u8(r.stage);
    w.u8(r.sub);
    w.boolean(r.fieldPlay);
}
od::port::GhostRoom readRoom(ByteReader& r) {
    od::port::GhostRoom g;
    g.stage = r.u8();
    g.sub = r.u8();
    g.fieldPlay = r.boolean();
    return g;
}
template <size_t N> void writeShorts(ByteWriter& w, const int16_t (&v)[N]) {
    for (int16_t x : v) w.i16(x);
}
template <size_t N> void readShorts(ByteReader& r, int16_t (&v)[N]) {
    for (int16_t& x : v) x = r.i16();
}

void writePuppet(ByteWriter& w, const WorldPuppet& p) {
    const od::port::PuppetState& s = p.state;
    w.u8(p.generation);
    w.u8(s.hostSlot);
    w.u8(s.pool);
    w.u16(s.routine);
    w.u32(s.typeWord);
    w.u32(s.paramsWord);
    w.u8(s.loopFlags);
    w.u8(s.flags);
    w.u8(s.runFlags);
    for (int32_t v : s.pos) w.i32(v);
    writeShorts(w, s.rotation);
    w.i16(s.hitPoints);
    w.i16(s.maxHitPoints);
    w.u8(s.anim);
    w.u8(s.keyIndex);
    w.u8(s.shownParts);
    w.u16(s.tint);
    writeShorts(w, s.scale);
    w.u8(s.hitCount);
    for (int k = 0; k < s.hitCount; ++k) {
        const od::port::PuppetHitEntry& e = s.hits[k];
        w.boolean(e.capsule);
        writeShorts(w, e.a);
        if (e.capsule) {
            writeShorts(w, e.b);
            w.u16(e.ra);
            w.u16(e.rb);
        }
        w.u32(e.ownerShape);
        w.u32(e.attack);
    }
}
WorldPuppet readPuppet(ByteReader& r) {
    WorldPuppet p;
    od::port::PuppetState& s = p.state;
    p.generation = r.u8();
    s.hostSlot = r.u8();
    s.pool = r.u8();
    s.routine = r.u16();
    s.typeWord = r.u32();
    s.paramsWord = r.u32();
    s.loopFlags = r.u8();
    s.flags = r.u8();
    s.runFlags = r.u8();
    for (int32_t& v : s.pos) v = r.i32();
    readShorts(r, s.rotation);
    s.hitPoints = r.i16();
    s.maxHitPoints = r.i16();
    s.anim = r.u8();
    s.keyIndex = r.u8();
    s.shownParts = r.u8();
    s.tint = r.u16();
    readShorts(r, s.scale);
    s.hitCount = r.u8();
    if (s.hitCount > od::port::kPuppetHitEntries) r.fail();
    for (int k = 0; k < s.hitCount && r.ok(); ++k) {
        od::port::PuppetHitEntry& e = s.hits[k];
        e.capsule = r.boolean();
        readShorts(r, e.a);
        if (e.capsule) {
            readShorts(r, e.b);
            e.ra = r.u16();
            e.rb = r.u16();
        }
        e.ownerShape = r.u32();
        e.attack = r.u32();
    }
    if (s.pool != 0x20 && s.pool != 0x40) r.fail();
    return p;
}

constexpr size_t kPartHeader = 3 + 8 + 4 + 1 + 1 + 1; // room, fingerprint, tick, part, parts, count
// the most a puppet takes: every hit entry a capsule
size_t puppetBytes() {
    WorldPuppet p;
    p.state.hitCount = od::port::kPuppetHitEntries;
    for (od::port::PuppetHitEntry& e : p.state.hits) e.capsule = true;
    ByteWriter w;
    writePuppet(w, p);
    return w.bytes().size();
}

} // namespace

std::vector<std::vector<uint8_t>> encodeWorld(const od::port::GhostRoom& room, uint64_t fingerprint, uint32_t tick,
                                              std::vector<WorldPuppet> puppets,
                                              const std::vector<std::array<int32_t, 3>>& players, size_t maxBytes,
                                              bool* trimmed) {
    const size_t perPart = std::min<size_t>(255, (maxBytes - kPartHeader) / puppetBytes());
    const size_t room_ = perPart * kMaxWorldParts;
    if (trimmed) *trimmed = puppets.size() > room_;
    if (puppets.size() > room_) { // the nearest to a player first
        auto nearest = [&](const WorldPuppet& p) {
            int64_t best = INT64_MAX;
            for (const auto& pl : players) {
                int64_t d = 0;
                for (int k = 0; k < 3; ++k) {
                    const int64_t v = (int64_t(p.state.pos[k]) - pl[size_t(k)]) >> 16;
                    d += v * v;
                }
                best = std::min(best, d);
            }
            return best;
        };
        std::stable_sort(puppets.begin(), puppets.end(),
                         [&](const WorldPuppet& a, const WorldPuppet& b) { return nearest(a) < nearest(b); });
        puppets.resize(room_);
    }
    const size_t parts = std::max<size_t>(1, (puppets.size() + perPart - 1) / perPart);
    std::vector<std::vector<uint8_t>> out;
    for (size_t part = 0; part < parts; ++part) {
        ByteWriter w;
        writeRoom(w, room);
        w.u64(fingerprint);
        w.u32(tick);
        w.u8(uint8_t(part));
        w.u8(uint8_t(parts));
        const size_t first = part * perPart, last = std::min(puppets.size(), first + perPart);
        w.u8(uint8_t(last - first));
        for (size_t i = first; i < last; ++i) writePuppet(w, puppets[i]);
        out.push_back(w.take());
    }
    return out;
}

std::optional<WorldPart> decodeWorld(std::span<const uint8_t> payload) {
    ByteReader r(payload);
    WorldPart p;
    p.room = readRoom(r);
    p.fingerprint = r.u64();
    p.tick = r.u32();
    p.part = r.u8();
    p.parts = r.u8();
    const uint8_t n = r.u8();
    if (p.parts == 0 || p.parts > kMaxWorldParts || p.part >= p.parts) return std::nullopt;
    for (uint8_t i = 0; i < n && r.ok(); ++i) p.puppets.push_back(readPuppet(r));
    if (!r.done()) return std::nullopt;
    return p;
}

std::vector<uint8_t> encodeEvent(const NetEvent& e) {
    ByteWriter w;
    w.u8(uint8_t(e.kind));
    writeRoom(w, e.room);
    switch (e.kind) {
    case EventKind::HitClaim: {
        const HitClaim& c = e.claim;
        w.u8(uint8_t(c.target));
        w.u8(c.hostSlot);
        w.u8(c.generation);
        w.u8(c.playerId);
        w.u32(c.wallResult);
        w.u32(c.attack);
        writeShorts(w, c.contact);
        w.u8(c.weapon);
        writeShorts(w, c.origin);
        writeShorts(w, c.dir);
        w.u32(c.tick);
        break;
    }
    case EventKind::PlayerHit:
        w.u8(e.hit.attacker);
        w.u32(e.hit.attack);
        writeShorts(w, e.hit.contact);
        break;
    case EventKind::Downed:
    case EventKind::Respawn:
    case EventKind::ScriptStarted:
    case EventKind::ScriptDone: w.u8(e.id); break;
    case EventKind::DropSpawned: {
        const od::port::PickupSpawn& p = e.pickup;
        for (int32_t v : p.pos) w.i32(v);
        w.u8(p.kind);
        w.u8(p.size);
        w.u8(p.variant);
        w.u32(p.velocityXY);
        w.u32(p.velocityZSpare);
        w.i16(p.yaw);
        break;
    }
    case EventKind::ObjectChanged:
        w.u16(e.wall.segment);
        w.u8(e.wall.flags);
        w.i16(e.wall.hitPoints);
        break;
    case EventKind::Travel:
        w.u8(uint8_t(e.travel.stage));
        w.u8(uint8_t(e.travel.sub));
        w.boolean(e.travel.transition);
        w.u8(e.travel.story.story);
        w.u8(e.travel.story.nextStory);
        for (uint8_t b : e.travel.story.flags.bytes) w.u8(b);
        break;
    case EventKind::Settings:
        w.boolean(e.settings.friendlyFire);
        w.u8(uint8_t(e.settings.scale));
        break;
    }
    return w.take();
}

std::optional<NetEvent> decodeEvent(std::span<const uint8_t> payload) {
    ByteReader r(payload);
    NetEvent e;
    const uint8_t kind = r.u8();
    if (kind < uint8_t(EventKind::HitClaim) || kind > uint8_t(EventKind::Settings)) return std::nullopt;
    e.kind = EventKind(kind);
    e.room = readRoom(r);
    switch (e.kind) {
    case EventKind::HitClaim: {
        HitClaim& c = e.claim;
        const uint8_t target = r.u8();
        if (target > uint8_t(ClaimTarget::Wall)) return std::nullopt;
        c.target = ClaimTarget(target);
        c.hostSlot = r.u8();
        c.generation = r.u8();
        c.playerId = r.u8();
        c.wallResult = r.u32();
        c.attack = r.u32();
        readShorts(r, c.contact);
        c.weapon = r.u8();
        readShorts(r, c.origin);
        readShorts(r, c.dir);
        c.tick = r.u32();
        break;
    }
    case EventKind::PlayerHit:
        e.hit.attacker = r.u8();
        e.hit.attack = r.u32();
        readShorts(r, e.hit.contact);
        break;
    case EventKind::Downed:
    case EventKind::Respawn:
    case EventKind::ScriptStarted:
    case EventKind::ScriptDone: e.id = r.u8(); break;
    case EventKind::DropSpawned: {
        od::port::PickupSpawn& p = e.pickup;
        for (int32_t& v : p.pos) v = r.i32();
        p.kind = r.u8();
        p.size = r.u8();
        p.variant = r.u8();
        p.velocityXY = r.u32();
        p.velocityZSpare = r.u32();
        p.yaw = r.i16();
        break;
    }
    case EventKind::ObjectChanged:
        e.wall.segment = r.u16();
        e.wall.flags = r.u8();
        e.wall.hitPoints = r.i16();
        break;
    case EventKind::Travel:
        e.travel.stage = r.u8();
        e.travel.sub = r.u8();
        e.travel.transition = r.boolean();
        e.travel.story.story = r.u8();
        e.travel.story.nextStory = r.u8();
        for (uint8_t& b : e.travel.story.flags.bytes) b = r.u8();
        break;
    case EventKind::Settings:
        e.settings.friendlyFire = r.boolean();
        e.settings.scale = std::clamp(int(r.u8()), 25, 100);
        break;
    }
    if (!r.done()) return std::nullopt;
    return e;
}

} // namespace od::app
