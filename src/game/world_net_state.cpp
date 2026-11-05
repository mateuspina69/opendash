// The network state of a world's actors (see world_net_state.hpp).
#include "game/world_net_state.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/animation_data.hpp"
#include "game/loaded_data.hpp"
#include "game/model_data.hpp"
#include "game/stage_state.hpp"

namespace od::port {

namespace {

// little-endian writes and reads (game/ does not include net/): a short read fails the message
struct Writer {
    std::vector<uint8_t> bytes;
    void u8(uint8_t v) { bytes.push_back(v); }
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
struct Reader {
    std::span<const uint8_t> bytes;
    size_t at = 0;
    bool ok = true;
    uint8_t u8() {
        if (at >= bytes.size()) {
            ok = false;
            return 0;
        }
        return bytes[at++];
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
    bool done() const { return ok && at == bytes.size(); }
};

constexpr uint8_t kMaskX = 0x01, kMaskY = 0x02, kMaskZ = 0x04, kMaskYaw = 0x08, kMaskRotXZ = 0x10, kMaskAnim = 0x20,
                  kMaskShown = 0x40, kMaskMore = 0x80;
constexpr uint8_t kMoreAge = 0x01, kMoreKey = 0x02, kMoreHeld = 0x04, kMoreScale = 0x08, kMoreTint = 0x10, kMoreParts = 0x20,
                  kMoreAim = 0x40;
constexpr uint8_t kEventAppear = 1, kEventGo = 2, kEventAppearStill = 3; // Still: no animation in play
constexpr size_t kBatchHeaderBytes = 8;
constexpr size_t kAppearBytes = 26, kGoBytes = 2;
constexpr size_t kMaxInFlight = 64;
constexpr size_t kMaxSamples = 32;

bool seqNewer(uint16_t a, uint16_t b) { return int16_t(uint16_t(a - b)) > 0; }

uint8_t animAgeByte(uint32_t tick, uint32_t animTick) {
    return uint8_t(std::min<uint32_t>(tick - animTick, 0xff));
}

} // namespace

bool netSlotVisible(uint8_t slot) {
    return (slot >= kAnimatedClass && slot < kAnimatedClass + kAnimatedFieldSlots) ||
           (slot >= kStaticClass && slot < kStaticClass + StaticPool::size());
}

bool netSlotVisibleOrShot(uint8_t slot) {
    return netSlotVisible(slot) || (slot >= kShotClass && slot < kShotClass + ShotPool::size()) ||
           (slot >= kProjectileClass && slot < kProjectileClass + ProjectilePool::size());
}

// ---- the world's table ----------------------------------------------------------------------------------------------

void NetWorldTable::seen(uint8_t slot, const Seen& s) {
    NetWorldActor& a = actors[slot];
    const bool occupantChanged = !a.live || a.identity.type != s.type || a.model != s.model || a.animTable != s.animTable;
    if (occupantChanged) {
        a = NetWorldActor{true, a.generation + 1, {}, {}, s.bound, s.model, s.animTable, 0, 0, 0};
        a.state.animTick = kNetNoAnimTick;
        ++occupants;
    }
    a.identity = {s.type, s.subtype, s.variant, s.modelEntry};
    const uint8_t previousAnim = a.state.anim;
    const uint32_t previousTick = a.state.animTick;
    a.state.pos = s.pos;
    a.state.rot = s.rot;
    a.state.anim = s.anim;
    a.state.shown = s.shown;
    a.state.visual = s.visual;
    a.state.aim = s.aim;
    // the animation does what stepping does, on the client too, until it does not: then the tick, and where it is
    const auto mark = [&](bool held) {
        a.state.animTick = tick;
        a.state.animKey = s.key;
        a.state.animTicks = s.ticksLeft;
        a.state.animKeyed = !s.atStart;
        a.state.held = held;
    };
    if (s.animScript == 0) {
        a.state.animTick = kNetNoAnimTick;
        a.state.held = false;
    } else if (previousTick == kNetNoAnimTick || previousAnim != s.anim || a.animScript != s.animScript) {
        mark(false);
    } else if (s.stepped) {
        if (a.state.held) mark(false);
    } else if (s.stayed) {
        if (!a.state.held) mark(true);
    } else {
        mark(false);
    }
    a.animScript = s.animScript;
    a.lastKeyAt = s.keyAt;
    a.lastTicks = s.ticksLeft;
}

void NetWorldTable::gone(uint8_t slot) { actors[slot].live = false; }

namespace {

// the entry of the stage's model bank the actor's model was bound from, and what that bind gives
uint8_t modelEntryOf(uint32_t cls, uint32_t model, uint32_t animTable, NetActorVisual& bound) {
    const uint32_t bank = stage().modelBank;
    if (bank == 0 || model == 0) return kNetNoModel;
    const FileDataReader records = loadedFile(bank, 4);
    const int32_t count = records.read<int32_t>(0);
    for (int32_t i = 0; i < count && i < kNetNoModel; ++i) {
        const auto entry = FileDataRef<ModelBankEntryData>(records.sub(4 + size_t(i) * 16)).get();
        if (cls != (entry.key & 0xf0u) || entry.model + bank != model) continue;
        if (cls == kAnimatedClass && entry.animTable + bank != animTable) continue;
        const auto header = FileDataRef<ModelHeaderData>(loadedFile(entry.model + bank, 56)).get();
        bound.scale = {header.scale.x, header.scale.y, header.scale.z};
        bound.tint = kTintNeutral;
        bound.hiddenParts = 0;
        if (cls == kAnimatedClass) {
            const uint32_t slots = std::min<uint32_t>(header.counts & 0xff, 24);
            for (uint32_t k = 0; k < slots; ++k)
                if (modelPart(header.parts + bank, k).flags & kPartHidden) bound.hiddenParts |= 1u << k;
        }
        return uint8_t(i);
    }
    return kNetNoModel;
}

// what classes added to the frames at their last pose, by animated slot
std::array<std::vector<NetAimOffset>, kAnimatedFieldSlots>& poseEdits() {
    static std::array<std::vector<NetAimOffset>, kAnimatedFieldSlots> edits;
    return edits;
}

void notePoseEdits(const AnimatedActor& a) {
    const uint32_t i = AnimatedPool::index(a);
    if (i >= kAnimatedFieldSlots) return;
    std::vector<NetAimOffset>& edits = poseEdits()[i];
    edits.clear();
    FrameAngles frame;
    if (!animFrameAsDecoded(a, frame)) return;
    DecodedFrame& decoded = decodedFrame();
    // entry 0 is the root's offset, which a pose changes in place: no class edits it
    for (uint32_t e = 1; e < frame.count; ++e) {
        const LaneVec4 now = decoded.entries[e];
        const std::array<int16_t, 3> lanes = {int16_t(uint16_t(now.x) - uint16_t(frame.entries[e][0])),
                                              int16_t(uint16_t(now.y) - uint16_t(frame.entries[e][1])),
                                              int16_t(uint16_t(now.z) - uint16_t(frame.entries[e][2]))};
        if (lanes[0] != 0 || lanes[1] != 0 || lanes[2] != 0) edits.push_back({uint8_t(e), lanes});
    }
}

NetWorldTable::Seen seenOf(const Actor& a, uint32_t model, uint32_t animTable) {
    NetWorldTable::Seen s;
    s.type = a.type;
    s.subtype = a.subtype;
    s.variant = a.variant;
    s.model = model;
    s.animTable = animTable;
    s.pos = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
    s.rot = {a.rotation.x, a.rotation.y, a.rotation.z};
    s.shown = (a.flags & kActorDrawn) != 0 && (a.runFlags & kActorShown) != 0;
    return s;
}

template <class Pool> void captureMeshPool(NetWorldTable& table, Pool& pool, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const auto& a = pool[i];
        const uint8_t slot = uint8_t(a.slot);
        if (slot == 0) continue;
        if (!(a.flags & kActorLive)) {
            table.gone(slot);
            continue;
        }
        NetWorldTable::Seen s = seenOf(a, a.model, 0);
        const NetWorldActor& held = table.actors[slot];
        s.modelEntry = held.live && held.model == a.model && held.identity.type == a.type
                           ? held.identity.modelEntry
                           : modelEntryOf(actorSlotClass(slot), a.model, 0, s.bound);
        if (s.modelEntry != kNetNoModel) {
            s.visual.scale = {a.scale.x, a.scale.y, a.scale.z};
            s.visual.tint = a.tint;
        }
        table.seen(slot, s);
    }
}

} // namespace

void watchNetPoses() { setAnimPoseObserver(notePoseEdits); }

void captureNetWorld(NetWorldTable& table, uint32_t tick) {
    table.tick = tick;
    AnimatedPool& animated = animatedPool();
    for (size_t i = 0; i < kAnimatedFieldSlots; ++i) {
        const AnimatedActor& a = animated[i];
        const uint8_t slot = uint8_t(a.slot);
        if (slot == 0) continue;
        if (!(a.flags & kActorLive)) {
            table.gone(slot);
            continue;
        }
        const bool hasModel = a.boneBlocks != 0;
        NetWorldTable::Seen s = seenOf(a, hasModel ? a.model : 0, hasModel ? a.animTable.value() : 0);
        s.anim = a.anim;
        s.animScript = hasModel ? a.animScript.value() : 0;
        const NetWorldActor& held = table.actors[slot];
        s.modelEntry = held.live && held.model == s.model && held.animTable == s.animTable && held.identity.type == a.type
                           ? held.identity.modelEntry
                           : modelEntryOf(kAnimatedClass, s.model, s.animTable, s.bound);
        if (s.modelEntry != kNetNoModel) {
            s.visual.scale = {a.scale.x, a.scale.y, a.scale.z};
            s.visual.tint = a.tint;
            const uint32_t slots = std::min<uint32_t>(a.boneSlotCount, 24);
            for (uint32_t k = 0; k < slots; ++k)
                if (a.parts[k].flags & kPartHidden) s.visual.hiddenParts |= 1u << k;
            s.aim = poseEdits()[i];
        }
        if (s.animScript != 0) {
            s.keyAt = a.animKeyAt.value();
            s.key = uint16_t((s.keyAt - s.animScript) / 4);
            s.ticksLeft = a.animKey.ticks;
            s.atStart = s.key == 0 && a.animKey.ticks == animationKey(s.keyAt).ticks;
            const AnimKeyPlace now{s.keyAt, s.ticksLeft}, last{held.lastKeyAt, held.lastTicks};
            s.stayed = held.lastKeyAt != 0 && now == last;
            s.stepped = held.lastKeyAt != 0 && held.animScript == s.animScript && animStepWouldGive(a, last) == now;
        }
        table.seen(slot, s);
    }
    captureMeshPool(table, staticPool(), StaticPool::size());
    captureMeshPool(table, shotPool(), ShotPool::size());
    captureMeshPool(table, projectilePool(), ProjectilePool::size());
    // the effects only count: they run on the client from their event
    EffectPool& effects = effectPool();
    for (size_t i = 0; i < EffectPool::size(); ++i) {
        const EffectActor& e = effects[i];
        const uint8_t slot = uint8_t(e.slot);
        if (slot == 0) continue;
        if (!(e.flags & kActorLive)) {
            table.gone(slot);
            continue;
        }
        NetWorldTable::Seen s;
        s.type = e.type;
        table.seen(slot, s);
    }
}

// ---- the world's side, per client ---------------------------------------------------------------------------------------

std::vector<std::vector<uint8_t>> NetStateSender::events(const NetWorldTable& table) {
    std::vector<std::vector<uint8_t>> batches;
    Writer w;
    const auto room = [&](size_t bytes) {
        if (!w.bytes.empty() && w.bytes.size() + bytes > kNetMaxEventBatch) {
            batches.push_back(std::move(w.bytes));
            w.bytes.clear();
        }
        if (w.bytes.empty()) {
            w.u32(table.tick);
            w.u32(eventsSent_);
        }
    };
    for (size_t i = 0; i < kNetSlots; ++i) {
        const uint8_t slot = uint8_t(i);
        if (!filter_(slot)) continue;
        Known& k = known_[i];
        const NetWorldActor& a = table.actors[i];
        const bool same = k.told && a.live && a.generation == k.generation;
        if (same) continue;
        if (k.told) {
            room(kGoBytes);
            w.u8(kEventGo);
            w.u8(slot);
            ++eventsSent_;
            k.told = false;
        }
        if (!a.live) continue;
        room(kAppearBytes);
        const bool playing = a.state.animTick != kNetNoAnimTick;
        w.u8(playing ? kEventAppear : kEventAppearStill);
        w.u8(slot);
        w.u8(a.identity.type);
        w.u8(a.identity.subtype);
        w.u8(a.identity.variant);
        w.u8(a.identity.modelEntry);
        w.u8(a.state.shown ? 1 : 0);
        for (const int16_t v : a.state.pos) w.i16(v);
        for (const int16_t v : a.state.rot) w.i16(v);
        if (playing) {
            w.u8(a.state.anim);
            w.u16(uint16_t(std::min<uint32_t>(table.tick - a.state.animTick, kNetAnimNotPlaying - 1)));
            w.u16(a.state.animKeyed ? a.state.animKey : uint16_t(0));
            w.u8(a.state.animKeyed ? a.state.animTicks : uint8_t(0));
            w.u8(uint8_t((a.state.held ? 2 : 0) | (a.state.animKeyed ? 1 : 0)));
        }
        ++eventsSent_;
        k.told = true;
        k.generation = a.generation;
        k.acked = a.state;
        k.acked.visual = a.bound; // a client's bind gives it; a class's change goes in an update
        k.acked.aim.clear();
        k.sent = k.acked;
    }
    if (w.bytes.size() > kBatchHeaderBytes) batches.push_back(std::move(w.bytes));
    return batches;
}

std::vector<uint8_t> NetStateSender::update(const NetWorldTable& table) {
    Writer w;
    const uint16_t sequence = nextSequence_++;
    w.u16(sequence);
    w.u32(table.tick);
    w.u32(eventsSent_);
    Sent sent;
    sent.sequence = sequence;
    bool cut = false;
    const size_t from = cursor_;
    for (size_t n = 0; n < kNetSlots; ++n) {
        const size_t i = (from + n) % kNetSlots;
        Known& k = known_[i];
        const NetWorldActor& a = table.actors[i];
        if (!k.told || !a.live || a.generation != k.generation) continue;
        if (a.state == k.acked && a.state == k.sent) continue;
        uint8_t mask = a.state.shown ? kMaskShown : 0;
        // a field goes while it differs from what the client is known to hold or from what it may hold by now
        const auto differs = [&](const auto member) { return a.state.*member != k.acked.*member || a.state.*member != k.sent.*member; };
        const auto axis = [&](const std::array<int16_t, 3> NetActorState::*member, size_t n) {
            return (a.state.*member)[n] != (k.acked.*member)[n] || (a.state.*member)[n] != (k.sent.*member)[n];
        };
        if (axis(&NetActorState::pos, 0)) mask |= kMaskX;
        if (axis(&NetActorState::pos, 1)) mask |= kMaskY;
        if (axis(&NetActorState::pos, 2)) mask |= kMaskZ;
        if (axis(&NetActorState::rot, 1)) mask |= kMaskYaw;
        if (axis(&NetActorState::rot, 0) || axis(&NetActorState::rot, 2)) mask |= kMaskRotXZ;
        const bool playing = a.state.animTick != kNetNoAnimTick;
        const bool animChanged = playing && (differs(&NetActorState::anim) || differs(&NetActorState::animTick));
        const uint8_t age = playing ? animAgeByte(table.tick, a.state.animTick) : 0;
        uint8_t more = 0;
        if (animChanged) {
            mask |= kMaskAnim;
            if (age != 0) more |= kMoreAge;
            if (a.state.animKeyed) more |= kMoreKey;
            if (a.state.held) more |= kMoreHeld;
        }
        const NetActorVisual &v = a.state.visual, &va = k.acked.visual, &vs = k.sent.visual;
        if (v.scale != va.scale || v.scale != vs.scale) more |= kMoreScale;
        if (v.tint != va.tint || v.tint != vs.tint) more |= kMoreTint;
        if (v.hiddenParts != va.hiddenParts || v.hiddenParts != vs.hiddenParts) more |= kMoreParts;
        if (a.state.aim != k.acked.aim || a.state.aim != k.sent.aim) more |= kMoreAim;
        if (more) mask |= kMaskMore;
        Writer e;
        e.u8(uint8_t(i));
        e.u8(mask);
        if (more) e.u8(more);
        if (mask & kMaskX) e.i16(a.state.pos[0]);
        if (mask & kMaskY) e.i16(a.state.pos[1]);
        if (mask & kMaskZ) e.i16(a.state.pos[2]);
        if (mask & kMaskYaw) e.i16(a.state.rot[1]);
        if (mask & kMaskRotXZ) {
            e.i16(a.state.rot[0]);
            e.i16(a.state.rot[2]);
        }
        if (mask & kMaskAnim) e.u8(a.state.anim);
        if (more & kMoreAge) e.u8(age);
        if (more & kMoreKey) {
            e.u16(a.state.animKey);
            e.u8(a.state.animTicks);
        }
        if (more & kMoreScale)
            for (const int16_t c : v.scale) e.i16(c);
        if (more & kMoreTint) e.u16(v.tint);
        if (more & kMoreParts) {
            e.u16(uint16_t(v.hiddenParts));
            e.u8(uint8_t(v.hiddenParts >> 16));
        }
        if (more & kMoreAim) {
            e.u8(uint8_t(a.state.aim.size()));
            for (const NetAimOffset& o : a.state.aim) {
                uint8_t lanes = 0;
                for (size_t l = 0; l < 3; ++l)
                    if (o.lanes[l] != 0) lanes |= uint8_t(1u << l);
                e.u8(uint8_t(o.entry | lanes << 5));
                for (size_t l = 0; l < 3; ++l)
                    if (o.lanes[l] != 0) e.i16(o.lanes[l]);
            }
        }
        if (w.bytes.size() + e.bytes.size() > kNetMaxPayload) {
            cursor_ = i; // the next update starts with what did not fit
            cut = true;
            break;
        }
        w.bytes.insert(w.bytes.end(), e.bytes.begin(), e.bytes.end());
        sent.entries.push_back({uint8_t(i), a.generation, a.state});
        k.sent = a.state;
    }
    if (cut) ++updatesCut_;
    else cursor_ = 0;
    inFlight_.push_back(std::move(sent));
    while (inFlight_.size() > kMaxInFlight) inFlight_.pop_front();
    return std::move(w.bytes);
}

void NetStateSender::acknowledge(uint16_t sequence) {
    while (!inFlight_.empty() && !seqNewer(inFlight_.front().sequence, sequence)) {
        const Sent& sent = inFlight_.front();
        // what an update older than the acknowledged one held may have been lost, so only that one's entries count
        if (sent.sequence == sequence)
            for (const Entry& e : sent.entries) {
                Known& k = known_[e.slot];
                if (k.told && k.generation == e.generation) k.acked = e.state;
            }
        inFlight_.pop_front();
    }
}

// ---- the render clock ------------------------------------------------------------------------------------------------

void NetRenderClock::heard(uint32_t tick) {
    if (!running_ || int32_t(tick - newest_) > 0) newest_ = tick;
    running_ = true;
}

double NetRenderClock::at(uint64_t nowNs) {
    constexpr double kTicksPerNs = 30.0 / 1e9, kResyncTicks = 8.0, kSteering = 0.1;
    const double target = double(newest_) - delay_;
    if (!started_) {
        started_ = true;
        shown_ = std::max(target, 0.0);
    } else {
        const double previous = shown_;
        shown_ += double(nowNs - std::min(nowNs, lastNs_)) * kTicksPerNs;
        const double error = target - shown_;
        if (std::abs(error) > kResyncTicks) shown_ = std::max(previous, target); // the world paused, or a gap
        else shown_ = std::max(previous, shown_ + kSteering * error);
        shown_ = std::min(shown_, std::max(previous, double(newest_))); // never past the newest state heard
    }
    lastNs_ = nowNs;
    return shown_;
}

// ---- the client ------------------------------------------------------------------------------------------------------

void NetStateClient::noteStart(Occupant& o, const NetActorState& s) {
    if (s.animTick == kNetNoAnimTick) return;
    if (!o.starts.empty() && o.starts.back().tick == s.animTick && o.starts.back().start.anim == s.anim) return; // a resend
    NetClientActors::AnimStart start;
    start.anim = s.anim;
    start.key = s.animKey;
    start.ticks = s.animTicks;
    start.keyed = s.animKeyed;
    start.held = s.held;
    o.starts.push_back({s.animTick, start});
}

bool NetStateClient::applyEvents(std::span<const uint8_t> bytes) {
    Reader r{bytes};
    const uint32_t tick = r.u32();
    const uint32_t first = r.u32();
    if (!r.ok || first != eventsArrived_) return false;
    while (r.ok && r.at < bytes.size()) {
        const uint8_t kind = r.u8();
        const uint8_t slot = r.u8();
        if (kind == kEventGo) {
            if (std::shared_ptr<Occupant>& o = heardOf_[slot]) {
                due_.push_back({tick, false, o});
                o = nullptr;
            }
        } else if (kind == kEventAppear || kind == kEventAppearStill) {
            auto o = std::make_shared<Occupant>();
            o->slot = slot;
            o->identity.type = r.u8();
            o->identity.subtype = r.u8();
            o->identity.variant = r.u8();
            o->identity.modelEntry = r.u8();
            o->last.shown = r.u8() != 0;
            for (int16_t& v : o->last.pos) v = r.i16();
            for (int16_t& v : o->last.rot) v = r.i16();
            if (kind == kEventAppear) {
                o->last.anim = r.u8();
                o->last.animTick = tick - r.u16();
                o->last.animKey = r.u16();
                o->last.animTicks = r.u8();
                const uint8_t bits = r.u8();
                o->last.animKeyed = (bits & 1) != 0;
                o->last.held = (bits & 2) != 0;
            }
            o->appearedAtEvent = eventsArrived_ + 1;
            o->samples.push_back({tick, o->last});
            noteStart(*o, o->last);
            heardOf_[slot] = o;
            due_.push_back({tick, true, o});
        } else {
            return false;
        }
        ++eventsArrived_;
    }
    return r.done();
}

std::optional<uint16_t> NetStateClient::applyParsed(std::span<const uint8_t> bytes, uint32_t eventsThrough) {
    Reader r{bytes};
    const uint16_t sequence = r.u16();
    const uint32_t tick = r.u32();
    r.u32();
    if (!r.ok) return std::nullopt;
    // every actor holds its last state at this tick unless the update says otherwise
    std::array<std::optional<NetActorState>, kNetSlots> changed{};
    while (r.ok && r.at < bytes.size()) {
        const uint8_t slot = r.u8();
        const uint8_t mask = r.u8();
        const uint8_t more = (mask & kMaskMore) ? r.u8() : uint8_t(0);
        Occupant* o = heardOf_[slot].get();
        NetActorState s = o ? o->last : NetActorState{};
        if (mask & kMaskX) s.pos[0] = r.i16();
        if (mask & kMaskY) s.pos[1] = r.i16();
        if (mask & kMaskZ) s.pos[2] = r.i16();
        if (mask & kMaskYaw) s.rot[1] = r.i16();
        if (mask & kMaskRotXZ) {
            s.rot[0] = r.i16();
            s.rot[2] = r.i16();
        }
        s.shown = (mask & kMaskShown) != 0;
        if (mask & kMaskAnim) s.anim = r.u8();
        const uint32_t age = (more & kMoreAge) ? r.u8() : 0u;
        s.animKeyed = false;
        if (more & kMoreKey) {
            s.animKey = r.u16();
            s.animTicks = r.u8();
            s.animKeyed = true;
        }
        if (mask & kMaskAnim) {
            s.animTick = tick - age;
            s.held = (more & kMoreHeld) != 0;
        }
        if (more & kMoreScale) {
            for (int16_t& c : s.visual.scale) c = r.i16();
            s.told |= kNetToldScale;
        }
        if (more & kMoreTint) {
            s.visual.tint = r.u16();
            s.told |= kNetToldTint;
        }
        if (more & kMoreParts) {
            s.visual.hiddenParts = r.u16();
            s.visual.hiddenParts |= uint32_t(r.u8()) << 16;
            s.told |= kNetToldParts;
        }
        if (more & kMoreAim) {
            s.aim.resize(r.u8());
            for (NetAimOffset& a : s.aim) {
                const uint8_t head = r.u8();
                a.entry = head & 0x1f;
                for (size_t l = 0; l < 3; ++l) a.lanes[l] = (head >> (5 + l) & 1) ? r.i16() : int16_t(0);
            }
        }
        changed[slot] = s;
    }
    if (!r.done()) return std::nullopt;
    lastSequence_ = sequence;
    anySequence_ = true;
    heard_ = true;
    newestTick_ = tick;
    for (size_t i = 0; i < kNetSlots; ++i) {
        Occupant* o = heardOf_[i].get();
        if (!o) continue;
        // an update built before this occupant appeared speaks of the one before it
        if (changed[i] && o->appearedAtEvent <= eventsThrough) {
            o->last = *changed[i];
            noteStart(*o, o->last);
        }
        o->samples.push_back({tick, o->last});
        while (o->samples.size() > kMaxSamples) o->samples.pop_front();
    }
    return sequence;
}

std::optional<uint16_t> NetStateClient::applyUpdate(std::span<const uint8_t> bytes) {
    Reader r{bytes};
    const uint16_t sequence = r.u16();
    r.u32();
    const uint32_t eventsThrough = r.u32();
    if (!r.ok) return std::nullopt;
    if (anySequence_ && !seqNewer(sequence, lastSequence_)) return std::nullopt; // an older one replaced by a newer
    if (eventsThrough > eventsArrived_) { // its actors are on their way: the newest of these waits for them
        Reader held{held_};
        const uint16_t heldSequence = held.u16();
        if (held_.empty() || seqNewer(sequence, heldSequence)) held_.assign(bytes.begin(), bytes.end());
        return std::nullopt;
    }
    return applyParsed(bytes, eventsThrough);
}

std::optional<uint16_t> NetStateClient::applyHeld() {
    if (held_.empty()) return std::nullopt;
    Reader r{held_};
    const uint16_t sequence = r.u16();
    r.u32();
    const uint32_t eventsThrough = r.u32();
    if (eventsThrough > eventsArrived_) return std::nullopt;
    const std::vector<uint8_t> bytes = std::move(held_);
    held_.clear();
    if (anySequence_ && !seqNewer(sequence, lastSequence_)) return std::nullopt;
    return applyParsed(bytes, eventsThrough);
}

namespace {

constexpr int kAngleBits = 0x1000;
constexpr int kSnapUnits = 0x400; // a jump this long is a cut, as the ghosts' (game/ghost_buffer)

int16_t lerpAngle(int16_t a, int16_t b, double f) {
    int d = (int(b) - int(a)) % kAngleBits;
    if (d > kAngleBits / 2) d -= kAngleBits;
    if (d < -kAngleBits / 2) d += kAngleBits;
    return int16_t(int(a) + int(std::lround(d * f)));
}

} // namespace

void NetStateClient::advance(uint32_t tick) {
    while (!due_.empty() && int32_t(due_.front().tick - tick) <= 0) {
        const Due d = std::move(due_.front());
        due_.pop_front();
        const uint8_t slot = d.occupant->slot;
        if (d.appear) {
            if (shown_[slot]) actors_.go(slot);
            shown_[slot] = d.occupant;
            actors_.appear(slot, d.occupant->identity, d.occupant->samples.front().state);
        } else if (shown_[slot] == d.occupant) {
            actors_.go(slot);
            shown_[slot] = nullptr;
        }
    }
    for (const std::shared_ptr<Occupant>& held : shown_) {
        if (!held) continue;
        Occupant& o = *held;
        while (o.samples.size() >= 2 && int32_t(o.samples[1].tick - tick) <= 0) o.samples.pop_front();
        NetActorState s = o.samples.front().state;
        if (o.samples.size() >= 2 && int32_t(tick - o.samples[0].tick) > 0) {
            const Sample &a = o.samples[0], &b = o.samples[1];
            const double f = double(tick - a.tick) / double(b.tick - a.tick);
            bool cut = false;
            for (size_t i = 0; i < 3; ++i) cut = cut || std::abs(int(b.state.pos[i]) - int(a.state.pos[i])) > kSnapUnits;
            if (!cut)
                for (size_t i = 0; i < 3; ++i) {
                    s.pos[i] = int16_t(int(a.state.pos[i]) + int(std::floor(double(int(b.state.pos[i]) - int(a.state.pos[i])) * f + 0.5)));
                    s.rot[i] = lerpAngle(a.state.rot[i], b.state.rot[i], f);
                }
            // an aim turns as the rotation does; a list that changed goes as it is at the state before
            if (a.state.aim.size() == b.state.aim.size())
                for (size_t n = 0; n < s.aim.size(); ++n)
                    if (a.state.aim[n].entry == b.state.aim[n].entry)
                        for (size_t l = 0; l < 3; ++l) s.aim[n].lanes[l] = lerpAngle(a.state.aim[n].lanes[l], b.state.aim[n].lanes[l], f);
        }
        // the newest animation due: a shorter one between two updates was over before anyone saw it
        std::optional<NetClientActors::AnimStart> start;
        while (!o.starts.empty() && int32_t(o.starts.front().tick - tick) <= 0) {
            start = o.starts.front().start;
            start->steps = tick - o.starts.front().tick;
            o.starts.pop_front();
        }
        actors_.tick(o.slot, s, start);
    }
}

size_t NetStateClient::shownCount() const {
    size_t n = 0;
    for (const auto& o : shown_)
        if (o) ++n;
    return n;
}

// ---- the client on the game's pools ---------------------------------------------------------------------------------

void PoolNetActors::appear(uint8_t slot, const NetActorIdentity& identity, const NetActorState& state) {
    ActorHeader& head = claimActorSlot(slot);
    head.type = identity.type;
    head.subtype = identity.subtype;
    head.variant = identity.variant;
    Actor& a = actorLayoutOf(head);
    a.pos.x.whole = state.pos[0];
    a.pos.y.whole = state.pos[1];
    a.pos.z.whole = state.pos[2];
    a.rotation.x = state.rot[0];
    a.rotation.y = state.rot[1];
    a.rotation.z = state.rot[2];
    animates_[slot] = identity.modelEntry != kNetNoModel && bindActorModelEntry(a, identity.modelEntry) &&
                      actorSlotClass(slot) == kAnimatedClass;
}

void PoolNetActors::go(uint8_t slot) {
    freeActor(actorHeaderAt(actorBySlot(slot)));
    animates_[slot] = false;
}

void PoolNetActors::tick(uint8_t slot, const NetActorState& state, const std::optional<AnimStart>& start) {
    Actor& a = actorLayoutOf(actorHeaderAt(actorBySlot(slot)));
    a.pos.x.whole = state.pos[0];
    a.pos.y.whole = state.pos[1];
    a.pos.z.whole = state.pos[2];
    a.rotation.x = state.rot[0];
    a.rotation.y = state.rot[1];
    a.rotation.z = state.rot[2];
    a.setDrawn(state.shown);
    if (actorSlotClass(slot) != kAnimatedClass) {
        MeshActor& mesh = meshActorAt(a.at);
        if (state.told & kNetToldScale) {
            mesh.scale.x = state.visual.scale[0];
            mesh.scale.y = state.visual.scale[1];
            mesh.scale.z = state.visual.scale[2];
        }
        if (state.told & kNetToldTint) mesh.tint = state.visual.tint;
        return;
    }
    if (!animates_[slot]) return;
    AnimatedActor& animated = asAnimated(a);
    if (state.told & kNetToldScale) {
        animated.scale.x = state.visual.scale[0];
        animated.scale.y = state.visual.scale[1];
        animated.scale.z = state.visual.scale[2];
    }
    if (state.told & kNetToldTint) animated.tint = state.visual.tint;
    if (state.told & kNetToldParts) {
        const uint32_t slots = std::min<uint32_t>(animated.boneSlotCount, 24);
        for (uint32_t k = 0; k < slots; ++k) {
            uint8_t& flags = animated.parts[k].flags;
            flags = (state.visual.hiddenParts >> k & 1) ? uint8_t(flags | kPartHidden) : uint8_t(flags & ~kPartHidden);
        }
    }
    // an animation byte that names nothing in the client's files stops that actor's animation, not the client
    try {
        if (start) {
            animSet(animated, start->anim, start->keyed ? int(start->key) : 0);
            if (start->keyed) {
                animated.animKey.ticks = start->ticks;
                animDecodeCurrent(animated);
            }
            held_[slot] = start->held;
            if (!start->held)
                for (uint32_t i = 0; i < start->steps; ++i) animStep(animated);
        } else if (animated.animScript.value() == 0) {
            return;
        } else if (held_[slot]) {
            animDecodeCurrent(animated);
        } else {
            animStep(animated);
        }
        DecodedFrame& decoded = decodedFrame();
        for (const NetAimOffset& o : state.aim) {
            if (o.entry >= DecodedFrame::kEntries) continue;
            const LaneVec4 at = decoded.entries[o.entry];
            decoded.entries[o.entry].assign(int16_t(uint16_t(at.x) + uint16_t(o.lanes[0])), int16_t(uint16_t(at.y) + uint16_t(o.lanes[1])),
                                            int16_t(uint16_t(at.z) + uint16_t(o.lanes[2])));
        }
        animPose(animated);
    } catch (const std::exception&) {
        animates_[slot] = false;
    }
}

void PoolNetActors::endTick() { applyActorFrees(); }

// ---- the pose log ----------------------------------------------------------------------------------------------------

namespace {

void poseRecord(std::FILE* file, uint32_t tick, const Actor& a, const AnimatedActor* animated) {
    Writer w;
    w.u32(tick);
    w.u8(a.slot);
    w.u8(a.type);
    uint16_t parts = 0;
    if (animated && animated->boneBlocks != 0 && animated->animScript.value() != 0) parts = animated->partCount;
    w.u8(animated ? animated->anim : 0);
    w.u16(animated && parts != 0 ? uint16_t((animated->animKeyAt.value() - animated->animScript.value()) / 4) : uint16_t(0));
    w.u8(animated ? uint8_t(animated->animKey.frame) : 0);
    w.u8(animated ? animated->animKey.ticks : 0);
    uint32_t hidden = 0;
    if (animated)
        for (uint32_t k = 0; k < std::min<uint32_t>(animated->boneSlotCount, 24); ++k)
            if (animated->parts[k].flags & kPartHidden) hidden |= 1u << k;
    w.u16(animated && parts != 0 ? animated->tint : uint16_t(0));
    w.u32(hidden);
    w.i16(a.pos.x.whole);
    w.i16(a.pos.y.whole);
    w.i16(a.pos.z.whole);
    w.i16(a.rotation.x);
    w.i16(a.rotation.y);
    w.i16(a.rotation.z);
    w.u16(parts);
    if (parts != 0) {
        const BoneBlock* root = boneBlockOf(*animated, 0);
        const ShortVec4 origin = root ? root->points[0] : ShortVec4{};
        for (int k = 0; k < int(parts); ++k) {
            const BoneBlock* b = boneBlockOf(*animated, k >> 2);
            if (!b) {
                for (int i = 0; i < 12; ++i) w.i16(0);
                continue;
            }
            const ShortVec4& p = b->points[size_t(k & 3)];
            w.i16(int16_t(uint16_t(p.x) - uint16_t(origin.x)));
            w.i16(int16_t(uint16_t(p.y) - uint16_t(origin.y)));
            w.i16(int16_t(uint16_t(p.z) - uint16_t(origin.z)));
            const Matrix3 m = b->matrices[size_t(k & 3)];
            for (int i = 0; i < 9; ++i) w.i16(m.e[i]);
        }
    }
    std::fwrite(w.bytes.data(), 1, w.bytes.size(), file);
}

} // namespace

void writeNetPoses(std::FILE* file, uint32_t tick, NetSlotFilter filter) {
    AnimatedPool& animated = animatedPool();
    for (size_t i = 0; i < kAnimatedFieldSlots; ++i)
        if ((animated[i].flags & kActorLive) && filter(animated[i].slot)) poseRecord(file, tick, animated[i], &animated[i]);
    StaticPool& statics = staticPool();
    for (size_t i = 0; i < StaticPool::size(); ++i)
        if ((statics[i].flags & kActorLive) && filter(statics[i].slot)) poseRecord(file, tick, statics[i], nullptr);
    ShotPool& shots = shotPool();
    for (size_t i = 0; i < ShotPool::size(); ++i)
        if ((shots[i].flags & kActorLive) && filter(shots[i].slot)) poseRecord(file, tick, shots[i], nullptr);
    ProjectilePool& projectiles = projectilePool();
    for (size_t i = 0; i < ProjectilePool::size(); ++i)
        if ((projectiles[i].flags & kActorLive) && filter(projectiles[i].slot))
            poseRecord(file, tick, projectiles[i], nullptr);
}

std::vector<NetPoseRecord> readNetPoses(const std::filesystem::path& file) {
    std::vector<NetPoseRecord> out;
    std::FILE* f = nullptr;
#ifdef _WIN32
    _wfopen_s(&f, file.c_str(), L"rb");
#else
    f = std::fopen(file.c_str(), "rb");
#endif
    if (!f) return out;
    std::fseek(f, 0, SEEK_END);
    std::vector<uint8_t> bytes(size_t(std::max(std::ftell(f), 0L)));
    std::fseek(f, 0, SEEK_SET);
    const size_t read = std::fread(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    Reader r{std::span<const uint8_t>(bytes).first(read)};
    while (r.ok && r.at < read) {
        NetPoseRecord p;
        p.tick = r.u32();
        p.slot = r.u8();
        p.type = r.u8();
        p.anim = r.u8();
        p.key = r.u16();
        p.frame = int8_t(r.u8());
        p.ticksLeft = r.u8();
        p.tint = r.u16();
        p.hiddenParts = r.u32();
        for (int16_t& v : p.pos) v = r.i16();
        for (int16_t& v : p.rot) v = r.i16();
        const uint16_t parts = r.u16();
        p.bones.resize(size_t(parts) * 12);
        for (int16_t& v : p.bones) v = r.i16();
        if (r.ok) out.push_back(std::move(p));
    }
    return out;
}

} // namespace od::port
