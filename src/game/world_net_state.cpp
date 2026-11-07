// The network state of a world's actors (see world_net_state.hpp).
#include <map>
#include <cstdio>
#include <cstdlib>
#include "game/world_net_state.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <type_traits>

#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/animation_data.hpp"
#include "game/footstep_dust.hpp"
#include "game/glow_trails.hpp"
#include "game/player/players.hpp"
#include "game/water_splash_state.hpp"
#include "game/hit_list.hpp"
#include "game/room_grid.hpp"
#include "game/shots/hyper_shell_state.hpp"
#include "game/loaded_data.hpp"
#include "game/model_data.hpp"
#include "game/modern_aim.hpp"
#include "game/stage_state.hpp"

#include "game/light_column.hpp"
#include "game/net_effect_links.hpp"
#include "game/shockwave_state.hpp"
#include "game/turret_bullet_state.hpp"
#include "game/shots/machine_gun_state.hpp"
#include "game/presentation.hpp"

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
constexpr uint8_t kMoreAnimExtra = 0x01, kMoreLock = 0x02, kMoreSolids = 0x04, kMoreScale = 0x08, kMoreTint = 0x10, kMoreParts = 0x20,
                  kMoreAim = 0x40, kMorePath = 0x80;
constexpr uint8_t kExtraAge = 0x01, kExtraKey = 0x02, kExtraHeld = 0x04, kExtraPaint = 0x08, kExtraLook = 0x10,
                  kExtraUniformScale = 0x20, kExtraReport = 0x40; // what else
constexpr uint8_t kLookBoneScale = 0x01;
constexpr uint8_t kLockPositionsOnly = 0x80, kLockWide = 0x40, kLockOwners = 0x20, kLockCountMask = 0x1f;
constexpr int kLockGrain = 4;   // the units a narrow lock offset counts in
constexpr uint8_t kSolidHasParam = 0x80;
constexpr int kLockTolerance = 4; // units a lock-on point may drift before the client is told
constexpr size_t kMaxLockPoints = 8;
constexpr uint8_t kEventAppear = 1, kEventGo = 2, kEventAppearStill = 3, kEventEffect = 4, kEventShift = 5, kEventRoomPatch = 6, kEventFlash = 7; // Still: no animation in play
constexpr size_t kBatchHeaderBytes = 8;
constexpr size_t kAppearBytes = 26, kGoBytes = 2, kEffectBytes = 35 + 4 + 16 + 1;
constexpr int kPathPosTolerance = 32; // 1/16 units an actor may leave its path by before the world sends a new one
constexpr int kPathRotTolerance = 16; // of a turn of 4096
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

bool netSlotOnPath(uint8_t slot) {
    return (slot >= kShotClass && slot < kShotClass + ShotPool::size()) ||
           (slot >= kProjectileClass && slot < kProjectileClass + ProjectilePool::size());
}

bool netSlotVisibleOrShot(uint8_t slot) { return netSlotVisible(slot) || netSlotOnPath(slot); }

bool netSlotNetworked(uint8_t slot) {
    return netSlotVisibleOrShot(slot) || (slot >= kEffectClass && slot < kEffectClass + EffectPool::size());
}

// ---- the world's table ----------------------------------------------------------------------------------------------

void NetWorldTable::seen(uint8_t slot, const Seen& s) {
    NetWorldActor& a = actors[slot];
    // a shot is the same occupant while its slot's count stays (a beam's class changes its model and its table as it
    // goes: that is still the shot every client runs from its one event)
    const bool shotSlot = slot >= kShotClass && slot < kShotClass + ShotPool::size();
    const bool occupantChanged = !a.live || a.identity.type != s.type || a.serial != s.serial ||
                                 (!shotSlot && (a.model != s.model || a.animTable != s.animTable));
    if (occupantChanged) {
        a = NetWorldActor{true, a.generation + 1, {}, {}, s.bound, s.model, s.animTable, 0, 0, 0};
        a.serial = s.serial;
        a.state.animTick = kNetNoAnimTick;
        ++occupants;
    }
    a.identity = {s.type, s.subtype, s.variant, s.modelEntry};
    if (occupantChanged) {
        a.effect = s.effect;
        a.effectGoes = s.effectGoes;
        a.effectEnds = s.effectEnds;
        a.effectStart = s.effectStart;
        a.effectStart.slot = slot;
        a.effectStart.owner = s.owner; // the player it belongs to, in the world's ids (a client maps them to its table)
    }
    const std::array<int16_t, 3> pathPos = a.state.pos, pathRot = a.state.rot;
    const uint8_t previousAnim = a.state.anim;
    const uint32_t previousTick = a.state.animTick;
    a.state.pos = s.pos;
    a.state.rot = s.rot;
    a.state.anim = s.anim;
    a.state.shown = s.shown;
    a.state.report = s.report;
    const uint16_t tintBefore = occupantChanged ? s.bound.tint : a.state.visual.tint;
    a.state.visual = s.visual;
    if (s.visual.tint == kTintFlash) { // a hit's flash: its own event, so that it is seen however often the updates go
        a.flashTick = tick;
        a.state.visual.tint = tintBefore;
    }
    a.state.aim = s.aim;
    a.state.paint = s.paint;
    a.owner = s.owner;
    a.startedByClientRun = s.startedByClientRun;
    a.state.solids = s.solids;
    bool lockKept = a.state.lock.size() == s.lock.size();
    for (size_t i = 0; lockKept && i < s.lock.size(); ++i) {
        const NetLockPoint &was = a.state.lock[i], &now = s.lock[i];
        lockKept = was.shape == now.shape && was.owner == now.owner && was.harmless == now.harmless;
        for (size_t l = 0; lockKept && l < 3; ++l) lockKept = std::abs(int(was.offset[l]) - int(now.offset[l])) <= kLockTolerance;
    }
    if (!lockKept) a.state.lock = s.lock;
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
    if (!s.onPath) return;
    // on a path: the state holds its point and steps, and they change when the actor leaves them
    if (occupantChanged) {
        a.state.pathTick = tick;
    } else {
        const uint32_t age = tick - a.state.pathTick;
        bool left = false;
        for (size_t i = 0; i < 3; ++i) {
            const int predicted = int(pathPos[i]) * 16 + int(a.state.posStep[i]) * int(age) +
                                  (i == 1 ? int(a.state.fallStep) * int(age) * (int(age) - 1) / 2 : 0);
            left = left || std::abs((s.fine[i] >> 12) - predicted) > kPathPosTolerance;
            int turned = (int(s.rot[i]) - (int(pathRot[i]) + int(a.state.rotStep[i]) * int(age))) & 0xfff;
            if (turned > 0x800) turned = 0x1000 - turned;
            left = left || turned > kPathRotTolerance;
        }
        if (left) {
            // the y step's growth since this path started: a fall goes on by itself
            const int newStep = (s.fine[1] - a.lastPos[1]) >> 12;
            const bool wasStill = a.state.posStep == std::array<int16_t, 3>{} && a.state.fallStep == 0;
            a.state.fallStep = wasStill || age < 2 ? int16_t(0) : int16_t(std::clamp((newStep - int(a.state.posStep[1])) / int(age - 1), -0x7fff, 0x7fff));

            for (size_t i = 0; i < 3; ++i) {
                a.state.posStep[i] = int16_t(std::clamp((s.fine[i] - a.lastPos[i]) >> 12, -0x8000, 0x7fff));
                int turn = (int(s.rot[i]) - int(a.lastRot[i])) & 0xfff;
                if (turn > 0x800) turn -= 0x1000;
                a.state.rotStep[i] = int16_t(turn);
                a.state.pos[i] = int16_t(a.lastPos[i] >> 16);
                a.state.rot[i] = a.lastRot[i];
            }
            a.state.pathTick = tick - 1;
        } else {
            a.state.pos = pathPos;
            a.state.rot = pathRot;
        }
    }
    a.lastPos = s.fine;
    a.lastRot = s.rot;
}

void NetWorldTable::gone(uint8_t slot) { actors[slot].live = false; }

void NetWorldTable::watchRoomBlock(std::span<const uint8_t> live, size_t from, size_t to) {
    if (live.size() != roomShadow.size()) return;
    to = std::min(to, live.size());
    if (from >= to || std::memcmp(live.data() + from, roomShadow.data() + from, to - from) == 0) return;
    constexpr size_t kMaxRun = 200, kJoinGap = 8;
    for (size_t i = from; i < to;) {
        if (live[i] == roomShadow[i]) {
            ++i;
            continue;
        }
        size_t end = i + 1, last = i;
        while (end < to && end - i < kMaxRun && end - last <= kJoinGap) {
            if (live[end] != roomShadow[end]) last = end;
            ++end;
        }
        NetRoomPatch p;
        p.offset = uint32_t(i);
        p.bytes.assign(live.begin() + ptrdiff_t(i), live.begin() + ptrdiff_t(last) + 1);
        std::copy(p.bytes.begin(), p.bytes.end(), roomShadow.begin() + ptrdiff_t(i));
        roomPatches.push_back(std::move(p));
        i = last + 1;
    }
}

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

// the tick's lock-on entries by slot, absolute (offset here: the entry's point)
std::array<std::vector<NetLockPoint>, kNetSlots>& lockEntries() {
    static std::array<std::vector<NetLockPoint>, kNetSlots> entries;
    return entries;
}

// the tick's solids by slot
std::array<std::vector<NetSolid>, kNetSlots>& solidEntries() {
    static std::array<std::vector<NetSolid>, kNetSlots> entries;
    return entries;
}

void gatherSolids() {
    auto& entries = solidEntries();
    for (auto& list : entries) list.clear();
    const CollisionQueues& q = actorCollisionQueues();
    for (uint32_t i = 0; i < q.solidCountLast && i < CollisionQueues::kSolids; ++i) {
        const SolidEntry& e = q.solids[i];
        if (e.actor) entries[e.actor->slot].push_back({e.type, e.bone, e.param});
    }
}

void gatherLockEntries() {
    auto& entries = lockEntries();
    for (auto& list : entries) list.clear();
    const ActorSystemState& sys = actorSystem();
    for (uint32_t i = 0; i < sys.hitCountLast && i < kHitListCapacity; ++i) {
        const HitEntry& e = sys.hitList[i];
        const uint32_t group = (e.owner >> kHitGroupShift) & 7;
        if (!(hitGroupPairs(group) & kHitGroupLockOn)) continue;
        auto& list = entries[e.owner >> kHitSlotShift];
        // a capsule is two entries, kept together: the selection reads them as one
        const bool capsule = (uint16_t(e.shape()) & kHitCapsuleMask) != 0 && i + 1 < sys.hitCountLast;
        if (list.size() + (capsule ? 2u : 1u) > kMaxLockPoints) {
            if (capsule) ++i;
            continue;
        }
        list.push_back({{e.pos.x, e.pos.y, e.pos.z}, uint16_t(e.shape()), e.owner & ((1u << kHitSlotShift) - 1),
                        (e.attack & kHitAttackHarmless) != 0});
        if (capsule) {
            const HitEntry& end = sys.hitList[++i];
            list.push_back({{end.pos.x, end.pos.y, end.pos.z}, uint16_t(end.shape()), end.owner & ((1u << kHitSlotShift) - 1),
                            (end.attack & kHitAttackHarmless) != 0});
        }
    }
}

NetWorldTable::Seen seenOf(const Actor& a, uint32_t model, uint32_t animTable) {
    NetWorldTable::Seen s;
    s.owner = a.ownerPlayer;
    s.startedByClientRun = a.startedByClientRun;
    s.solids = solidEntries()[a.slot];
    s.lock = lockEntries()[a.slot];
    for (NetLockPoint& p : s.lock) {
        p.offset[0] = int16_t(uint16_t(p.offset[0]) - uint16_t(a.pos.x.whole));
        p.offset[1] = int16_t(uint16_t(p.offset[1]) - uint16_t(a.pos.y.whole));
        p.offset[2] = int16_t(uint16_t(p.offset[2]) - uint16_t(a.pos.z.whole));
    }
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

// the classes a client runs from its own room setup, or for its own Mega Man (the markers of the charge and of a status)
bool netClientKeepsClass(RoutineId id) {
    return id == RoutineId::ChargeMarkerUpdate || id == RoutineId::DrainBurnMarkerUpdate || id == RoutineId::SlowMarkerUpdate ||
           id == RoutineId::DoorActorUpdate || id == RoutineId::SnowUpdate || id == RoutineId::DoorLeafHingedUpdate || id == RoutineId::DoorLeafDoubleUpdate ||
           id == RoutineId::DoorLeafSlidingUpdate;
}

// the statics that are effects: seen, not played with, and the same on every machine's own random numbers
bool netEffectClass(RoutineId id) {
    switch (id) {
    case RoutineId::BlastUpdate:
    case RoutineId::ExplosionUpdate:
    case RoutineId::FlashBurstUpdate:
    case RoutineId::WallDebrisUpdate:
    case RoutineId::HurtSparksUpdate:
    case RoutineId::SparkSprayUpdate:
    case RoutineId::ShellCasingUpdate:
    case RoutineId::SmokeColumnUpdate:
    case RoutineId::SparkleUpdate:
    case RoutineId::ExtinguisherMistUpdate:
    case RoutineId::ShockWaveUpdate:
    case RoutineId::HitSparkUpdate:
    case RoutineId::BurstUpdate:
    case RoutineId::SkidSparksUpdate:
    case RoutineId::DustPuffUpdate:
    case RoutineId::LaserWallSparkUpdate:
    case RoutineId::MachineGunMuzzleUpdate:
    case RoutineId::MachineGunImpactUpdate:
    case RoutineId::FootstepDustUpdate:
    case RoutineId::SparkEmitterUpdate:
    case RoutineId::MissileTrailUpdate:
    case RoutineId::DarkTrailUpdate:
    case RoutineId::WaterSplashUpdate:
    case RoutineId::BossBladeUpdate:
    case RoutineId::PickupUpdate: return true;
    default: return false;
    }
}

bool staticIsEffect(const StaticActor& a) {
    const RoutineRef h = actorHandler(a, (a.loopFlags & kActorLateLoop) ? ActorPool::StaticsLate : ActorPool::Statics, false);
    return h && netEffectClass(h.id());
}

// the projectiles that are effects: their picture is their class's, what they hit is the world's copy's
bool netEffectProjectileClass(RoutineId id) { return id == RoutineId::ShockwaveUpdate || id == RoutineId::TurretBulletUpdate; }

// what a splash follows, by its variant's top bits: its own position (0), an actor (1), a player's bone point (2)
void noteSplashSource(StaticActor& a, NetEffect& e, bool& goes) {
    const auto* splash = heldClassStateOf<WaterSplashFields>(a);
    const uint32_t source = a.variant >> 6;
    if (source == 0) return;
    e.spare = int16_t(a.pos.y.frac); // the point's offset over the position's words (WaterSplashFields)
    if (splash && source == 1 && splash->followed) {
        e.parent = splash->followed->slot;
    } else if (splash && source == 2 && splash->point) {
        goes = false;
        forEachPlayer([&](const Player& p) {
            for (size_t b = 0; b < kPlayerBones; ++b)
                if (static_cast<const ShortVec3*>(&p.bonePoints[b]) == splash->point) {
                    e.parent = p.slot;
                    e.bone = uint8_t(b);
                    goes = true;
                }
        });
    } else {
        goes = false;
    }
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
            s.visual.rootScale = {a.scaleBytes[0], a.scaleBytes[1], a.scaleBytes[2]};
            s.visual.semi = a.semiTransparency;
        }
        if constexpr (std::is_same_v<Pool, StaticPool>) {
            if (!held.live || held.identity.type != a.type || held.model != a.model) {
                const RoutineRef h =
                    actorHandler(a, (a.loopFlags & kActorLateLoop) ? ActorPool::StaticsLate : ActorPool::Statics, false);
                const bool glowsColumn = h && h.id() == RoutineId::LightColumnUpdate && lightColumnOfAGlow(a);
                s.effect = h && (netEffectClass(h.id()) || netClientKeepsClass(h.id()) || glowsColumn);
                s.effectGoes = s.effect && !netClientKeepsClass(h.id()) && !glowsColumn;
                s.effectStart.pool = uint8_t(kStaticClass);
                s.effectStart.type = a.type;
                s.effectStart.subtype = a.subtype;
                s.effectStart.variant = a.variant;
                s.effectStart.spawnByte = a.spawnByte;
                s.effectStart.loopFlags = a.loopFlags;
                s.effectStart.flags = uint8_t(a.flags & (kActorLive | kActorDrawn | kActorScenery));
                for (size_t p = 0; p < 4; ++p) s.effectStart.params[p] = a.params[p];
                s.effectStart.pos = s.pos;
                s.effectStart.rot = s.rot;
                s.effectStart.velocity = {a.velocity.x, a.velocity.y, a.velocity.z};
                if (h && h.id() == RoutineId::FootstepDustUpdate) {
                    const auto* dust = heldClassStateOf<FootstepDustFields>(a);
                    const Actor* parent = dust ? dust->parent.get() : nullptr;
                    // no walker known yet: it went with the slot, nothing to show
                    if (parent) s.effectStart.parent = parent->slot;
                    else s.effectGoes = false;
                }
                if (h && h.id() == RoutineId::DarkTrailUpdate) {
                    const auto* trail = heldClassStateOf<hyper_shell_state::DarkTrailFields>(a);
                    if (trail && uint32_t(trail->parent) != 0) s.effectStart.parent = actorHeaderAt(trail->parent).slot;
                    else s.effectGoes = false;
                }
                if (h && h.id() == RoutineId::BossBladeUpdate) {
                    // the boss it follows, in its parameters' word: the client's copy follows the boss's copy there
                    const uint32_t boss = a.paramsWord();
                    if (boss != 0 && (actorHeaderAt(boss).flags & kActorLive)) s.effectStart.parent = actorHeaderAt(boss).slot;
                    else s.effectGoes = false;
                }
                // a pickup: each client runs it from its event (its draw is the class's fields); who takes it is the
                // world's, whose end of it ends the copies
                if (h && h.id() == RoutineId::PickupUpdate) s.effectEnds = true;
                if (h && h.id() == RoutineId::WaterSplashUpdate)
                    noteSplashSource(const_cast<StaticActor&>(static_cast<const StaticActor&>(a)), s.effectStart, s.effectGoes);
            }
        }
        if constexpr (std::is_same_v<Pool, ProjectilePool>) {
            // a projectile whose picture is its class's (a ground shockwave's ring): each client runs it from its event
            if (const RoutineRef h = actorHandler(a, ActorPool::Projectiles, false); h && netEffectProjectileClass(h.id())) {
                s.effect = true;
                s.effectGoes = true;
                s.effectStart.pool = uint8_t(kProjectileClass);
                s.effectStart.type = a.type;
                s.effectStart.subtype = a.subtype;
                s.effectStart.variant = a.variant;
                s.effectStart.spawnByte = a.spawnByte;
                s.effectStart.loopFlags = a.loopFlags;
                s.effectStart.flags = uint8_t(a.flags & (kActorLive | kActorDrawn));
                for (size_t p = 0; p < 4; ++p) s.effectStart.params[p] = a.params[p];
                s.effectStart.pos = s.pos;
                s.effectStart.rot = s.rot;
                s.effectStart.velocity = {a.velocity.x, a.velocity.y, a.velocity.z};
                if (h.id() == RoutineId::TurretBulletUpdate) {
                    // a turret's homing bullet: the turret that holds it, its texture word (its owner's model's), the
                    // wobble's phase it rolled; who it hits and when it goes are the world's
                    const auto* bullet = heldClassStateOf<turret_bullet_state::TurretBulletFields>(a);
                    const AnimatedActor* owner = bullet ? bullet->owner.get() : nullptr;
                    if (owner && (owner->flags & kActorLive)) s.effectStart.parent = owner->slot;
                    else s.effectGoes = false;
                    const uint32_t texture = a.hitBox.result;
                    s.effectStart.trail[0] = int16_t(uint16_t(texture));
                    s.effectStart.trail[1] = int16_t(uint16_t(texture >> 16));
                    s.effectStart.step = a.step;
                    s.effectEnds = true;
                }
            }
        }
        if constexpr (std::is_same_v<Pool, ShotPool>) {
            s.serial = shotSlotSerial(ShotPool::index(a));
            // a player's shot (the buster's, a blade's swing that follows its owner's arm): each client runs it from its
            // event, its picture its class's draw; the world's copy hits, and its go ends the others'
            if (actorHandler(a, ActorPool::Shots, false)) {
                s.effect = true;
                s.effectGoes = true;
                s.effectStart.pool = uint8_t(kShotClass);
                s.effectStart.type = a.type;
                s.effectStart.subtype = a.subtype;
                s.effectStart.variant = a.variant;
                s.effectStart.spawnByte = a.spawnByte;
                s.effectStart.loopFlags = a.loopFlags;
                s.effectStart.flags = uint8_t(a.flags & (kActorLive | kActorDrawn));
                s.effectStart.owner = a.ownerPlayer;
                for (size_t i = 0; i < 4; ++i) s.effectStart.params[i] = a.params[i];
                // what the fire aimed at (as a client's fire hands them to the world: remote_combat's NetFire)
                const ShotActor& shot = static_cast<const ShotActor&>(a);
                // ... and the angles it left with (its owner's crosshair): a client's copy aims as the owner's did
                s.effectStart.trail = {shot.aimPoint.x, shot.aimPoint.y, shot.aimPoint.z, shot.aimPoint.spare,
                                       shot.rotation.x, shot.rotation.y, 1, 0};
                s.effectStart.second = shot.target && (shot.target->flags & kActorLive) ? shot.target->slot : kNetNoParent;
            }
        }
        if (netSlotOnPath(slot)) {
            s.onPath = true;
            s.fine = {a.pos.x.value(), a.pos.y.value(), a.pos.z.value()};
        }
        table.seen(slot, s);
    }
}

} // namespace

bool netStartsOnClients(const ActorHeader& actor, ActorPool pool) {
    const RoutineRef h = actorHandler(actor, pool, false);
    if (!h) return false;
    switch (pool) {
    case ActorPool::Effects:
    case ActorPool::EffectsLate: { // every effect goes, but one whose links do not read (captureNetWorld)
        const NetEffectLinks* links = netEffectLinksOf(h.id());
        std::array<const ActorHeader*, 2> held{};
        return !links || links->read(asEffect(const_cast<ActorHeader&>(actor)), held);
    }
    case ActorPool::Statics:
    case ActorPool::StaticsLate: return netEffectClass(h.id());
    case ActorPool::Projectiles: return netEffectProjectileClass(h.id());
    default: return false; // a player's shot: the shots' update (startSlot)
    }
}

void watchNetPoses() { setAnimPoseObserver(notePoseEdits); }

void captureNetWorld(NetWorldTable& table, uint32_t tick) {
    table.tick = tick;
    table.coordinateShift = stage().coordinateShift;
    gatherLockEntries();
    gatherSolids();
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
        s.report = a.stepCounter;
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
            s.visual.boneScale = (a.runFlags & kActorBoneScale) != 0;
            s.visual.rootScale = {a.rootScaleBytes[0], a.rootScaleBytes[1], a.rootScaleBytes[2]};
            s.visual.semi = a.semiTransparency;
            for (int k = 0; k < int(a.partCount) && k < 32; ++k) {
                const BoneBlock* b = boneBlockOf(a, k >> 2);
                if (!b) break;
                NetPartPaint p;
                p.part = uint8_t(k);
                p.colour = b->boneColour(size_t(k & 3));
                p.semi = b->semiBits(size_t(k & 3));
                if (s.visual.boneScale) p.scale = {b->scaleBytes[size_t(k & 3)][0], b->scaleBytes[size_t(k & 3)][1], b->scaleBytes[size_t(k & 3)][2]};
                else p.scale = {kScaleByteOne, kScaleByteOne, kScaleByteOne};
                if (p.colour != kTintNeutral || p.semi != 0 || p.scale != std::array<uint8_t, 3>{kScaleByteOne, kScaleByteOne, kScaleByteOne})
                    s.paint.push_back(p);
            }
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
    // the effects run on the client from their event
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
        s.effect = true;
        s.effectGoes = true;
        if (const RoutineRef h = actorHandler(e, (e.loopFlags & kActorLateLoop) ? ActorPool::EffectsLate : ActorPool::Effects, false)) {
            if (const NetEffectLinks* links = netEffectLinksOf(h.id())) {
                std::array<const ActorHeader*, 2> held{};
                if (!links->read(const_cast<EffectActor&>(e), held)) s.effectGoes = false;
                if (held[0]) s.effectStart.parent = held[0]->slot;
                if (held[1]) s.effectStart.second = held[1]->slot;
            }
        }
        s.effectStart.subState = e.subState;
        s.effectStart.step = e.step;
        s.effectStart.stepCounter = e.stepCounter;
        for (size_t k = 0; k < e.trail.size(); ++k) s.effectStart.trail[k] = e.trail[k];
        s.effectStart.pool = uint8_t(kEffectClass);
        s.effectStart.type = e.type;
        s.effectStart.subtype = e.subtype;
        s.effectStart.variant = e.variant;
        s.effectStart.spawnByte = e.spawnByte;
        s.effectStart.loopFlags = e.loopFlags;
        s.effectStart.flags = uint8_t(e.flags & (kActorLive | kActorDrawn));
        for (size_t p = 0; p < 4; ++p) s.effectStart.params[p] = e.params[p];
        s.effectStart.pos = {e.pos.x, e.pos.y, e.pos.z};
        s.effectStart.spare = e.posSpare;
        s.owner = e.ownerPlayer;
        s.startedByClientRun = e.startedByClientRun;
        table.seen(slot, s);
    }
}

namespace {
std::map<RoutineId, NetEffectLinks>& linksTable() {
    static std::map<RoutineId, NetEffectLinks> table = [] {
        std::map<RoutineId, NetEffectLinks> t;
        t[RoutineId::GlowTrailUpdate] = {
            [](EffectActor& e, std::array<const ActorHeader*, 2>& held) {
                held[0] = classState<GlowTrailFields>(e).parent.get();
                return true;
            },
            [](EffectActor& e, const std::array<ActorHeader*, 2>& held) { classState<GlowTrailFields>(e).parent = held[0]; }};
        t[RoutineId::ShockRingUpdate] = {
            [](EffectActor& e, std::array<const ActorHeader*, 2>& held) {
                held[0] = classState<shockwave_state::ShockRingFxFields>(e).shock.get();
                return true;
            },
            [](EffectActor& e, const std::array<ActorHeader*, 2>& held) { classState<shockwave_state::ShockRingFxFields>(e).shock = held[0]; }};
        // what a shot makes: the shot runs on each client from its own event, and makes its own
        const NetEffectLinks ofShot = {[](EffectActor&, std::array<const ActorHeader*, 2>&) { return false; },
                                       [](EffectActor&, const std::array<ActorHeader*, 2>&) {}};
        t[RoutineId::BladeTrailUpdate] = ofShot;
        t[RoutineId::ShiningLaserBeamUpdate] = ofShot;
        t[RoutineId::SuctionUpdate] = ofShot;
        t[RoutineId::MachineGunMuzzleUpdate] = {
            [](EffectActor& e, std::array<const ActorHeader*, 2>& held) {
                const uint32_t shot = classState<machine_gun_state::MuzzleFields>(e).shot;
                held[0] = shot ? &actorHeaderAt(shot) : nullptr;
                return true;
            },
            [](EffectActor& e, const std::array<ActorHeader*, 2>& held) { classState<machine_gun_state::MuzzleFields>(e).shot = held[0] ? held[0]->at : 0; }};
        return t;
    }();
    return table;
}
} // namespace

void registerNetEffectLinks(RoutineId cls, const NetEffectLinks& links) { linksTable()[cls] = links; }
const NetEffectLinks* netEffectLinksOf(RoutineId cls) {
    const auto it = linksTable().find(cls);
    return it == linksTable().end() ? nullptr : &it->second;
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
    if (table.coordinateShift != shiftTold_ && table.coordinateShift != 0xff) {
        room(kGoBytes);
        w.u8(kEventShift);
        w.u8(table.coordinateShift);
        ++eventsSent_;
        shiftTold_ = table.coordinateShift;
    }
    for (; patchesTold_ < table.roomPatches.size(); ++patchesTold_) {
        const NetRoomPatch& p = table.roomPatches[patchesTold_];
        room(1 + 4 + 1 + p.bytes.size());
        w.u8(kEventRoomPatch);
        w.u32(p.offset);
        w.u8(uint8_t(p.bytes.size()));
        w.bytes.insert(w.bytes.end(), p.bytes.begin(), p.bytes.end());
        ++eventsSent_;
    }
    for (size_t i = 0; i < kNetSlots; ++i) {
        const uint8_t slot = uint8_t(i);
        if (!filter_(slot)) continue;
        Known& k = known_[i];
        const NetWorldActor& a = table.actors[i];
        // a client's own shot: the client runs it itself. What a player's shot started: every client runs it itself, from
        // its own shot or from the copy it runs of another player's (the world's hits; its pictures would be a second set)
        const bool shotSlot = slot >= kShotClass && slot < kShotClass + ShotPool::size();
        const bool withheld = a.live && ((shotSlot && a.owner == ownShotsOf_) || (a.startedByClientRun && ownShotsOf_ != kNetNoPlayer));
        const bool live = a.live && !withheld;
        const bool same = k.told && live && a.generation == k.generation;
        if (same && !k.effect && a.flashTick != k.flashTold) {
            room(kGoBytes);
            w.u8(kEventFlash);
            w.u8(slot);
            ++eventsSent_;
            k.flashTold = a.flashTick;
        }
        if (same) continue;
        // an effect ends by itself on the client; a shot run from its event ends with the world's, and so does one whose
        // end is the world's to say (a pickup taken)
        if (k.told && k.effect && !k.effectEnds && !(slot >= kShotClass && slot < kShotClass + ShotPool::size())) {
            k.told = false;
        } else if (k.told) {
            room(kGoBytes);
            w.u8(kEventGo);
            w.u8(slot);
            ++eventsSent_;
            k.told = false;
        }
        if (!live) continue;
        if (a.effect) {
            k.told = true;
            k.effect = true;
            k.effectEnds = a.effectEnds;
            k.generation = a.generation;
            if (!a.effectGoes) continue;
            const NetEffect& e = a.effectStart;
            room(kEffectBytes);
            w.u8(kEventEffect);
            w.u8(slot);
            w.u8(e.pool);
            w.u8(e.type);
            w.u8(e.subtype);
            w.u8(e.variant);
            w.u8(e.spawnByte);
            w.u8(e.loopFlags);
            w.u8(e.flags);
            w.u8(e.parent);
            w.u8(e.second);
            w.u8(e.subState);
            w.u8(e.step);
            w.u8(e.stepCounter);
            for (const int16_t v : e.trail) w.i16(v);
            w.u8(e.bone);
            w.u8(e.owner);
            for (const uint8_t p : e.params) w.u8(p);
            for (const int16_t v : e.pos) w.i16(v);
            w.i16(e.spare);
            if (e.pool == kStaticClass || e.pool == kProjectileClass) {
                for (const int16_t v : e.rot) w.i16(v);
                for (const int16_t v : e.velocity) w.i16(v);
            }
            ++eventsSent_;
            continue;
        }
        k.effect = false;
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
        k.flashTold = a.flashTick;
        k.acked = a.state;
        k.acked.visual = a.bound; // a client's bind gives it; a class's change goes in an update
        k.acked.report = 0;       // ... and a new occupant holds none
        k.acked.aim.clear();
        k.acked.lock.clear();
        k.acked.solids.clear();
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
    UpdateBytes counted;
    counted.head = uint32_t(w.bytes.size());
    const size_t from = cursor_;
    for (size_t n = 0; n < kNetSlots; ++n) {
        const size_t i = (from + n) % kNetSlots;
        Known& k = known_[i];
        const NetWorldActor& a = table.actors[i];
        if (!k.told || k.effect || !a.live || a.generation != k.generation) continue;
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
        uint8_t more = 0, extra = 0;
        if (animChanged) {
            mask |= kMaskAnim;
            if (age != 0) extra |= kExtraAge;
            if (a.state.animKeyed) extra |= kExtraKey;
            if (a.state.held) extra |= kExtraHeld;
        }
        if (a.state.paint != k.acked.paint || a.state.paint != k.sent.paint) extra |= kExtraPaint;
        const auto lookDiffers = [&](const NetActorVisual& o) {
            return a.state.visual.boneScale != o.boneScale || a.state.visual.rootScale != o.rootScale || a.state.visual.semi != o.semi;
        };
        if (lookDiffers(k.acked.visual) || lookDiffers(k.sent.visual)) extra |= kExtraLook;
        if (differs(&NetActorState::report)) extra |= kExtraReport;
        if (a.state.lock != k.acked.lock || a.state.lock != k.sent.lock) more |= kMoreLock;
        if (a.state.solids != k.acked.solids || a.state.solids != k.sent.solids) more |= kMoreSolids;
        const NetActorVisual &v = a.state.visual, &va = k.acked.visual, &vs = k.sent.visual;
        if (v.scale != va.scale || v.scale != vs.scale) more |= kMoreScale;
        if (v.tint != va.tint || v.tint != vs.tint) more |= kMoreTint;
        if (v.hiddenParts != va.hiddenParts || v.hiddenParts != vs.hiddenParts) more |= kMoreParts;
        if (a.state.aim != k.acked.aim || a.state.aim != k.sent.aim) more |= kMoreAim;
        const auto pathDiffers = [&](const NetActorState& o) {
            return a.state.pathTick != o.pathTick || a.state.posStep != o.posStep || a.state.rotStep != o.rotStep ||
                   a.state.fallStep != o.fallStep;
        };
        if (pathDiffers(k.acked) || pathDiffers(k.sent)) more |= kMorePath;
        const bool uniformScale = v.scale[0] == v.scale[1] && v.scale[1] == v.scale[2];
        if ((more & kMoreScale) && uniformScale) extra |= kExtraUniformScale;
        if (extra) more |= kMoreAnimExtra;
        if (more) mask |= kMaskMore;
        Writer e;
        size_t mark = 0;
        const auto count = [&](uint32_t UpdateBytes::*field) {
            counted.*field += uint32_t(e.bytes.size() - mark);
            mark = e.bytes.size();
        };
        e.u8(uint8_t(i));
        e.u8(mask);
        if (more) e.u8(more);
        count(&UpdateBytes::head);
        if (mask & kMaskX) e.i16(a.state.pos[0]);
        if (mask & kMaskY) e.i16(a.state.pos[1]);
        if (mask & kMaskZ) e.i16(a.state.pos[2]);
        count(&UpdateBytes::position);
        if (mask & kMaskYaw) e.i16(a.state.rot[1]);
        if (mask & kMaskRotXZ) {
            e.i16(a.state.rot[0]);
            e.i16(a.state.rot[2]);
        }
        count(&UpdateBytes::rotation);
        if (mask & kMaskAnim) e.u8(a.state.anim);
        if (extra) e.u8(extra);
        if (extra & kExtraAge) e.u8(age);
        if (extra & kExtraKey) {
            e.u16(a.state.animKey);
            e.u8(a.state.animTicks);
        }
        count(&UpdateBytes::animation);
        if (more & kMoreScale) {
            if (uniformScale) e.i16(v.scale[0]);
            else
                for (const int16_t c : v.scale) e.i16(c);
        }
        if (more & kMoreTint) e.u16(v.tint);
        if (more & kMoreParts) {
            e.u16(uint16_t(v.hiddenParts));
            e.u8(uint8_t(v.hiddenParts >> 16));
        }
        count(&UpdateBytes::visual);
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
        count(&UpdateBytes::aim);
        if (extra & kExtraPaint) {
            e.u8(uint8_t(a.state.paint.size()));
            for (const NetPartPaint& p : a.state.paint) {
                e.u8(p.part);
                e.u16(p.colour);
                for (const uint8_t c : p.scale) e.u8(c);
                e.u8(p.semi);
            }
        }
        if (extra & kExtraLook) {
            e.u8(a.state.visual.boneScale ? kLookBoneScale : uint8_t(0));
            for (const uint8_t c : a.state.visual.rootScale) e.u8(c);
            e.u8(a.state.visual.semi);
        }
        if (extra & kExtraReport) e.u8(a.state.report);
        count(&UpdateBytes::paint);
        if (more & kMoreSolids) {
            e.u8(uint8_t(a.state.solids.size()));
            for (const NetSolid& s : a.state.solids) {
                e.u8(uint8_t(uint8_t(s.type) | (s.param != 0 ? kSolidHasParam : 0)));
                e.u8(uint8_t(int8_t(s.bone)));
                if (s.param != 0) e.u32(s.param);
            }
        }
        count(&UpdateBytes::solids);
        if (more & kMoreLock) {
            // the positions alone, when the client is known to hold what else the points are
            bool positionsOnly = a.state.lock.size() == k.acked.lock.size() && !a.state.lock.empty();
            for (size_t n = 0; positionsOnly && n < a.state.lock.size(); ++n)
                positionsOnly = a.state.lock[n].shape == k.acked.lock[n].shape && a.state.lock[n].owner == k.acked.lock[n].owner &&
                                a.state.lock[n].harmless == k.acked.lock[n].harmless;
            bool wide = false, sameOwner = true;
            for (const NetLockPoint& p : a.state.lock) {
                for (const int16_t c : p.offset) wide = wide || c < -127 * kLockGrain || c > 127 * kLockGrain;
                sameOwner = sameOwner && p.owner == a.state.lock.front().owner && p.harmless == a.state.lock.front().harmless;
            }
            e.u8(uint8_t(a.state.lock.size() | (positionsOnly ? kLockPositionsOnly : 0) | (wide ? kLockWide : 0) |
                         (!sameOwner ? kLockOwners : 0)));
            const auto owner = [&](const NetLockPoint& p) {
                e.u16(uint16_t(p.owner));
                e.u8(uint8_t(p.owner >> 16));
                e.u8(p.harmless ? 1 : 0);
            };
            if (!positionsOnly && sameOwner && !a.state.lock.empty()) owner(a.state.lock.front());
            for (const NetLockPoint& p : a.state.lock) {
                for (const int16_t c : p.offset) {
                    if (wide) e.i16(c);
                    else e.u8(uint8_t(int8_t(c >= 0 ? (c + kLockGrain / 2) / kLockGrain : (c - kLockGrain / 2) / kLockGrain)));
                }
                if (positionsOnly) continue;
                e.u16(p.shape);
                if (!sameOwner) owner(p);
            }
        }
        count(&UpdateBytes::lock);
        if (more & kMorePath) {
            e.u8(animAgeByte(table.tick, a.state.pathTick));
            // which steps are not zero: bits 0..2 the position's, 3..5 the rotation's, 6 the fall
            uint8_t steps = a.state.fallStep != 0 ? 0x40 : 0;
            for (size_t l = 0; l < 3; ++l) {
                if (a.state.posStep[l] != 0) steps |= uint8_t(1u << l);
                if (a.state.rotStep[l] != 0) steps |= uint8_t(8u << l);
            }
            e.u8(steps);
            for (size_t l = 0; l < 3; ++l)
                if (a.state.posStep[l] != 0) e.i16(a.state.posStep[l]);
            for (size_t l = 0; l < 3; ++l)
                if (a.state.rotStep[l] != 0) e.i16(a.state.rotStep[l]);
            if (steps & 0x40) e.i16(a.state.fallStep);
        }
        count(&UpdateBytes::path);
        // past the budget the rest waits for the next update (a burst spreads over a few), one actor at least goes
        const size_t after = w.bytes.size() + e.bytes.size();
        if (after > kNetMaxPayload || (after > kNetUpdateBudget && !sent.entries.empty())) {
            cursor_ = i; // the next update starts with what did not fit
            cut = true;
            break;
        }
        w.bytes.insert(w.bytes.end(), e.bytes.begin(), e.bytes.end());
        sent.entries.push_back({uint8_t(i), a.generation, a.state});
        k.sent = a.state;
    }
    counted.total = uint32_t(w.bytes.size());
    if (counted.total > largest_.total) largest_ = counted;
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
    if (o.startNoted == s.animTick) return; // a resend
    o.startNoted = s.animTick;
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
        if (kind == kEventEffect) {
            NetEffect e;
            e.slot = r.u8();
            e.pool = r.u8();
            e.type = r.u8();
            e.subtype = r.u8();
            e.variant = r.u8();
            e.spawnByte = r.u8();
            e.loopFlags = r.u8();
            e.flags = r.u8();
            e.parent = r.u8();
            e.second = r.u8();
            e.subState = r.u8();
            e.step = r.u8();
            e.stepCounter = r.u8();
            for (int16_t& v : e.trail) v = r.i16();
            e.bone = r.u8();
            e.owner = r.u8();
            for (uint8_t& p : e.params) p = r.u8();
            for (int16_t& v : e.pos) v = r.i16();
            e.spare = r.i16();
            if (e.pool == kStaticClass || e.pool == kProjectileClass) {
                for (int16_t& v : e.rot) v = r.i16();
                for (int16_t& v : e.velocity) v = r.i16();
            }
            due_.push_back({tick, false, nullptr, e, std::nullopt, std::nullopt});
            ++eventsArrived_;
            continue;
        }
        if (kind == kEventShift) {
            due_.push_back({tick, false, nullptr, std::nullopt, r.u8(), std::nullopt});
            ++eventsArrived_;
            continue;
        }
        if (kind == kEventRoomPatch) {
            NetRoomPatch p;
            p.offset = r.u32();
            p.bytes.resize(r.u8());
            for (uint8_t& b : p.bytes) b = r.u8();
            due_.push_back({tick, false, nullptr, std::nullopt, std::nullopt, std::move(p)});
            ++eventsArrived_;
            continue;
        }
        const uint8_t slot = r.u8();
        if (kind == kEventFlash) {
            if (const std::shared_ptr<Occupant>& o = heardOf_[slot]) {
                Due d{tick, false, o, std::nullopt, std::nullopt, std::nullopt};
                d.flash = true;
                due_.push_back(std::move(d));
            }
        } else if (kind == kEventGo) {
            if (std::shared_ptr<Occupant>& o = heardOf_[slot]) {
                due_.push_back({tick, false, o, std::nullopt, std::nullopt, std::nullopt});
                o = nullptr;
            } else if (slot >= kShotClass && slot < kShotClass + ShotPool::size()) {
                // a shot run here from its event (a beam, a swing: they last until the world's ends)
                Due d{tick, false, nullptr, std::nullopt, std::nullopt, std::nullopt};
                d.goShot = slot;
                due_.push_back(std::move(d));
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
            o->last.pathTick = tick;
            o->samples.push_back({tick, o->last});
            noteStart(*o, o->last);
            heardOf_[slot] = o;
            due_.push_back({tick, true, o, std::nullopt, std::nullopt, std::nullopt});
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
        const uint8_t extra = (more & kMoreAnimExtra) ? r.u8() : uint8_t(0);
        const uint32_t age = (extra & kExtraAge) ? r.u8() : 0u;
        if (extra & kExtraKey) {
            s.animKey = r.u16();
            s.animTicks = r.u8();
        }
        if (mask & kMaskAnim) {
            s.animTick = tick - age;
            s.animKeyed = (extra & kExtraKey) != 0;
            s.held = (extra & kExtraHeld) != 0;
        }
        if (more & kMoreScale) {
            if (extra & kExtraUniformScale) s.visual.scale.fill(r.i16());
            else
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
        if (extra & kExtraPaint) {
            s.paint.resize(r.u8());
            for (NetPartPaint& p : s.paint) {
                p.part = r.u8();
                p.colour = r.u16();
                for (uint8_t& c : p.scale) c = r.u8();
                p.semi = r.u8();
            }
        }
        if (extra & kExtraLook) {
            s.visual.boneScale = (r.u8() & kLookBoneScale) != 0;
            for (uint8_t& c : s.visual.rootScale) c = r.u8();
            s.visual.semi = r.u8();
            s.told |= kNetToldLook;
        }
        if (extra & kExtraReport) s.report = r.u8();
        if (more & kMoreSolids) {
            s.solids.resize(r.u8());
            for (NetSolid& v : s.solids) {
                const uint8_t type = r.u8();
                v.type = int16_t(type & ~kSolidHasParam);
                v.bone = int16_t(int8_t(r.u8()));
                v.param = (type & kSolidHasParam) ? r.u32() : 0;
            }
        }
        if (more & kMoreLock) {
            const uint8_t head = r.u8();
            const bool positionsOnly = (head & kLockPositionsOnly) != 0, wide = (head & kLockWide) != 0, owners = (head & kLockOwners) != 0;
            s.lock.resize(head & kLockCountMask);
            const auto owner = [&](NetLockPoint& p) {
                p.owner = r.u16();
                p.owner |= uint32_t(r.u8()) << 16;
                p.harmless = r.u8() != 0;
            };
            NetLockPoint shared;
            if (!positionsOnly && !owners && !s.lock.empty()) owner(shared);
            for (NetLockPoint& p : s.lock) {
                for (int16_t& c : p.offset) c = wide ? r.i16() : int16_t(int(int8_t(r.u8())) * kLockGrain);
                if (positionsOnly) continue;
                p.shape = r.u16();
                if (owners) owner(p);
                else {
                    p.owner = shared.owner;
                    p.harmless = shared.harmless;
                }
            }
        }
        if (more & kMorePath) {
            s.pathTick = tick - r.u8();
            const uint8_t steps = r.u8();
            for (size_t l = 0; l < 3; ++l) s.posStep[l] = (steps >> l & 1) ? r.i16() : int16_t(0);
            for (size_t l = 0; l < 3; ++l) s.rotStep[l] = (steps >> (3 + l) & 1) ? r.i16() : int16_t(0);
            s.fallStep = (steps & 0x40) ? r.i16() : int16_t(0);
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
        const bool told = changed[i] && o->appearedAtEvent <= eventsThrough;
        if (told) {
            o->last = *changed[i];
            noteStart(*o, o->last);
        }
        if (netSlotOnPath(uint8_t(i))) { // a path holds until the next, from its own tick
            if (told) {
                while (!o->samples.empty() && int32_t(o->samples.back().tick - o->last.pathTick) >= 0) o->samples.pop_back();
                o->samples.push_back({o->last.pathTick, o->last});
            }
            continue;
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
        if (d.effect) {
            actors_.effect(*d.effect);
            continue;
        }
        if (d.shift) {
            actors_.coordinateShift(*d.shift);
            continue;
        }
        if (d.patch) {
            actors_.roomPatch(*d.patch);
            continue;
        }
        if (d.goShot) {
            actors_.go(*d.goShot);
            continue;
        }
        const uint8_t slot = d.occupant->slot;
        if (d.flash) {
            if (shown_[slot] == d.occupant) d.occupant->flashNow = true;
            continue;
        }
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
        if (netSlotOnPath(o.slot)) {
            const int age = int(int32_t(tick - s.pathTick));
            for (size_t i = 0; i < 3; ++i) {
                s.pos[i] = int16_t((int(s.pos[i]) * 16 + int(s.posStep[i]) * age + (i == 1 ? int(s.fallStep) * age * (age - 1) / 2 : 0)) >> 4);
                s.rot[i] = int16_t(int(s.rot[i]) + int(s.rotStep[i]) * age);
            }
        } else if (o.samples.size() >= 2 && int32_t(tick - o.samples[0].tick) > 0) {
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
        if (o.flashNow) {
            actors_.flash(o.slot);
            o.flashNow = false;
        }
    }
}

size_t NetStateClient::shownCount() const {
    size_t n = 0;
    for (const auto& o : shown_)
        if (o) ++n;
    return n;
}

// ---- the client on the game's pools ---------------------------------------------------------------------------------

ActorHeader* PoolNetActors::slotOf(uint8_t slot) const {
    if (shotsInOwnSlots_ && actorSlotClass(slot) == kShotClass)
        return ownSlot_[slot] != 0 ? &actorHeaderAt(ownSlot_[slot]) : nullptr;
    return &actorHeaderAt(actorBySlot(slot));
}

void PoolNetActors::appear(uint8_t slot, const NetActorIdentity& identity, const NetActorState& state) {
    if (shotsInOwnSlots_ && actorSlotClass(slot) == kShotClass) {
        ownSlot_[slot] = allocActor10();
        if (ownSlot_[slot] == 0) return; // the pool is full here: this shot is not seen
    }
    ActorHeader& head = ownSlot_[slot] != 0 ? actorHeaderAt(ownSlot_[slot]) : claimActorSlot(slot);
    head.flags = kActorLiveDrawn;
    head.runFlags &= uint8_t(~kActorRun); // the pool loops pass it by: its class runs in the world
    head.state = 1;                       // past a class's first state: the draw pass passes over a static still in it
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
    const bool bound = identity.modelEntry != kNetNoModel && bindActorModelEntry(a, identity.modelEntry);
    animates_[slot] = bound && actorSlotClass(slot) == kAnimatedClass;
    if (netSlotVisibleOrShot(slot)) appeared_.push_back({slot, identity, bound});
}

void PoolNetActors::go(uint8_t slot) {
    // a copy that ended here by itself: its slot may hold a shot of this machine's since, which is not the world's to end
    const bool stale = ownSlot_[slot] != 0 && shotSlotSerial(ShotPool::index(asShot(actorHeaderAt(ownSlot_[slot])))) != ownSerial_[slot];
    // ... and one gone already is not freed again: a free of a slot no longer live puts it back in the pool's live mask
    // (applyActorFrees toggles the freed bits), a slot lost for good -- the shots' pool runs out and the buster stops
    if (ActorHeader* head = stale ? nullptr : slotOf(slot); head && (head->flags & kActorLive)) {
        if (actorSlotClass(slot) == kShotClass) freeActor(asShot(*head)); // freeActor(ActorHeader&), as the exe, has no case for shots
        else freeActor(*head);
    }
    ownSlot_[slot] = 0;
    animates_[slot] = false;
    lock_[slot].clear();
    solids_[slot].clear();
}

void PoolNetActors::coordinateShift(uint8_t shift) { stage().coordinateShift = shift; }

void PoolNetActors::roomPatch(const NetRoomPatch& patch) {
    const uint32_t block = roomGrid().block;
    for (size_t i = 0; i < patch.bytes.size(); ++i) GameStorage::Field<uint8_t>{block + patch.offset + uint32_t(i)} = patch.bytes[i];
}

void PoolNetActors::effect(const NetEffect& e) {
    static const bool logged = std::getenv("OD_NET_EFFECT_LOG") != nullptr; // test instrument
    // what it hangs from: an actor held here, or (a player's slot) this machine's Mega Man
    Actor* parent = nullptr;
    if (e.parent != kNetNoParent) {
        // ... held in a slot of this client's, for a shot; a player's by this machine's table
        ActorHeader* head = actorSlotClass(e.parent) == kPlayerClass ? &actorHeaderAt(playerAddress(tableOfPlayer(e.parent))) : slotOf(e.parent);
        if (!head || (!(head->flags & kActorLive) && actorSlotClass(e.parent) != kPlayerClass)) return;
        parent = &actorLayoutOf(*head);
    }
    // a projectile or a static runs in the world's slot (the world's are held here in theirs: one in a slot of its own would
    // be overwritten by the next the world starts there); an effect (a pool the world's are not held in) in a free one
    const bool worldSlot = (e.pool == kProjectileClass || e.pool == kStaticClass) && actorSlotClass(e.slot) == e.pool;
    const uint32_t address = e.pool == kEffectClass ? allocActorA0(e.loopFlags)
                             : e.pool == kShotClass ? allocActor10()
                             : worldSlot            ? claimActorSlotFor(e.slot, e.loopFlags)
                                                    : 0;
    if (address == 0) return; // the pool is full here: this one is not seen
    // a world's actor (not a player's shot, which hits as its owner's): its hits are the world's copy's
    actorHeaderAt(address).worldCopy = e.pool != kShotClass;
    const bool onPlayer = e.parent != kNetNoParent && actorSlotClass(e.parent) == kPlayerClass;
    if (e.pool == kShotClass || onPlayer)
        started_.push_back({e.pool, e.type, e.variant, e.owner, e.parent, onPlayer ? tableOfPlayer(e.parent) : kNetNoParent});
    ActorHeader& head = actorHeaderAt(address);
    head.flags = e.flags;
    head.type = e.type;
    head.subtype = e.subtype;
    head.variant = e.variant;
    head.spawnByte = e.spawnByte;
    for (size_t i = 0; i < 4; ++i) head.params[i] = e.params[i];
    head.enterState(0);
    head.ownerPlayer = tableOfPlayer(e.owner);
    if (logged) {
        const RoutineRef named = e.pool == kShotClass ? actorHandler(head, ActorPool::Shots, false)
                                 : e.pool == kEffectClass ? actorHandler(head, (e.loopFlags & kActorLateLoop) ? ActorPool::EffectsLate : ActorPool::Effects, false)
                                 : e.pool == kProjectileClass ? actorHandler(head, ActorPool::Projectiles, false)
                                                          : actorHandler(head, (e.loopFlags & kActorLateLoop) ? ActorPool::StaticsLate : ActorPool::Statics, false);
        std::fprintf(stderr, "net effect %s\n", named ? routineName(named.id()) : "?");
    }
    if (e.pool == kShotClass) {
        head.loopFlags = e.loopFlags;
        // its class aims it (its first state) as its owner did: the angles it left the owner's hand with
        if (e.trail[6] != 0) setPlayerShotAim(head.ownerPlayer, true, e.trail[4], e.trail[5]);
        ShotActor& shot = asShot(head);
        shot.aimPoint.x = e.trail[0];
        shot.aimPoint.y = e.trail[1];
        shot.aimPoint.z = e.trail[2];
        shot.aimPoint.spare = e.trail[3];
        ActorHeader* target = e.second != kNetNoParent ? slotOf(e.second) : nullptr;
        shot.target = target && (target->flags & kActorLive) ? target : nullptr;
        if (shotsInOwnSlots_) { // the world's go ends it; the slot's occupant count tells it from a shot made there after
            ownSlot_[e.slot] = address;
            ownSerial_[e.slot] = shotSlotSerial(ShotPool::index(asShot(head)));
        }
        ++effectsStarted_;
        return;
    }
    const auto handlerIs = [&](RoutineId id) {
        const RoutineRef h = actorHandler(head, e.pool == kEffectClass
                                                    ? ((e.loopFlags & kActorLateLoop) ? ActorPool::EffectsLate : ActorPool::Effects)
                                                    : ((e.loopFlags & kActorLateLoop) ? ActorPool::StaticsLate : ActorPool::Statics), false);
        return h && h.id() == id;
    };
    if (e.pool == kEffectClass) {
        const RoutineRef h = actorHandler(head, (e.loopFlags & kActorLateLoop) ? ActorPool::EffectsLate : ActorPool::Effects, false);
        if (const NetEffectLinks* links = h ? netEffectLinksOf(h.id()) : nullptr) {
            ActorHeader* second = e.second != kNetNoParent ? slotOf(e.second) : nullptr;
            links->write(asEffect(head), {parent, second});
        }
    }
    if (parent && e.pool == kStaticClass && handlerIs(RoutineId::FootstepDustUpdate))
        classState<FootstepDustFields>(asStatic(head)).parent = parent;
    if (parent && e.pool == kStaticClass && handlerIs(RoutineId::DarkTrailUpdate))
        classState<hyper_shell_state::DarkTrailFields>(asStatic(head)).parent = parent->at;
    if (e.pool == kStaticClass && handlerIs(RoutineId::BossBladeUpdate)) head.setParamsWord(parent ? parent->at : 0);
    if (e.pool == kEffectClass) {
        EffectActor& fx = asEffect(head);
        fx.pos.x = e.pos[0];
        fx.pos.y = e.pos[1];
        fx.pos.z = e.pos[2];
        fx.posSpare = e.spare;
        fx.subState = e.subState;
        fx.step = e.step;
        fx.stepCounter = e.stepCounter;
        for (size_t k = 0; k < fx.trail.size(); ++k) fx.trail[k] = e.trail[k];
    } else if (e.pool == kProjectileClass) {
        ProjectileActor& p = projectileActorAt(address);
        p.pos.x = int32_t(uint32_t(uint16_t(e.pos[0])) << 16);
        p.pos.y = int32_t(uint32_t(uint16_t(e.pos[1])) << 16);
        p.pos.z = int32_t(uint32_t(uint16_t(e.pos[2])) << 16);
        p.rotation.x = e.rot[0];
        p.rotation.y = e.rot[1];
        p.rotation.z = e.rot[2];
        p.velocity.x = e.velocity[0];
        p.velocity.y = e.velocity[1];
        p.velocity.z = e.velocity[2];
        if (const RoutineRef h = actorHandler(head, ActorPool::Projectiles, false); h && h.id() == RoutineId::TurretBulletUpdate) {
            p.step = e.step;
            p.hitBox.result = uint32_t(uint16_t(e.trail[0])) | uint32_t(uint16_t(e.trail[1])) << 16;
            classState<turret_bullet_state::TurretBulletFields>(p).owner = parent ? &asAnimated(*parent) : nullptr;
        }
    } else {
        StaticActor& s = asStatic(head);
        s.pos.x = int32_t(uint32_t(uint16_t(e.pos[0])) << 16);
        s.pos.y = int32_t(uint32_t(uint16_t(e.pos[1])) << 16);
        s.pos.z = int32_t(uint32_t(uint16_t(e.pos[2])) << 16);
        s.rotation.x = e.rot[0];
        s.rotation.y = e.rot[1];
        s.rotation.z = e.rot[2];
        s.velocity.x = e.velocity[0];
        s.velocity.y = e.velocity[1];
        s.velocity.z = e.velocity[2];
        if (handlerIs(RoutineId::WaterSplashUpdate) && (e.variant >> 6) != 0) {
            s.pos.y.frac = uint16_t(e.spare);
            WaterSplashFields& splash = classState<WaterSplashFields>(s);
            if ((e.variant >> 6) == 1) splash.followed = parent;
            else if (parent && actorSlotClass(e.parent) == kPlayerClass) splash.point = &asPlayer(*parent).bonePoints[e.bone];
        }
    }
    ++effectsStarted_;
}

void PoolNetActors::flash(uint8_t slot) {
    ActorHeader* head = slotOf(slot);
    if (!head) return;
    uint16_t& tint = actorSlotClass(slot) == kAnimatedClass ? asAnimated(actorLayoutOf(*head)).tint : meshActorAt(head->at).tint;
    if (!flashed_[slot]) flashed_[slot] = tint;
    tint = kTintFlash;
}

void PoolNetActors::tick(uint8_t slot, const NetActorState& state, const std::optional<AnimStart>& start) {
    ActorHeader* head = slotOf(slot);
    if (!head) return;
    Actor& a = actorLayoutOf(*head);
    if (const std::optional<uint16_t> before = std::exchange(flashed_[slot], std::nullopt)) { // the flash was one tick
        if (actorSlotClass(slot) == kAnimatedClass) asAnimated(a).tint = *before;
        else meshActorAt(a.at).tint = *before;
    }
    a.savePreviousPose(); // what the renderer interpolates from
    lock_[slot] = state.lock;
    solids_[slot] = state.solids;
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
        if (state.told & kNetToldLook) {
            mesh.scaleBytes = {state.visual.rootScale[0], state.visual.rootScale[1], state.visual.rootScale[2]};
            mesh.semiTransparency = state.visual.semi;
        }
        return;
    }
    asAnimated(a).stepCounter = state.report; // the copies of the parts it started read it (a boss's blades)
    if (!animates_[slot]) return;
    AnimatedActor& animated = asAnimated(a);
    if (state.told & kNetToldScale) {
        animated.scale.x = state.visual.scale[0];
        animated.scale.y = state.visual.scale[1];
        animated.scale.z = state.visual.scale[2];
    }
    if (state.told & kNetToldTint) animated.tint = state.visual.tint;
    if (state.told & kNetToldLook) {
        animated.rootScaleBytes = {state.visual.rootScale[0], state.visual.rootScale[1], state.visual.rootScale[2]};
        animated.semiTransparency = state.visual.semi;
        animated.runFlags = state.visual.boneScale ? uint8_t(animated.runFlags | kActorBoneScale) : uint8_t(animated.runFlags & ~kActorBoneScale);
    }
    // the parts as the class painted them; the others as the model binds them
    for (int k = 0, next = 0; k < int(animated.partCount) && k < 32; ++k) {
        BoneBlock* b = boneBlockOf(animated, k >> 2);
        if (!b) break;
        const size_t lane = size_t(k & 3);
        if (next < int(state.paint.size()) && state.paint[size_t(next)].part == k) {
            const NetPartPaint& p = state.paint[size_t(next++)];
            b->colour(lane) = p.colour;
            b->setScaleWord(lane, uint32_t(p.scale[0]) | uint32_t(p.scale[1]) << 8 | uint32_t(p.scale[2]) << 16 | uint32_t(p.semi) << 24);
        } else {
            b->colour(lane) = kTintNeutral;
            b->setScaleWord(lane, uint32_t(kScaleByteOne) * 0x010101u);
        }
    }
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

bool PoolNetActors::heldShot(size_t index) const {
    if (index >= ShotPool::size()) return false;
    // the copy that is still in that slot (one that ended there by itself is not: a shot of this machine may have the slot
    // since, a new occupant)
    const uint32_t at = shotPool()[index].at, serial = shotSlotSerial(index);
    for (size_t i = kShotClass; i < kShotClass + ShotPool::size(); ++i)
        if (ownSlot_[i] == at && ownSerial_[i] == serial) return true;
    return false;
}

ShotActor* PoolNetActors::heldCopy(uint8_t worldSlot) const {
    if (ownSlot_[worldSlot] == 0) return nullptr;
    ShotActor& s = asShot(actorHeaderAt(ownSlot_[worldSlot]));
    if (!(s.flags & kActorLive) || shotSlotSerial(ShotPool::index(s)) != ownSerial_[worldSlot]) return nullptr;
    return &s;
}

bool PoolNetActors::shotRunsFor(uint8_t tablePlayer) const {
    for (size_t i = kShotClass; i < kShotClass + ShotPool::size(); ++i) {
        if (ownSlot_[i] == 0) continue;
        const ActorHeader& h = actorHeaderAt(ownSlot_[i]);
        if (shotSlotSerial(ShotPool::index(asShot(actorHeaderAt(ownSlot_[i])))) != ownSerial_[i]) continue; // ended here, the slot is another's
        if (!(h.flags & kActorLive) || !(h.runFlags & kActorRun) || h.ownerPlayer != tablePlayer) continue;
        const RoutineRef c = actorHandler(h, ActorPool::Shots, false); // a swing, a beam, a suction: held while it runs
        if (c && (c.id() == RoutineId::BladeArmShotUpdate || c.id() == RoutineId::ShiningLaserShotUpdate || c.id() == RoutineId::VacuumArmShotUpdate))
            return true;
    }
    return false;
}

void PoolNetActors::submitSolids() {
    for (size_t i = 0; i < kNetSlots; ++i) {
        if (solids_[i].empty()) continue;
        ActorHeader* head = slotOf(uint8_t(i));
        if (!head || !(head->flags & kActorLive)) continue;
        for (const NetSolid& s : solids_[i]) actorCollisionSubmitSolid(*head, SolidType(s.type), s.bone, s.param);
    }
}

bool netClientKeepsActor(const StaticActor& a) {
    const RoutineRef h = actorHandler(a, (a.loopFlags & kActorLateLoop) ? ActorPool::StaticsLate : ActorPool::Statics, false);
    return h && netClientKeepsClass(h.id());
}

bool netClientKeepsAnyActor(const Actor& a) {
    return actorSlotClass(a.slot) == kStaticClass && netClientKeepsActor(static_cast<const StaticActor&>(a));
}

void PoolNetActors::submitLockPoints() {
    size_t box = 0;
    for (size_t i = 0; i < kNetSlots; ++i) {
        if (lock_[i].empty()) continue;
        ActorHeader* head = slotOf(uint8_t(i));
        if (!head || !(head->flags & kActorLive)) continue;
        const Actor& a = actorLayoutOf(*head);
        for (const NetLockPoint& p : lock_[i]) {
            if (box >= lockBoxes_.size()) return;
            const int16_t pos[3] = {int16_t(uint16_t(a.pos.x.whole) + uint16_t(p.offset[0])),
                                    int16_t(uint16_t(a.pos.y.whole) + uint16_t(p.offset[1])),
                                    int16_t(uint16_t(a.pos.z.whole) + uint16_t(p.offset[2]))};
            lockBoxes_[box].result = 0;
            hitListPutEntry(pos, p.shape, uint32_t(i) << kHitSlotShift | p.owner, p.harmless ? kHitAttackHarmless : 0u, lockBoxes_[box]);
            ++box;
        }
    }
}

namespace {

// one pool loop; a class that throws loses the actor it was on
void runLocalPool(const PlayerEnv& env, ActorContext& context, ActorPool pool, uint32_t& cursor, NetLocalRunFailures& failed) {
    for (int guard = 0; guard < 64; ++guard) {
        try {
            updatePool(env, context, pool);
            return;
        } catch (const std::exception& e) {
            ActorHeader& actor = actorHeaderAt(cursor);
            const std::string what = std::string(pool == ActorPool::Effects || pool == ActorPool::EffectsLate ? "effect"
                                                 : pool == ActorPool::Projectiles                              ? "projectile"
                                                                                                               : "static") +
                                     " type " + std::to_string(int(actor.type)) + ": " + e.what();
            if (std::find(failed.classes.begin(), failed.classes.end(), what) == failed.classes.end()) failed.classes.push_back(what);
            freeActor(actor);
        }
    }
}

} // namespace

void runNetLocalActors(const PlayerEnv& env, ActorContext& context, bool late, NetLocalRunFailures& failed) {
    ActorSystemState& s = actorSystem();
    struct Muted { // the world plays what they play, and the client hears it from there
        Muted() { setLocalEffectsMuted(true); }
        ~Muted() { setLocalEffectsMuted(false); }
    } muted;
    // the projectiles run here (a ground shockwave), in the game's order; the world's held here have their run flag off
    if (!late) runLocalPool(env, context, ActorPool::Projectiles, s.projectileCursor, failed);
    runLocalPool(env, context, late ? ActorPool::EffectsLate : ActorPool::Effects, s.effectCursor, failed);
    runLocalPool(env, context, late ? ActorPool::StaticsLate : ActorPool::Statics, s.staticCursor, failed);
}

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
    uint32_t paint = 0x811c9dc5u;
    const auto mix = [&](uint32_t v) { paint = (paint ^ v) * 0x01000193u; };
    if (animated && parts != 0)
        for (int k = 0; k < int(parts); ++k)
            if (const BoneBlock* b = boneBlockOf(*animated, k >> 2)) {
                mix(b->boneColour(size_t(k & 3)));
                mix(b->semiBits(size_t(k & 3)));
            }
    uint32_t hidden = 0;
    if (animated)
        for (uint32_t k = 0; k < std::min<uint32_t>(animated->boneSlotCount, 24); ++k)
            if (animated->parts[k].flags & kPartHidden) hidden |= 1u << k;
    w.u16(animated && parts != 0 ? animated->tint : uint16_t(0));
    w.u32(hidden);
    w.u32(paint);
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

void writeNetPoses(std::FILE* file, uint32_t tick, NetSlotFilter filter, bool client) {
    AnimatedPool& animated = animatedPool();
    for (size_t i = 0; i < kAnimatedFieldSlots; ++i)
        if ((animated[i].flags & kActorLive) && filter(animated[i].slot)) poseRecord(file, tick, animated[i], &animated[i]);
    StaticPool& statics = staticPool();
    for (size_t i = 0; i < StaticPool::size(); ++i)
        if ((statics[i].flags & kActorLive) && filter(statics[i].slot) &&
            !(client ? (statics[i].runFlags & kActorRun) != 0 : staticIsEffect(statics[i]) || netClientKeepsActor(statics[i])))
            poseRecord(file, tick, statics[i], nullptr);
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
        p.paint = r.u32();
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
