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
                  kMaskShown = 0x40, kMaskAnimAge = 0x80;
constexpr uint8_t kEventAppear = 1, kEventGo = 2;
constexpr size_t kUpdateHeaderBytes = 10;
constexpr size_t kBatchHeaderBytes = 8;
constexpr size_t kAppearBytes = 22, kGoBytes = 2;
constexpr size_t kMaxEntryBytes = 2 + 6 + 2 + 4 + 2;
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
        a = NetWorldActor{true, a.generation + 1, {}, {}, s.model, s.animTable, 0};
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
    if (s.animScript == 0) a.state.animTick = kNetNoAnimTick;
    else if (previousTick == kNetNoAnimTick || previousAnim != s.anim || a.animScript != s.animScript)
        a.state.animTick = tick;
    a.animScript = s.animScript;
}

void NetWorldTable::gone(uint8_t slot) { actors[slot].live = false; }

namespace {

// the entry of the stage's model bank the actor's model was bound from
uint8_t modelEntryOf(uint32_t cls, uint32_t model, uint32_t animTable) {
    const uint32_t bank = stage().modelBank;
    if (bank == 0 || model == 0) return kNetNoModel;
    const FileDataReader records = loadedFile(bank, 4);
    const int32_t count = records.read<int32_t>(0);
    for (int32_t i = 0; i < count && i < kNetNoModel; ++i) {
        const auto entry = FileDataRef<ModelBankEntryData>(records.sub(4 + size_t(i) * 16)).get();
        if (cls != (entry.key & 0xf0u) || entry.model + bank != model) continue;
        if (cls == kAnimatedClass && entry.animTable + bank != animTable) continue;
        return uint8_t(i);
    }
    return kNetNoModel;
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
                           : modelEntryOf(actorSlotClass(slot), a.model, 0);
        table.seen(slot, s);
    }
}

} // namespace

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
                           : modelEntryOf(kAnimatedClass, s.model, s.animTable);
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
        w.u8(kEventAppear);
        w.u8(slot);
        w.u8(a.identity.type);
        w.u8(a.identity.subtype);
        w.u8(a.identity.variant);
        w.u8(a.identity.modelEntry);
        w.u8(a.state.shown ? 1 : 0);
        for (const int16_t v : a.state.pos) w.i16(v);
        for (const int16_t v : a.state.rot) w.i16(v);
        w.u8(a.state.anim);
        w.u16(a.state.animTick == kNetNoAnimTick
                  ? kNetAnimNotPlaying
                  : uint16_t(std::min<uint32_t>(table.tick - a.state.animTick, kNetAnimNotPlaying - 1)));
        ++eventsSent_;
        k.told = true;
        k.generation = a.generation;
        k.acked = a.state;
        k.sent = a.state;
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
        if (w.bytes.size() + kMaxEntryBytes > kNetMaxPayload) {
            if (!cut) cursor_ = i; // the next update starts with what did not fit
            cut = true;
            break;
        }
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
        if (animChanged) {
            mask |= kMaskAnim;
            if (age != 0) mask |= kMaskAnimAge;
        }
        w.u8(uint8_t(i));
        w.u8(mask);
        if (mask & kMaskX) w.i16(a.state.pos[0]);
        if (mask & kMaskY) w.i16(a.state.pos[1]);
        if (mask & kMaskZ) w.i16(a.state.pos[2]);
        if (mask & kMaskYaw) w.i16(a.state.rot[1]);
        if (mask & kMaskRotXZ) {
            w.i16(a.state.rot[0]);
            w.i16(a.state.rot[2]);
        }
        if (mask & kMaskAnim) {
            w.u8(a.state.anim);
            if (mask & kMaskAnimAge) w.u8(age);
        }
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
    if (!o.starts.empty() && o.starts.back().tick == s.animTick && o.starts.back().anim == s.anim) return; // a resend
    o.starts.push_back({s.animTick, s.anim});
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
        } else if (kind == kEventAppear) {
            auto o = std::make_shared<Occupant>();
            o->slot = slot;
            o->identity.type = r.u8();
            o->identity.subtype = r.u8();
            o->identity.variant = r.u8();
            o->identity.modelEntry = r.u8();
            o->last.shown = r.u8() != 0;
            for (int16_t& v : o->last.pos) v = r.i16();
            for (int16_t& v : o->last.rot) v = r.i16();
            o->last.anim = r.u8();
            const uint16_t age = r.u16();
            o->last.animTick = age == kNetAnimNotPlaying ? kNetNoAnimTick : tick - age;
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
        if (mask & kMaskAnim) {
            s.anim = r.u8();
            s.animTick = tick - ((mask & kMaskAnimAge) ? r.u8() : 0u);
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
        }
        // the newest animation due: a shorter one between two updates was over before anyone saw it
        std::optional<NetClientActors::AnimStart> start;
        while (!o.starts.empty() && int32_t(o.starts.front().tick - tick) <= 0) {
            start = NetClientActors::AnimStart{o.starts.front().anim, tick - o.starts.front().tick};
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
    if (!animates_[slot]) return;
    AnimatedActor& animated = asAnimated(a);
    // an animation byte that names nothing in the client's files stops that actor's animation, not the client
    try {
        if (start) {
            animSet(animated, start->anim, 0);
            for (uint32_t i = 0; i < start->steps; ++i) animStep(animated);
            animPose(animated);
        } else if (animated.animScript.value() != 0) {
            animStepPose(animated);
        }
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
    w.u8(animated ? animated->keyIndex : 0);
    w.u8(animated ? uint8_t(animated->animKey.frame) : 0);
    w.u8(animated ? animated->animKey.ticks : 0);
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
        p.keyIndex = r.u8();
        p.frame = int8_t(r.u8());
        p.ticksLeft = r.u8();
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
