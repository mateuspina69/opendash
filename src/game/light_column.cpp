// The light column and its glow (see light_column.hpp).
//   glow 0x437fa0, states [0x88d4dc]: 0 0x437fc0 (set up), 1 0x438030 (variant 0: 0x438080 emit; variant | 0x80: 0x438210
//        the mote rises), 2 0x438060 (the column and the glow freed)
//   column 0x598460, states [0x8ce348]: 0 0x598480 (the start effects), 1 0x598510 (the capsule; fading or steady)
// The column's rings (8 levels x 16 segments, exe 0x598680) and the glow's discs (0x438370) are render output; the mote's
// 0xf8 record is written through dc::emit.
#include "game/actor_state.hpp"
#include "game/light_column_state.hpp"
#include "game/field_alias.hpp"
#include "game/light_column.hpp"

#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/draw_commands.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"

namespace od::port {

using namespace light_column_state;

namespace {

constexpr const char* kLightColumnPort = "light column port"; // the area of its port-gap errors

enum class GlowState : uint8_t {
    Setup = 0, // exe 0x437fc0
    Run = 1,   // exe 0x438030
    Free = 2,  // exe 0x438060
};
constexpr uint8_t kGlowType = 0x13;         // the stage effect type (the motes are glows too)
constexpr uint8_t kMoteVariant = 0x80;      // variant bit: a mote
constexpr uint8_t kMoteSlotLoops = 0x7f;    // the variant's other bits (the stack word the mote's record reads)
constexpr int8_t kMoteRise = -0x30;         // 0xd0
constexpr uint8_t kMoteGrey = 0x80;
constexpr int16_t kMoteFadeTicks = 0x10;
constexpr uint8_t kGlowColumnVariant = 0x31; // the column's variant: colour 1, silent (0x10), steady (0x20)
constexpr uint8_t kSweepGroupBits = kActorSweepGroup1 | kActorSweepGroup2;

// ---- the light column (pool 0x60) ---------------------------------------------------------------------------------
// The column's fields (its class block reuses the slot's model fields: it binds no model)
struct LightColumnFields {
    explicit LightColumnFields(const StaticActor& actor) : LightColumnFields(const_cast<StaticActor&>(actor)) {}
    explicit LightColumnFields(StaticActor& actor) : radius(actor.params, 0), heightSteps(actor.params[2]), damage(actor.params[3]), timer(actor.model, 0), colourEntry(actor.extraGeometry), hitBox(actor.hitBox) {}
    BytesField<uint16_t, 4> radius; // the capsule's radius (and the rings')
    uint8_t& heightSteps; // its height in 8-unit steps
    uint8_t& damage; // its hit entries' attack
    PartField<int16_t, uint32_t> timer; // a fading column's ticks left
    uint32_t& colourEntry; // its colour row (exe data 0x8ce330 + 8 per colour; read by the draw)
    HitBoxHead& hitBox;
};
enum class LightColumnState : uint8_t { Start = 0, Shine = 1 }; // exe 0x598480 / 0x598510
constexpr uint8_t kColumnSilent = 0x10;    // variant bits: no start effects ...
constexpr uint8_t kColumnSteady = 0x20;    // ... and no fade (the capsule offset against the player's facing)
constexpr uint8_t kColumnColourMask = 0x0f;
constexpr uint32_t kColumnColourSize = 8;
constexpr int16_t kColumnFadeTicks = 0x10;
constexpr uint16_t kSoundColumn = 0x8b;
// the hit entries by the column's sweep group (loop flags & 3): 0 the bodies' group 6, 1 the player's attacks, else the
// heavy thrown group; their attack flags
constexpr uint32_t kColumnShape = kHitNoLockOn | hitCapsule(2);

void moteSpawn(const EffectActor& glow, uint32_t xy, uint32_t zSpare, int16_t size, uint8_t height) { // exe 0x4381b0
    const uint32_t address = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (!address) return;
    EffectActor& e = effectActorAt(address);
    GlowFields& m = classState<GlowFields>(e);
    e.flags = kActorLive;
    e.type = kGlowType;
    e.variant = uint8_t(glow.variant | kMoteVariant);
    const uint8_t loops = glow.loopFlags;
    m.period = 0;
    e.loopFlags |= uint8_t((loops & kSweepGroupBits) | kActorLateLoop);
    m.heightSteps = int8_t(height);
    m.size = int16_t(size + kMoteSizeExtra);
    e.setPositionWords(xy, zSpare);
}

void glowEmit(EffectActor& glow, GlowFields& f) { // exe 0x438080
    if (glow.subState == 0) {
        const uint32_t address = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (address) {
            StaticActor& c = staticActorAt(address);
            c.flags = kActorLive;
            c.type = kLightColumnStageType;
            c.variant = kGlowColumnVariant;
            c.loopFlags |= uint8_t((glow.loopFlags & kSweepGroupBits) | kActorLateLoop);
            f.column = &c;
            ++glow.subState;
        }
        int32_t n = 0;
        if (f.period != 0) {
            int32_t rise = f.rise;
            if (rise < 0) rise = -rise;
            n = ((int32_t(f.heightSteps) << 3) / rise) / int32_t(f.period);
        }
        for (int32_t i = 0; i < n; ++i) { // the column prefilled with motes
            const uint16_t dy = uint16_t(int32_t(f.period) * int32_t(f.rise) * i);
            const uint32_t xy = (glow.positionWord(0) & 0xffffu) | uint32_t(uint16_t(uint16_t(glow.pos.y) + dy)) << 16;
            moteSpawn(glow, xy, glow.positionWord(1), f.size, uint8_t(f.heightSteps));
        }
    }
    if (f.column) {
        StaticActor& c = *f.column;
        LightColumnFields col(c);
        col.radius = uint16_t(f.size);
        col.heightSteps = uint8_t(f.heightSteps);
        col.damage = 1;
        c.pos.x = wholeToFixed16(glow.pos.x);
        c.pos.y = wholeToFixed16(glow.pos.y);
        c.pos.z = wholeToFixed16(glow.pos.z);
    }
    if (f.timer != 0) {
        f.timer = int16_t(uint16_t(f.timer) - 1);
        return;
    }
    moteSpawn(glow, glow.positionWord(0), glow.positionWord(1), f.size, uint8_t(f.heightSteps));
    f.timer = int16_t(f.period);
}

void moteRise(EffectActor& mote, GlowFields& f) { // exe 0x438210 (its logic and its record)
    const int16_t t = f.timer;
    if (t < kMoteFadeTicks) f.brightness = uint8_t(uint32_t(t) << 3);
    f.timer = int16_t(t - 1);
    if (t < 0) {
        freeActor(mote);
        return;
    }
    mote.pos.y = int16_t(uint16_t(mote.pos.y) + int16_t(f.rise));
    // the mote's 0xf8 record (8 words, grey = the brightness). Word 6's high half is a u16 the exe reads from its stack
    // at [esp + (variant & 0x7f) * 2 + 2]: index 0 = the local 0x7dd1, 1 / 2 = the return address into 0x438030
    // (0x438045), 3 / 4 = the actor argument, 5 / 6 = the block argument (further: the callers' frames, not reproduced
    // -> 0x7dd1)
    const uint32_t g = f.brightness;
    const uint32_t k = mote.variant & kMoteSlotLoops;
    const uint32_t block = slotIdentityWord(mote) + 0x18;
    uint32_t hi = 0x7dd1;
    if (k == 1) hi = 0x8045;
    else if (k == 2) hi = 0x0043;
    else if (k == 3) hi = slotIdentityWord(mote) & 0xffffu;
    else if (k == 4) hi = slotIdentityWord(mote) >> 16;
    else if (k == 5) hi = block & 0xffffu;
    else if (k == 6) hi = block >> 16;
    dc::emit({(uint32_t(uint8_t(gameState().frame)) & 0x3f) << 16 | 0x1010u, (((g | 0xfffff800u) << 8) | g) << 8 | g,
              uint32_t(uint16_t(mote.pos.x)) | uint32_t(int32_t(mote.pos.y)) << 16, 0x22000000u,
              uint32_t(uint16_t(mote.pos.z)) | 0x1010000u, uint32_t(int32_t(f.size)) << 16, hi << 16 | 0x2848u,
              0x2f3757u},
             mote.at);
}

void glow(EffectActor& e) { // exe 0x437fa0
    GlowFields& f = classState<GlowFields>(e);
    switch (GlowState(e.state)) {
    case GlowState::Setup: // exe 0x437fc0
        f.rise = kMoteRise;
        if (e.variant & kMoteVariant) {
            f.brightness = kMoteGrey;
            f.timer = int16_t(uint16_t((int32_t(f.heightSteps) << 3) / 0x30));
        } else {
            f.timer = int16_t(f.period);
            f.column = nullptr;
        }
        e.state = uint8_t(GlowState::Run);
        [[fallthrough]];
    case GlowState::Run: // exe 0x438030
        if (e.variant & kMoteVariant) moteRise(e, f);
        else glowEmit(e, f);
        return;
    case GlowState::Free: // exe 0x438060
        if (f.column) freeActor(*f.column);
        freeActor(e);
        return;
    default: notPortedAt(kLightColumnPort, "0x437fa0 state", e.state);
    }
}

void lightColumn(StaticActor& actor) { // exe 0x598460
    LightColumnFields f(actor);
    switch (LightColumnState(actor.state)) {
    case LightColumnState::Start: // exe 0x598480
        if (!(actor.variant & kColumnSilent)) { // the start effects: a shake, the vibration, the sound at the column
            cameraShakeAt(0x2000, 0x300, 0x100, 0x1c00, actor.pos); // exe 0x40eab0
            padVibrateAt(0x501, 0x412c0, 0x100, 0x1c00, actor.pos); // exe 0x55cd80
            soundPlayAtPos(kSoundColumn, actor.pos);                // exe 0x5d5380
        }
        f.colourEntry = stateAddress(TableId::LightColumnColours) + (actor.variant & kColumnColourMask) * kColumnColourSize;
        f.timer = kColumnFadeTicks;
        // a steady column (variant & 0x20): its rings, exe 0x598680 (render output)
        ++actor.state;
        return;
    case LightColumnState::Shine: { // exe 0x598510
        uint32_t r;
        uint16_t x, z;
        if (!(actor.variant & kColumnSteady)) {
            const int32_t t = f.timer;
            if (t == 0) {
                freeActor(actor);
                return;
            }
            r = uint32_t(int32_t(uint32_t(f.radius) * uint32_t(kColumnFadeTicks - t)) >> 4);
            // exe 0x598680(actor, block, r): render output
            f.timer = int16_t(uint16_t(f.timer) - 1);
            x = uint16_t(actor.pos.x.whole);
            z = uint16_t(actor.pos.z.whole);
            f.hitBox.previousX = int16_t(x);
            f.hitBox.previousZ = int16_t(z);
        } else {
            r = f.radius;
            // exe 0x598680(actor, block, r): render output
            const Player& p = player();
            const uint32_t i = (uint32_t(uint16_t(p.rotation.y)) - uint32_t(uint16_t(p.directionOffset.y))) & kAngleMask;
            x = uint16_t(actor.pos.x.whole);
            z = uint16_t(actor.pos.z.whole);
            f.hitBox.previousX = int16_t(uint16_t(x - uint16_t(fixedSin(int(i)) >> 8)));
            f.hitBox.previousZ = int16_t(uint16_t(z - uint16_t(fixedCos(int(i)) >> 8)));
        }
        const uint16_t y = uint16_t(actor.pos.y.whole);
        const int16_t top[3] = {int16_t(x), int16_t(uint16_t(y - uint16_t(uint32_t(f.heightSteps) << 3))), int16_t(z)};
        const int16_t base[3] = {int16_t(x), int16_t(y), int16_t(z)};
        f.hitBox.result = 0;
        uint32_t group, attackKind;
        const uint8_t sweep = actor.loopFlags & kSweepGroupBits;
        if (sweep == 0) {
            group = hitGroup(6);
            attackKind = kHitAttackSolid | kHitAttackByPlayer;
        } else if (sweep & kActorSweepGroup1) {
            group = hitGroup(kHitGroupPlayerAttacks);
            attackKind = kHitAttackByPlayer;
        } else {
            group = hitGroup(kHitGroupHeavyThrown);
            attackKind = kHitAttackSolid;
        }
        const uint16_t rad = uint16_t(int32_t(r) >> 2);
        hitListSubmitCapsule(base, rad, top, rad, f.hitBox, hitOwner(actor.slot) | group | kColumnShape,
                             uint32_t(f.damage) | attackKind); // exe 0x5e4190
        return;
    }
    default: notPortedAt(kLightColumnPort, "0x598460 state", actor.state);
    }
}

} // namespace

void lightGlowUpdate(const PlayerEnv&, uint32_t actor) { glow(effectActorAt(actor)); }

void lightColumnUpdate(const PlayerEnv&, uint32_t actor) { lightColumn(staticActorAt(actor)); }

} // namespace od::port
