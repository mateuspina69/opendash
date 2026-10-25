#include <cstdio>
// The hit list: the submits and the pass (see hit_list.hpp).
#include "game/hit_list.hpp"

#include <stdexcept>
#include <vector>

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/distances.hpp"
#include "game/fixed_point.hpp"
#include "game/lock_on_targets.hpp"
#include "game/net_puppets.hpp"

namespace od::port {

namespace {

// netplay: the hits queued for the next pass, the remote bodies and their boxes
std::vector<ExternalHit> gExternalHits;
std::vector<RemoteBody> gRemoteBodies;
std::vector<HitBox> gRemoteBoxes;

// the group pair table 0x919f2c (exe data)
const DataTable<ScalarTable<uint8_t, 8>> hitGroupTable{TableId::HitGroupPairs};

constexpr uint32_t kSamples = 8;    // a capsule's segment sampled from 8 (its first end) to 0 (its second)
constexpr uint32_t kPairBit = 0x80; // bit 7 of a group's byte, shifted by the other group: the pair is tested

// x86 idiv-by-8 idiom (cdq / and 7 / add / sar 3): truncation toward zero
int div8(int v) { return (v + ((v >> 31) & 7)) >> 3; }

// one end of an entry as a sphere (the pass's scratch, the exe's globals 0x6d0120..0x6d014f)
struct Sphere {
    int16_t x, y, z, r;
};
// an entry's sphere (its centre and its radius: the shape's radius bits << 2)
Sphere sphereOf(const HitEntry& e) {
    return {e.pos.x, e.pos.y, e.pos.z, int16_t((uint16_t(e.shape()) & kHitRadiusMask) << kHitRadiusScale)};
}
// the capsule's sample step bits of an entry (0: a sphere)
int capsuleOf(const HitEntry& e) { return (int(e.shape()) >> kHitCapsuleShift) & 3; }
// the contact output an entry's box asks for
int contactOf(const HitEntry& e) { return int(int32_t(e.shape())) & int(kHitContactMask); }
// the group of an owner word
uint32_t groupOf(uint32_t owner) { return (owner >> kHitGroupShift) & kHitGroupMask; }

// exe 0x5e4ac0(self, other, v): the box value v with the other's damage added when it applies (compared heights: the
// level bits, none / doubled), capped at 0xfff
uint32_t addDamage(const HitEntry& self, const HitEntry& other, uint32_t v) {
    uint32_t total = v & kHitDamageMask, damage = other.attack & kHitDamageMask;
    if ((self.owner & kHitCompareHeights) && (other.owner & kHitCompareHeights)) {
        const int levels =
            (int(uint16_t(self.shape()) & kHitLevelMask) - int(uint16_t(other.shape()) & kHitLevelMask)) >> kHitLevelShift;
        if (levels > 1) {
            v |= kHitLevelAbove;
            damage = 0;
        } else if (levels < -1) {
            v |= kHitLevelBelow;
            if (levels < -3) damage <<= 1;
        } else {
            v |= kHitLevelWithin;
        }
    } else if ((other.owner & kHitGroupsAll) == 0) { // the player deals no contact damage
        damage = 0;
    }
    total += damage;
    if (total > kHitDamageMask) total = kHitDamageMask;
    return (v & ~kHitDamageMask) | total;
}

// one side of a hit: the box of `self` takes the other's attack, and the contact output it asks for (the other's
// point, interpolated at sample t of its capsule `otherEnd`; or the other's last move)
void applyHit(const HitEntry& self, const HitEntry& other, int contact, int otherCapsule, const Sphere& o0,
              const Sphere& o1, int t) {
    HitBoxHead& head = *self.box;
    uint32_t v = (other.attack & kHitFlagsMask) | head.result;
    if (int32_t((self.attack & kHitImmunityMask) << groupOf(other.owner)) >= 0) v = addDamage(self, other, v);
    head.result = v;
    if (contact != int(kHitContactPoint) && contact != int(kHitContactMotion)) return;
    // the contact output goes to the bytes after the head: a submitter that asks for it hands a whole HitBox, or (the
    // contact motion only) a projectile's head (its slot's +0xbc), whose next dword is the slot's +0xc4
    if (contact == int(kHitContactMotion)) {
        // the direction and length of the other's last move (its previous x / z)
        const int dx = int(other.previousX) - other.pos.x, dz = int(other.previousZ) - other.pos.z;
        const int angle = atanAngle(float(dx), float(dz));
        const int distance = isqrt(int32_t(uint32_t(dz * dz) + uint32_t(dx * dx)));
        if (self.contactBox) {
            self.contactBox->setContactMotion(angle, distance);
            return;
        }
        if (ProjectileActor* slot = projectileOfHitBox(head)) { // the angle's dword, the distance ORed into its high half
            slot->debrisFlags = uint32_t(uint16_t(uint32_t(angle))) | (uint32_t(distance) << 16);
            return;
        }
    }
    if (!self.contactBox) {
        char what[128];
        std::snprintf(what, sizeof what, "hit list: a contact output asked of an entry with a head-only box (owner slot %02x)",
                      unsigned(self.owner >> kHitSlotShift));
        throw std::runtime_error(what);
    }
    HitBox& box = *self.contactBox; // the contact point
    if (otherCapsule == 0) {
        box.contact.x = o0.x;
        box.contact.y = o0.y;
        box.contact.z = o0.z;
        box.contactKind = 0;
    } else {
        const int c = int(kSamples) - t;
        box.contact.x = wrap16(div8(o0.x * c + o1.x * t));
        box.contact.y = wrap16(div8(o0.y * c + o1.y * t));
        box.contactKind = 1;
        box.contact.z = wrap16(div8(o0.z * c + o1.z * t));
    }
}

// a capsule's bounding sphere: the midpoint of its ends, half its length + both radii
Sphere boundingSphere(const Sphere& a, const Sphere& b) {
    Sphere m{};
    m.x = wrap16((a.x + b.x) >> 1);
    m.y = wrap16((a.y + b.y) >> 1);
    m.z = wrap16((a.z + b.z) >> 1);
    const int dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    m.r = wrap16((isqrt(int32_t(uint32_t(dz * dz) + uint32_t(dy * dy) + uint32_t(dx * dx))) >> 1) + a.r + b.r);
    return m;
}

// exe 0x5e4350(&i, &j): the sphere / capsule test of entries i and j; j is advanced past a capsule's second half
void pairTest(uint32_t i, uint32_t& j) {
    ActorSystemState& s = actorSystem();
    const HitEntry& a = s.hitList[i], b = s.hitList[j];
    const int contactA = contactOf(a), contactB = contactOf(b);
    const Sphere p0 = sphereOf(a), q0 = sphereOf(b);
    const int capsuleA = capsuleOf(a), capsuleB = capsuleOf(b);
    int stepA = 0xffff, stepB = 0xffff;
    Sphere p1{}, q1{}, mp = p0, mq = q0; // the capsules' second ends and bounding spheres
    if (capsuleA) {
        p1 = sphereOf(s.hitList[i + 1]);
        stepA = int(kSamples) >> capsuleA;
        mp = boundingSphere(p0, p1);
    }
    if (capsuleB) {
        q1 = sphereOf(s.hitList[j + 1]);
        stepB = int(kSamples) >> capsuleB;
        mq = boundingSphere(q0, q1);
        ++j;
    }
    // the bounding boxes of the two spheres (unsigned compares)
    const int reach = mp.r + mq.r;
    const uint32_t span = uint32_t(reach * 2);
    if (uint32_t(mp.x - mq.x + reach) > span) return;
    if (uint32_t(mp.y - mq.y + reach) > span) return;
    if (uint32_t(mp.z - mq.z + reach) > span) return;
    // sample both segments (8 = the first end, 0 = the second) until two samples overlap
    int ta = 0;
    for (int sa = int(kSamples);;) {
        int tb = 0;
        for (int sb = int(kSamples);;) {
            const int x = div8(p0.x * sa - q0.x * sb - q1.x * tb + p1.x * ta);
            const int y = div8(p0.y * sa - q0.y * sb - q1.y * tb + p1.y * ta);
            const int z = div8(p0.z * sa - q0.z * sb - q1.z * tb + p1.z * ta);
            const int r = div8(p0.r * sa + q0.r * sb + q1.r * tb + p1.r * ta);
            if (int32_t(uint32_t(z * z) + uint32_t(y * y) + uint32_t(x * x)) <= int32_t(uint32_t(r * r))) {
                applyHit(a, b, contactA, capsuleB, q0, q1, tb);
                applyHit(b, a, contactB, capsuleA, p0, p1, ta);
                return;
            }
            sb -= stepB;
            tb += stepB;
            if (sb <= -1) break;
        }
        sa -= stepA;
        ta += stepA;
        if (sa <= -1) return;
    }
}

// one entry's words written (the exe's submits)
void writeEntry(HitEntry& e, const int16_t pos[3], HitBoxHead& box, HitBox* contactBox, uint32_t owner, uint16_t shape,
                uint32_t attack) {
    e.pos.x = pos[0];
    e.pos.y = pos[1];
    e.pos.z = pos[2];
    e.attack = attack;
    e.setShape(int16_t(shape));
    e.owner = owner;
    e.box = &box;
    e.contactBox = contactBox;
    e.previousX = box.previousX;
    e.previousZ = box.previousZ;
}

void submit(const int16_t pos[3], HitBoxHead& box, HitBox* contactBox, uint32_t ownerShape, uint32_t attack) {
    ActorSystemState& s = actorSystem();
    const uint32_t n = s.hitCount;
    if (n >= kHitListCapacity) return;
    s.hitCount = n + 1;
    auto&& e = s.hitList[n];
    writeEntry(e, pos, box, contactBox, ownerShape & kHitOwnerMask, uint16_t(ownerShape), attack);
    noteHitEntry(uint8_t(ownerShape >> kHitSlotShift), pos, ownerShape, attack); // netplay: the shared actors
}

void submitCapsule(const int16_t a[3], uint16_t ra, const int16_t b[3], uint16_t rb, HitBoxHead& box,
                   HitBox* contactBox, uint32_t ownerShape, uint32_t attack) {
    ActorSystemState& s = actorSystem();
    const uint32_t n = s.hitCount;
    if (n >= kHitListCapacity - 1) return;
    const uint16_t levelAndCapsule = uint16_t(ownerShape & kHitShapeAboveRadius);
    for (uint32_t k = 0; k < 2; ++k) {
        auto&& e = s.hitList[n + k];
        writeEntry(e, k ? b : a, box, contactBox, ownerShape & kHitOwnerMask, uint16_t(levelAndCapsule | (k ? rb : ra)),
                   attack);
    }
    s.hitCount = n + 2;
    noteHitEntry(uint8_t(ownerShape >> kHitSlotShift), a, ownerShape, attack); // netplay: the shared actors
}

} // namespace

uint8_t hitGroupPairs(uint32_t group) { return hitGroupTable[group]; }

void hitListSubmit(const int16_t pos[3], HitBoxHead& box, uint32_t ownerShape, uint32_t attack) {
    submit(pos, box, nullptr, ownerShape, attack);
}
void hitListSubmit(const int16_t pos[3], HitBox& box, uint32_t ownerShape, uint32_t attack) {
    submit(pos, box, &box, ownerShape, attack);
}

void hitListSubmitCapsule(const int16_t a[3], uint16_t ra, const int16_t b[3], uint16_t rb, HitBoxHead& box,
                          uint32_t ownerShape, uint32_t attack) {
    submitCapsule(a, ra, b, rb, box, nullptr, ownerShape, attack);
}
void hitListSubmitCapsule(const int16_t a[3], uint16_t ra, const int16_t b[3], uint16_t rb, HitBox& box,
                          uint32_t ownerShape, uint32_t attack) {
    submitCapsule(a, ra, b, rb, box, &box, ownerShape, attack);
}

void hitListQueueExternal(const ExternalHit& hit) { gExternalHits.push_back(hit); }

void hitListSetRemoteBodies(std::vector<RemoteBody> bodies) {
    gRemoteBodies = std::move(bodies);
    gRemoteBoxes.assign(gRemoteBodies.size(), HitBox{});
}

const HitBox& hitListRemoteBodyBox(size_t index) { return gRemoteBoxes.at(index); }

namespace {

// the queued external hits on their targets' entries among the first n (after the pairs: the boxes' results kept)
void applyExternalHits(uint32_t n) {
    ActorSystemState& s = actorSystem();
    for (const ExternalHit& h : gExternalHits) {
        for (uint32_t i = 0; i < n; ++i) {
            const HitEntry& e = s.hitList[i];
            if ((e.owner >> kHitSlotShift) != h.targetSlot) continue;
            HitEntry attacker;
            attacker.pos.x = h.contact[0];
            attacker.pos.y = h.contact[1];
            attacker.pos.z = h.contact[2];
            attacker.attack = h.attack;
            attacker.owner = hitOwner(0xff) | hitGroup(kHitGroupPlayerAttacks);
            attacker.previousX = h.contact[0];
            attacker.previousZ = h.contact[2];
            const Sphere at = sphereOf(attacker);
            applyHit(e, attacker, contactOf(e), 0, at, at, 0);
            break;
        }
    }
    gExternalHits.clear();
}

} // namespace

void hitListPass() {
    ActorSystemState& s = actorSystem();
    for (size_t k = 0; k < gRemoteBodies.size(); ++k) {
        const RemoteBody& b = gRemoteBodies[k];
        HitBox& box = gRemoteBoxes[k];
        box.result = 0;
        box.previousX = b.pos[0];
        box.previousZ = b.pos[2];
        hitListSubmit(b.pos, box,
                      hitOwner(b.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | kHitContactPoint |
                          hitRadius(b.radius),
                      b.attack);
    }
    const uint32_t n = s.hitCount;
    s.hitCount = 0;
    selectLockOnTargets(n, kHitGroupsAll);
    if (n < 2) {
        applyExternalHits(n);
        noteHitPassEnded();
        return;
    }
    for (uint32_t i = 0; i < n - 1;) {
        const HitEntry& ei = s.hitList[i];
        const uint32_t step = (uint16_t(ei.shape()) & kHitCapsuleMask) ? 2 : 1;
        const uint32_t owner = ei.owner;
        const uint8_t pairs = hitGroupPairs(groupOf(owner));
        for (uint32_t j = step + i; j < n; ++j) {
            const uint32_t other = s.hitList[j].owner;
            if (((uint32_t(pairs) << groupOf(other)) & kPairBit) && ((other ^ owner) & kHitSlotMask)) pairTest(i, j);
        }
        i += step;
    }
    applyExternalHits(n);
    noteHitPassEnded();
}

} // namespace od::port
