// The pool-0xe0 spawner classes of the sub-ruins' rooms (see subruin_spawners.hpp), one function per exe function.
#include "content/subruins/subruin_spawners.hpp"
#include "content/subruins/subruin_spawners_state.hpp"

#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/encounter.hpp"
#include "game/field_alias.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace subruin_spawners_state;

namespace {

constexpr const char* kSubruinPort = "subruin port"; // the area of its port-gap errors

// the stage setups' layout flags (ST4D's / ST4E's three layouts)
constexpr uint32_t kFlagLayoutA = 0xb0, kFlagLayoutB = 0xb1, kFlagLayoutC = 0xb2;

enum class GroupSpawnerState : uint8_t { Init = 0, Run = 1 };
// ST4E's records (exe data, 0x18 bytes each) and the flags / ids its variants wait for
constexpr uint32_t kRecordSize = 0x18;
constexpr uint32_t kV1Id = 9, kV4Id = 0x21;
constexpr uint32_t kFlagV3Wave = 0x2d0, kFlagV4Done = 0xb5, kFlagV7Wave = 0x2d4;

// ---- the drop spawner (0x424e10) ------------------------------------------------------------------------------------
// Its parameters: the facing it watches (yaw / 16), the drops (0: 4), its box's half sizes (variant 1, 0x100 units)
struct DropSpawnerParams {
    explicit DropSpawnerParams(const ControllerActor& actor) : DropSpawnerParams(const_cast<ControllerActor&>(actor)) {}
    explicit DropSpawnerParams(ControllerActor& actor) : facing(actor.params[0]), count(actor.params[1]), halfX(actor.params[2]), halfZ(actor.params[3]) {}
    FieldAlias<int8_t, uint8_t> facing;
    FieldAlias<int8_t, uint8_t> count;
    uint8_t& halfX;
    uint8_t& halfZ;
};
enum class DropSpawnerState : uint8_t { Start = 0, Watch = 1, Free = 2 };
constexpr uint8_t kDropSpawnerType = 0x12;
constexpr uint8_t kDroppedType = 0x2a, kDroppedVariant = 2; // the pool-0x20 actors it drops
constexpr int16_t kDropSpawnerHeight = -0x200;
constexpr uint8_t kDropBox = 5;

// ---- the line trigger (0x424fb0) ------------------------------------------------------------------------------------
// Its parameters: what it fires (1 the zone slot's group, 2 the script slot's) and the group
struct LineTriggerParams {
    explicit LineTriggerParams(const ControllerActor& actor) : LineTriggerParams(const_cast<ControllerActor&>(actor)) {}
    explicit LineTriggerParams(ControllerActor& actor) : kind(actor.params[0]), group(actor.params[1]) {}
    FieldAlias<int8_t, uint8_t> kind;
    FieldAlias<int8_t, uint8_t> group;
};
enum class LineAxis : uint8_t { XAtLeast = 0, XAtMost = 1, ZAtLeast = 2, ZAtMost = 3 };
constexpr int8_t kFireZoneGroup = 1, kFireScriptGroup = 2;

// exe 0x422cd0 (state 0 of the group spawners): its own bytes cleared, the run state (the dword store at +8)
void spawnerInit(ControllerActor& c) {
    classState<GroupSpawnerFields>(c).clearOwnBytesBytes();
    c.enterState(uint8_t(GroupSpawnerState::Run));
}
// a drop spawner (the e0 class 0x12) the variants 2 / 4 place: its variant, its point (x, -0x200, z), its facing and
// drops, (variant 1) its box; null when the pool is full
ControllerActor* dropSpawner(uint8_t variant, uint16_t x, uint16_t z, uint8_t yaw, uint8_t count, bool box) {
    const uint32_t address = allocActorE0();
    if (address == 0) return nullptr;
    ControllerActor& c = controllerActorAt(address);
    DropSpawnerParams params(c);
    DropSpawnerFields& f = classState<DropSpawnerFields>(c);
    c.flags = kActorLive;
    c.type = kDropSpawnerType;
    if (variant) c.variant = variant;
    f.point.x = int16_t(x);
    f.point.y = kDropSpawnerHeight;
    f.point.z = int16_t(z);
    params.facing = int8_t(yaw);
    params.count = int8_t(count);
    if (box) {
        params.halfX = kDropBox;
        params.halfZ = kDropBox;
    }
    return &c;
}

// the variants of 0x4227a0 ([0x88bc90 + variant * 4]); the sub-state is the variant's own step
void st4eVariant(ControllerActor& c, uint32_t v) {
    GroupSpawnerFields& f = classState<GroupSpawnerFields>(c);
    const uint8_t st = c.subState;
    switch (v) {
    case 0: case 5: case 6: case 8: // 0x420d20 / 0x420d60 / 0x422b80 / 0x422c20: the group of the variant, once
        if (st == 0) {
            encounterSpawnGroupZoneSlot(int(v));
            c.subState = 1;
        }
        return;
    case 1: // 0x4227e0: layout B with id 9 alive: its record (tracked, kept), step 1; else group 1 (not layout B), step
        // 2. Step 1: once the record's object-state group is marked (layout B, id 9 still alive): its wave of 5 records
        // (untracked), step 2.
        if (st == 0) {
            if (!storyFlag(kFlagLayoutB)) {
                encounterSpawnGroupZoneSlot(1);
            } else if (!enemyDefeated(kV1Id)) {
                f.recordActor = encounterSpawnRecord(stateAddress(TableId::SubruinSpawnersV1Record));
                c.subState = 1;
                return;
            }
            c.subState = 2;
        } else if (st == 1 && storyFlag(kFlagLayoutB) && !enemyDefeated(kV1Id)) {
            const ActorHeader& record = actorHeaderAt(f.recordActor);
            const uint8_t idx = uint8_t(enemyGroupFindFor(record, record.variant));
            if (enemyGroupMarks(int32_t(idx), EnemyGroupMarks::LastTick) == 0) return;
            for (uint32_t k = 0; k < 5; ++k) encounterSpawnRecordUntracked(stateAddress(TableId::SubruinSpawnersV1Wave) + k * kRecordSize);
            c.subState = 2;
        }
        return;
    case 2: // 0x4228c0: group 2; layout A: a drop spawner variant 1 at (-0x400, -0x200, -0x600), 8 drops, in a 0xa00
        // box, facing 0 when the player's z < -0xb00, else 0xc00. Step 1.
        if (st != 0) return;
        encounterSpawnGroupZoneSlot(2);
        if (storyFlag(kFlagLayoutA)) {
            if (player().pos.z.whole < -0xb00) {
                if (dropSpawner(1, 0xfc00, 0xfa00, 0, 8, true)) {
                    c.subState = 1;
                    return;
                }
            } else {
                dropSpawner(1, 0xfc00, 0xfa00, 0xc0, 8, true);
            }
        }
        c.subState = 1;
        return;
    case 3: // 0x422970: group 3; layout C without its wave's flag -> step 1, else step 2. Step 1: the flag -> its wave of
        // 4 records (untracked), step 2.
        if (st == 0) {
            encounterSpawnGroupZoneSlot(3);
            if (!storyFlag(kFlagLayoutC)) {
                c.subState = 2;
            } else if (!storyFlag(kFlagV3Wave)) {
                c.subState = 1;
            }
        } else if (st == 1 && storyFlag(kFlagV3Wave)) {
            for (uint32_t k = 0; k < 4; ++k) encounterSpawnRecordUntracked(stateAddress(TableId::SubruinSpawnersV3Wave) + k * kRecordSize);
            c.subState = 2;
        }
        return;
    case 4: // 0x4229f0: group 4; layouts A / B: a drop spawner variant 0 at (0x200, -0x200, z), 8 (A) / 4 (B) drops,
        // facing 0 at z -0xf00 when the player's z < -0xf00, else 0x800 at z -0x400; layout C not done: its record
        // (untracked). Step 1. Step 1: layout C, not done, id 0x21 defeated -> step 2, done.
        if (st == 0) {
            encounterSpawnGroupZoneSlot(4);
            const bool a0 = storyFlag(kFlagLayoutA);
            if (a0 || storyFlag(kFlagLayoutB)) {
                const uint8_t n = a0 ? 8 : 4;
                const bool near = player().pos.z.whole < -0xf00;
                if (dropSpawner(0, 0x200, near ? 0xf100 : 0xfc00, near ? 0 : 0x80, n, false)) {
                    c.subState = 1;
                    return;
                }
            } else if (!storyFlag(kFlagV4Done)) {
                encounterSpawnRecordUntracked(stateAddress(TableId::SubruinSpawnersV4Record));
            }
            c.subState = 1;
        } else if (st == 1 && storyFlag(kFlagLayoutC) && !storyFlag(kFlagV4Done) && enemyDefeated(kV4Id)) {
            c.subState = 2;
            setStoryFlag(kFlagV4Done);
        }
        return;
    case 7: // 0x422ba0: group 7; layout B without its wave's flag -> step 1, else step 2. Step 1: the flag -> its wave of
        // 5 records (untracked), step 2.
        if (st == 0) {
            encounterSpawnGroupZoneSlot(7);
            if (!storyFlag(kFlagLayoutB)) {
                c.subState = 2;
            } else if (!storyFlag(kFlagV7Wave)) {
                c.subState = 1;
            }
        } else if (st == 1 && storyFlag(kFlagV7Wave)) {
            for (uint32_t k = 0; k < 5; ++k) encounterSpawnRecordUntracked(stateAddress(TableId::SubruinSpawnersV7Wave) + k * kRecordSize);
            c.subState = 2;
        }
        return;
    case 9: return; // 0x52ef00: ret
    default: notPortedAt(kSubruinPort, "0x4227a0 variant", v);
    }
}

// exe 0x425050: the line trigger fired -- kind 1: its group in the zone slot (armed: exe 0x5e6640, else exe
// 0x50f480(slot, 0, g); no zone slot: nothing); kind 2: exe 0x5e66b0(g) in the script slot (if any). Freed.
void lineTriggerFire(ControllerActor& c) {
    const LineTriggerParams params(c);
    EncounterState& e = encounterState();
    const int8_t kind = params.kind;
    if (kind == kFireZoneGroup) {
        if (e.slotOfKind[uint32_t(EncounterKind::Zones)] >= 0) {
            EncounterSlot& zone = e.slots[uint8_t(e.slotOfKind[uint32_t(EncounterKind::Zones)])];
            if (zone.active == kEncounterSlotArmed) {
                encounterSpawnGroupZoneSlot(params.group);
                freeActor(c);
                return;
            }
            encounterSpawnGroup(zone, false, params.group);
        }
    } else if (kind == kFireScriptGroup && e.slotOfKind[uint32_t(EncounterKind::Script)] >= 0) {
        encounterSpawnGroupScriptSlot(params.group);
    }
    freeActor(c);
}

// exe 0x424e50 (the watch): variant 0: the player's x / z at the spawner's height; when its facing minus the angle
// from there to the spawner is within (0x400, 0xc00) (the player crossed to its back side) -> the drop along the
// facing. Variant != 0: the player inside its box (unsigned range tests) -> the drop along the angle of the spawner from
// itself (atan2(0, 0)). The drop: the next state, the drop's point moved 0x3000 back along yaw + 0x800 (exe 0x5e3750),
// then its count of pool-0x20 actors at the point, yaw the drop's
void dropSpawnerWatch(ControllerActor& c) {
    const DropSpawnerParams params(c);
    DropSpawnerFields& f = classState<DropSpawnerFields>(c);
    const Player& p = player();
    int yaw;
    if (c.variant == 0) {
        f.drop.x = int16_t(int32_t(p.pos.x) >> 16);
        f.drop.y = f.point.y;
        f.drop.z = int16_t(int32_t(p.pos.z) >> 16);
        const int ang = angleOf(f.drop, f.point);
        yaw = int(params.facing) << 4;
        const uint32_t d = uint32_t(yaw - ang) & 0xfff;
        if (d >= 0xc00 || d <= 0x400) return;
    } else {
        const uint32_t ex = uint32_t(params.halfX) << 8, ez = uint32_t(params.halfZ) << 8;
        const uint32_t dx = uint32_t(int32_t(p.pos.x.whole) - int32_t(f.point.x)) + ex;
        if (dx >= ex * 2) return;
        const uint32_t dz = uint32_t(int32_t(p.pos.z.whole) - int32_t(f.point.z)) + ez;
        if (dz >= ez * 2) return;
        f.drop = f.point;
        yaw = angleOf(f.drop, f.point);
    }
    ++c.state;
    moveAlongYaw16(f.drop, yaw + 0x800, 0, 0, -0x3000);
    int n = params.count;
    if (n == 0) n = 4;
    for (; n > 0; --n) {
        const uint32_t address = allocActor20();
        if (address == 0) continue;
        AnimatedActor& d = animatedActorAt(address);
        d.flags = kActorLiveDrawn;
        d.type = kDroppedType;
        d.subtype = 0;
        d.variant = kDroppedVariant;
        d.spawnByte = 0;
        d.loopFlags |= kActorSweepGroup2;
        d.pos.x = int32_t(uint32_t(int32_t(f.drop.x)) << 16);
        d.pos.y = int32_t(uint32_t(int32_t(f.drop.y)) << 16);
        d.pos.z = int32_t(uint32_t(int32_t(f.drop.z)) << 16);
        d.rotation.y = int16_t(yaw);
    }
}

} // namespace

// exe 0x4227a0: [0x88bc88 + state * 4]: 0 = 0x422cd0 (init), 1 = 0x4227c0 (the variant [0x88bc90 + variant * 4]); the
// table runs on into the variant table, so a state s >= 2 calls the variant s - 2 (unreachable in practice)
void st4eGroupSpawnerUpdate(const PlayerEnv&, uint32_t actor) {
    ControllerActor& c = controllerActorAt(actor);
    const uint8_t s = c.state;
    if (s == uint8_t(GroupSpawnerState::Init)) spawnerInit(c);
    else if (s == uint8_t(GroupSpawnerState::Run)) st4eVariant(c, c.variant);
    else if (s <= 11) st4eVariant(c, s - 2u);
    else notPortedAt(kSubruinPort, "0x4227a0 state", s);
}

// exe 0x422cb0: [0x88bcb8 + state * 4]: 0 = 0x422cd0 (init), 1 = 0x422cf0 ([0x88bcc0 + variant * 4], the variants 0..8
// = 0x52ef00 ret), the states 2..10 = ret
void st50GroupSpawnerUpdate(const PlayerEnv&, uint32_t actor) {
    ControllerActor& c = controllerActorAt(actor);
    const uint8_t s = c.state;
    if (s == uint8_t(GroupSpawnerState::Init)) spawnerInit(c);
    else if (s == uint8_t(GroupSpawnerState::Run)) {
        if (c.variant > 8) notPortedAt(kSubruinPort, "0x422cb0 variant", c.variant);
    } else if (s > 10) {
        notPortedAt(kSubruinPort, "0x422cb0 state", s);
    }
}

// exe 0x424fb0: state 0 -> its own bytes cleared, state 1. Then by its variant: the player's x >= its x / x <= / z >= /
// z <= -> fired (exe 0x425050: its group, freed)
void subruinLineTriggerUpdate(const PlayerEnv&, uint32_t actor) {
    ControllerActor& c = controllerActorAt(actor);
    LineTriggerFields& f = classState<LineTriggerFields>(c);
    if (c.state == 0) {
        f.ownBytes = {};
        c.state = 1;
    }
    const Player& p = player();
    const int16_t px = p.pos.x.whole, pz = p.pos.z.whole;
    switch (LineAxis(c.variant)) {
    case LineAxis::XAtLeast: if (px >= f.x) lineTriggerFire(c); return;
    case LineAxis::XAtMost: if (px <= f.x) lineTriggerFire(c); return;
    case LineAxis::ZAtLeast: if (pz >= f.z) lineTriggerFire(c); return;
    case LineAxis::ZAtMost: if (pz <= f.z) lineTriggerFire(c); return;
    default: return;
    }
}

// exe 0x424e10: [0x88bddc + state * 4]: 0 = 0x424e30 (the step cleared, the next state), 1 = 0x424e50 (the watch), 2 =
// 0x427cf0 (freed, exe 0x5e15c0)
void subruinDropSpawnerUpdate(const PlayerEnv&, uint32_t actor) {
    ControllerActor& c = controllerActorAt(actor);
    switch (DropSpawnerState(c.state)) {
    case DropSpawnerState::Start:
        c.step = 0;
        ++c.state;
        return;
    case DropSpawnerState::Watch: dropSpawnerWatch(c); return;
    case DropSpawnerState::Free: freeActor(c); return;
    default: notPortedAt(kSubruinPort, "0x424e10 state", c.state);
    }
}

} // namespace od::port
