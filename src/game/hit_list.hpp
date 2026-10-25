// The hit list: every actor that can hit or be hit submits entries during
// its update (exe 0x5e4120 / 0x5e4190); the pass at the end of the frame body (exe 0x5e4260) picks the lock-on
// candidates (game/lock_on_targets), then tests every pair of entries whose groups collide and whose owners differ
// (spheres or capsules) and hands each side the other's attack in its result box (damage, flags, a contact output).
// The entries live in the actor system's globals (game/actor_pools ActorSystemState::hitList, HitEntry).
//
// An entry's words, as the submitters build them:
//   owner | shape (the submit's third argument): the owner's slot << 24, its group << 21, kHitCompareHeights /
//            kHitNoLockOn; the shape (low 16 bits): the radius / 4, the height level << 9, a capsule's sample step,
//            the contact output the box asks for
//   attack: the damage (& kHitDamageMask) | the flags ORed into the other's box (& kHitFlagsMask) | the group
//            immunities (bit 31 - g: no damage from group g)
// A result box (HitBoxHead / HitBox, game/actor_state) has the attack's layout: the damage accumulated in the low 12
// bits, the other side's flags ORed in, the pass's own result bits (kHitLevel*, kHitLockedTarget,
// kHitClosestCandidate).
#pragma once

#include <cstdint>

namespace od::port {

struct HitBoxHead;
struct HitBox;

// ---- the owner word -----------------------------------------------------------------------------------------------
constexpr uint32_t kHitSlotShift = 24;
constexpr uint32_t kHitGroupShift = 21;
constexpr uint32_t kHitGroupMask = 7;
constexpr uint32_t kHitOwnerMask = 0xfff80000; // the owner word's bits of the submit's third argument
constexpr uint32_t kHitSlotMask = 0xff000000;
constexpr uint32_t hitOwner(uint8_t slot) { return uint32_t(slot) << kHitSlotShift; }
constexpr uint32_t hitGroup(uint32_t group) { return group << kHitGroupShift; }
// the groups (the pair table 0x919f2c says which groups collide; the meanings are the users' in the ported classes)
constexpr uint32_t kHitGroupPlayer = 0;        // the player
constexpr uint32_t kHitGroupPlayerAttacks = 1; // the player's shots, what it throws
constexpr uint32_t kHitGroupBodies = 2;        // enemies, NPCs, props
constexpr uint32_t kHitGroupHeavyThrown = 3;   // a heavy thrown box
constexpr uint32_t kHitGroupLiftable = 5;      // a resting lift box, a carried NPC (hypothesis: things you can pick up)
constexpr uint32_t kHitGroupsAll = 0xe00000;   // every group bit of the owner word (the lock-on selection's mask)
constexpr uint32_t kHitCompareHeights = 0x100000; // damage only within one height level of the other's
constexpr uint32_t kHitNoLockOn = 0x80000;        // never the player's lock-on target

// ---- the shape (the low 16 bits) ----------------------------------------------------------------------------------
constexpr uint32_t hitRadius(uint32_t units) { return units >> 2; }
constexpr uint32_t hitHeightLevel(uint32_t level) { return level << 9; }
constexpr uint32_t kHitRadiusMask = 0x1ff; // the radius / 4
constexpr uint32_t kHitRadiusScale = 2;    // ... << 2: the radius
constexpr uint32_t kHitShapeAboveRadius = 0xfe00; // the shape's bits above the radius (level, capsule, contact)
constexpr uint32_t kHitLevelMask = 0xe00;  // the height level (<< 9)
constexpr uint32_t kHitLevelShift = 9;
constexpr uint32_t kHitCapsuleMask = 0x3000; // a capsule: this entry and the next are its ends, sampled 8 >> k steps
constexpr uint32_t kHitCapsuleShift = 12;
constexpr uint32_t hitCapsule(uint32_t k) { return k << kHitCapsuleShift; } // sampled every 8 >> k steps
constexpr uint32_t kHitContactMask = 0xc000;                                // the contact output the box asks for:
constexpr uint32_t kHitContactPoint = 0x4000;  // ... the contact point (on a capsule: interpolated, contactKind 1)
constexpr uint32_t kHitContactMotion = 0x8000; // ... the angle and the distance of the other's last move

// ---- the attack word ------------------------------------------------------------------------------------------------
constexpr uint32_t kHitDamageMask = 0xfff;
constexpr uint32_t kHitFlagsMask = 0xfff000;        // the flags ORed into the other's box
constexpr uint32_t kHitImmunityMask = 0xff000000;   // bit 31 - g: no damage from group g
constexpr uint32_t hitImmunity(uint32_t group) { return 0x80000000u >> group; }
constexpr uint32_t kHitAttackBurning = 0x1000;      // a burning body's touch (hypothesis: the flames' attack kind)
constexpr uint32_t kHitAttackWater = 0x2000;        // the extinguisher's spray (what it puts out or washes checks it)
constexpr uint32_t kHitAttackBump = 0x10000;        // a body's touch (hypothesis: no attack kind of its own)
constexpr uint32_t kHitAttackSolid = 0x20000;       // a solid body: the other's shots burst on it
constexpr uint32_t kHitAttackByPlayer = 0x40000;    // the attack kinds the enemies take damage from (& 0x1c0000): the
constexpr uint32_t kHitAttackThrown = 0x80000;      // player's own, a thrown object's,
constexpr uint32_t kHitAttackStrong = 0x100000;     // a strong one's
constexpr uint32_t kHitAttackDeflects = 0x200000;   // a deflecting body: the player's shots turn away from it
constexpr uint32_t kHitAttackStrongPush = 0x400000; // pushes what it hits whatever its threshold (the special weapons)
constexpr uint32_t kHitAttackHarmless = 0x800000;   // not counted as a near enemy by the lock-on pass

// ---- the pass's own bits in a result box ----------------------------------------------------------------------------
constexpr uint32_t kHitLevelAbove = 0x1000000;   // compared heights: two levels or more above the other (no damage)
constexpr uint32_t kHitLevelWithin = 0x2000000;  // ... within one level
constexpr uint32_t kHitLevelBelow = 0x4000000;   // ... two levels or more below (four or more: the damage doubled)
constexpr uint32_t kHitLockedTarget = 0x8000000; // an entry of the player's locked actor
constexpr uint32_t kHitClosestCandidate = 0x10000000; // the lock-on candidate closest to the player

// The group pair table 0x919f2c (exe data, 8 bytes): bit (7 - h) of byte g = groups g and h are tested against each
// other; kHitGroupLockOn = the group's entries are lock-on candidates
constexpr uint8_t kHitGroupLockOn = 0x40;
uint8_t hitGroupPairs(uint32_t group);

// exe 0x5e4120(pos, box, ownerShape, attack): one entry at the s16 point (none when the list is full), its result box
// `box` (the owner's previous x / z copied from it)
void hitListSubmit(const int16_t pos[3], HitBoxHead& box, uint32_t ownerShape, uint32_t attack);
// ... with a box that takes a contact output
void hitListSubmit(const int16_t pos[3], HitBox& box, uint32_t ownerShape, uint32_t attack);
// exe 0x5e4190(a, ra, b, rb, box, ownerShape, attack): a capsule -- two entries, its ends a / b with the radii ra / rb
// (the shape's height level and capsule bits on both)
void hitListSubmitCapsule(const int16_t a[3], uint16_t ra, const int16_t b[3], uint16_t rb, HitBoxHead& box,
                          uint32_t ownerShape, uint32_t attack);
void hitListSubmitCapsule(const int16_t a[3], uint16_t ra, const int16_t b[3], uint16_t rb, HitBox& box,
                          uint32_t ownerShape, uint32_t attack);
// exe 0x5e4260: the lock-on selection (exe 0x5e4b60 over every group), then every pair of entries whose groups collide
// and whose owners differ tested (exe 0x5e4350); the list emptied
void hitListPass();

} // namespace od::port
