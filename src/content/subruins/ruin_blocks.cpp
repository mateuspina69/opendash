// The ruin blocks (static type 0x1f sub 0, class 0x5a41a0, see ruin_blocks.hpp), transcribed from the dash2.exe
// disassembly. States [0x8cecdc]: 0 0x5a4230 init, 1 0x5a4330 run (a block's sub-states [0x5a46ac]: 0 appear, 1
// 0x5a4375 standing, 2 0x5a4570 hidden, 3 0x5a460c fading in; the pieces: 0x5a464e), 2 0x5a4eb0 the pieces' bounce, 3
// 0x5afdd0 freed.
#include "content/subruins/ruin_blocks.hpp"
#include "content/subruins/ruin_blocks_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/encounter.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace ruin_blocks_state;

namespace {

// Its spawn parameters: a piece's debris mode (0: a block) and model index; this tick's hittable flag
struct BlockParams {
    explicit BlockParams(const StaticActor& actor) : BlockParams(const_cast<StaticActor&>(actor)) {}
    explicit BlockParams(StaticActor& actor) : debrisMode(actor.params[0]), debrisModel(actor.params[1]), hittable(actor.params[3]) {}
    uint8_t& debrisMode;
    FieldAlias<int8_t, uint8_t> debrisModel;
    uint8_t& hittable;
};
// a piece's offset from the block (s16 x / z, along its yaw)
struct PieceOffset : DataRecord {
    int16_t x = 0;
    int16_t z = 0;
    static PieceOffset load(const TableReader& r) {
        PieceOffset rec;
        rec.x = r.s16(0);
        rec.z = r.s16(2);
        return rec;
    }
};

enum class BlockVariant : uint8_t { Crumbling = 0, Breakable = 1, Wall = 2, Switched = 3 };
enum class BlockState : uint8_t { Init = 0, Run = 1, Bounce = 2, Free = 3 };
enum class BlockSub : uint8_t { Appear = 0, Standing = 1, Hidden = 2, FadeIn = 3 };
constexpr uint8_t kDebrisFalls = 1, kDebrisFallsThenBounces = 2, kDebrisBounces = 3;

const DataTable<ScalarTable<uint8_t, 4>> blockModels{TableId::RuinBlocksBlockModels}; // by variant (models 0 / 7 / 6 / 0)
const DataTable<ScalarTable<uint8_t, 8>> pieceModels{TableId::RuinBlocksPieceModels}; // by the debris model index
const DataTable<ScalarTable<uint16_t, 4>> regrowTicks{TableId::RuinBlocksRegrowTicks}; // by variant (0x5a / 0x96 / 0x96 / 0x5a)
const DataTable<RecordTable<PieceOffset, 3, 4>> crumbleOffsets{TableId::RuinBlocksCrumbleOffsets};
constexpr uint8_t kCrackedModelIndex = 1;          // pieceModels[1]: the crumbling platform cracked
constexpr uint8_t kSmallPieceModel = 5;
constexpr uint8_t kPlainModelStage = 0x58;         // ST58 binds model variant 0 (exe 0x5e0ba0)
constexpr uint16_t kSoundCrumble = 0x250, kSoundCrack = 0x24f, kSoundWallBreak = 0x254, kSoundShatter = 0x24d;
constexpr uint16_t kSoundFadeOut = 0x261, kSoundBackPlain = 0x260, kSoundBack = 0x24e;

// the block's grey level (0..0x1f in each RGB555 channel of the tint)
void fade(StaticActor& a, int32_t v) { a.tint = uint16_t((((v << 5) | v) << 5) | v); }
// a 16.16 point: the block's position moved along its yaw by (x, y, z) (exe 0x5e36f0 on a copy of it)
void pointAlongYaw(const StaticActor& a, int x, int y, int z, int32_t out[3]) {
    int32_t d[3];
    rotateByYaw(a.rotation.y, x, y, z, d);
    out[0] = int32_t(uint32_t(a.pos.x) + uint32_t(d[0]));
    out[1] = int32_t(uint32_t(a.pos.y) + uint32_t(d[1]));
    out[2] = int32_t(uint32_t(a.pos.z) + uint32_t(d[2]));
}
// a piece's spin: the block's rotation, each lane turned at random within the mask's range around it
void spinFrom(StaticActor& piece, const StaticActor& a, uint32_t mask, uint32_t half) {
    piece.rotation = a.rotation;
    piece.rotation.x = int16_t(uint16_t(uint16_t(piece.rotation.x) + uint16_t((crtRand() & mask) - half)));
    piece.rotation.y = int16_t(uint16_t(uint16_t(piece.rotation.y) + uint16_t((crtRand() & mask) - half)));
    piece.rotation.z = int16_t(uint16_t(uint16_t(piece.rotation.z) + uint16_t((crtRand() & mask) - half)));
}
// a piece of the block (its class, a debris mode and model)
StaticActor* spawnPiece(const StaticActor& a, uint8_t mode, uint8_t model) {
    const uint32_t address = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!address) return nullptr;
    StaticActor& e = staticActorAt(address);
    BlockParams p(e);
    e.flags = kActorLive;
    e.type = a.type;
    e.subtype = a.subtype;
    e.variant = a.variant;
    p.debrisMode = mode;
    p.debrisModel = int8_t(model);
    e.loopFlags |= kActorSweepGroup2;
    return &e;
}

// exe 0x5a46c0: three capsules (radius 0x12) over the block. The wall: along x (yaw & 0x400 clear) or z, from y - 0x51
// to y - 0x1af; the others: along x, z +-0xaf, at y + 0x20
void hitCapsules(StaticActor& a, BlockFields& f) {
    int16_t pa[3] = {}, pb[3] = {}; // the exe's locals (the first iteration writes every word read)
    const bool wall = a.variant == uint8_t(BlockVariant::Wall), alongX = (uint16_t(a.rotation.y) & 0x400) == 0;
    if (!wall) {
        pa[1] = pb[1] = int16_t(uint16_t(a.pos.y.whole) + 0x20u);
    } else if (alongX) {
        pa[2] = pb[2] = a.pos.z.whole;
    } else {
        pa[0] = pb[0] = a.pos.x.whole;
    }
    for (int32_t d = -0xaf; d <= 0xaf; d += 0xaf) {
        if (!wall) {
            pa[0] = pb[0] = int16_t(uint16_t(a.pos.x.whole) + uint16_t(d));
            pa[2] = int16_t(uint16_t(a.pos.z.whole) + 0xafu);
            pb[2] = int16_t(uint16_t(a.pos.z.whole) - 0xafu);
        } else {
            if (alongX) pa[0] = pb[0] = int16_t(uint16_t(a.pos.x.whole) + uint16_t(d));
            else pa[2] = pb[2] = int16_t(uint16_t(a.pos.z.whole) + uint16_t(d));
            pa[1] = int16_t(uint16_t(a.pos.y.whole) - 0x51u);
            pb[1] = int16_t(uint16_t(a.pos.y.whole) - 0x1afu);
        }
        hitListSubmitCapsule(pa, 0x12, pb, 0x12, f.hitBox, uint32_t(a.slot) << 24 | 0x482000u,
                             kHitAttackHarmless | kHitAttackSolid); // exe 0x5e4190
    }
}

// exe 0x5a47e0 (the crumbling platform breaks): the sound, 3 falling pieces (models 2..4) at their offsets along the
// yaw, 0x1e lower, their floor the block's ground, each lane turned by +-0x80 at random
void crumblePieces(const PlayerEnv& env, StaticActor& a) {
    const int32_t g = actorGround(env, a); // exe 0x40f190
    soundPlayAtPos(kSoundCrumble, a.pos);
    uint8_t k = 2;
    for (size_t i = 0; i < crumbleOffsets.size(); ++i, ++k) {
        const uint32_t address = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (!address) continue;
        StaticActor& e = staticActorAt(address);
        BlockParams p(e);
        e.flags = kActorLive;
        e.type = a.type;
        e.subtype = a.subtype;
        e.loopFlags |= kActorSweepGroup2;
        classState<BlockFields>(e).floorY = int16_t(g);
        e.variant = a.variant;
        p.debrisMode = kDebrisFalls;
        p.debrisModel = int8_t(k);
        int32_t q[3];
        pointAlongYaw(a, crumbleOffsets[i].x, 0, crumbleOffsets[i].z, q);
        q[1] = int32_t(uint32_t(q[1]) + (0x1eu << 16));
        e.pos.x = q[0];
        e.pos.y = q[1];
        e.pos.z = q[2];
        spinFrom(e, a, 0xff, 0x80);
    }
}

// exe 0x5a4900 (the wall's hit): the sound, two explosions (spread 0x100, 0x14 flashes) at (0, -0x800, -+0x100) along
// the yaw, then 8 x 4 pieces (mode 2) at x -0xe00..0xe00 step 0x400, y -0x400 - row * 0x800, a random spin of +-0x400
// and velocity (+-0x40, 0x40..-0xb0, +-0x40), their floor the block's height
void wallBreak(StaticActor& a) {
    soundPlayAtPos(kSoundWallBreak, a.pos);
    for (const int32_t z : {-0x100, 0x100}) {
        const uint32_t address = allocActor60(0); // exe 0x5e1300
        if (!address) continue;
        StaticActor& x = staticActorAt(address);
        ExplosionParams params(x);
        x.flags = kActorLive;
        x.loopFlags |= kActorSweepGroup2;
        x.type = typeByte(GlobalStaticType::Explosion);
        x.variant = 0;
        x.pos = a.pos;
        moveAlongYaw(x.pos, a.rotation.y, 0, -0x800, z); // exe 0x5e36f0
        params.spread = 0x100;
        params.flashes = 0x14;
    }
    for (int32_t x = -0xe00; x < 0x1200; x += 0x400)
        for (int32_t row = 0; row < 4; ++row) {
            StaticActor* piece = spawnPiece(a, kDebrisFallsThenBounces, kSmallPieceModel);
            if (!piece) continue;
            StaticActor& e = *piece;
            int32_t q[3];
            pointAlongYaw(a, x, -0x400 - (row << 11), 0, q);
            e.pos.x = q[0];
            e.pos.y = q[1];
            e.pos.z = q[2];
            spinFrom(e, a, 0x7ff, 0x400);
            classState<BlockFields>(e).floorY = a.pos.y.whole;
            e.velocity.x = int16_t(((crtRand() & 7) - 4u) << 4);
            e.velocity.y = int16_t((4u - (crtRand() & 0xf)) << 4);
            e.velocity.z = int16_t(((crtRand() & 7) - 4u) << 4);
        }
}

// exe 0x5a4b20 (the block shattered): the sound, 16 pieces (mode 3) pushed out at a random angle by 0x100..0x10f0, a
// random spin of +-0x400, the block's vertical speed, their floor the block's ground
void shatter(const PlayerEnv& env, StaticActor& a) {
    soundPlayAtPos(kSoundShatter, a.pos);
    for (int n = 0x10; n != 0; --n) {
        StaticActor* piece = spawnPiece(a, kDebrisBounces, kSmallPieceModel);
        if (!piece) continue;
        StaticActor& e = *piece;
        e.pos = a.pos;
        const int r = int(((crtRand() & 0xff) + 0x10) << 4);
        const int ang = int(crtRand() & 0xfff);
        int16_t dx = 0, dz = 0;
        angleVector(ang, r, dx, dz);       // exe 0x5e40c0 (y word 0)
        integrateVelocity(e, dx, 0, dz);   // exe 0x5e3690
        spinFrom(e, a, 0x7ff, 0x400);
        classState<BlockFields>(e).floorY = int16_t(actorGround(env, a)); // exe 0x40f190 (the block's)
        e.velocity.x = int16_t(((crtRand() & 7) - 4u) << 4);
        e.velocity.y = a.velocity.y;
        e.velocity.z = int16_t(((crtRand() & 7) - 4u) << 4);
    }
}

// exe 0x5a4230 (the run state written first: a missing model leaves the actor in it)
void init(StaticActor& a, BlockFields& f) {
    BlockParams params(a);
    a.enterState(uint8_t(BlockState::Run));
    a.clearClassBlock(); // exe 0x40dc40
    if (params.debrisMode == 0) {
        if (gameState().stage == kPlainModelStage) {
            if (!bindActorModel(a, 0)) return; // exe 0x5e0ba0
        } else if (!bindActorModel(a, blockModels[a.variant])) { // exe 0x5e0bc0
            return;
        }
        a.collisionBox.pointAt(TableId::RuinBlocksBlockBox);
    } else {
        if (!bindActorModel(a, pieceModels[size_t(int32_t(params.debrisModel.get()))])) return;
        if (params.debrisMode == kDebrisFalls) {
            a.collisionBox.pointAt(TableId::RuinBlocksFallingPieceBox);
            a.velocity.z = 0;
            a.velocity.y = 0;
            a.velocity.x = 0;
        } else {
            a.collisionBox.pointAt(TableId::RuinBlocksBouncingPieceBox);
        }
    }
    const uint8_t v = a.variant;
    if (v != uint8_t(BlockVariant::Crumbling) && v != uint8_t(BlockVariant::Switched))
        a.semiTransparency = semiTransparencyBits(1, 0); // exe 0x5e1a70
    a.flags |= kActorDrawn;
    a.airborne = 0;
    a.onPlatform = 0;
    a.platform = nullptr;
    a.actorHit = 0;
    if (v != uint8_t(BlockVariant::Switched)) return;
    f.group = enemyGroupFindFor(a, 3); // exe 0x5e6cd0(a, 3)
    a.flags &= uint8_t(~kActorDrawn);
    a.subState = uint8_t(BlockSub::Hidden);
    a.step = 0;
}

// exe 0x5a4435: hidden, the regrowth timer of its variant; the pieces
void breakBlock(const PlayerEnv& env, StaticActor& a, BlockFields& f) {
    const uint8_t v = a.variant;
    a.flags ^= kActorDrawn;
    ++a.subState;
    a.step = 0;
    f.timer = int16_t(regrowTicks[v]);
    actorDetachRiders(a); // exe 0x414e70
    if (v == uint8_t(BlockVariant::Crumbling)) crumblePieces(env, a); // exe 0x5a47e0
    else shatter(env, a);                                             // exe 0x5a4b20
}

void standing(const PlayerEnv& env, StaticActor& a, BlockFields& f) { // exe 0x5a4375
    BlockParams params(a);
    params.hittable = 1;
    const uint32_t hit = f.hitBox.result;
    if ((hit & kHitDamageMask) && (hit & (kHitAttackStrongPush | kHitAttackByPlayer))) {
        params.hittable = 0;
        f.clearClassWordsBytes();
        if (a.variant == uint8_t(BlockVariant::Wall)) {
            a.enterState(uint8_t(BlockState::Bounce));
            wallBreak(a); // exe 0x5a4900
            return;
        }
        breakBlock(env, a, f);
        return;
    }
    switch (BlockVariant(a.variant)) {
    case BlockVariant::Crumbling:
        if (playerRides(a)) {
            f.timer -= 2u;
            const uint32_t t = uint16_t(f.timer) & 0xfffeu;
            if (t == 0x32) { // cracked
                bindActorModel(a, pieceModels[kCrackedModelIndex]); // exe 0x5e0bc0
                soundPlayAtPos(kSoundCrack, a.pos);
            } else if (t == 0) {
                breakBlock(env, a, f);
                return;
            }
        } else {
            const int32_t old = f.timer;
            f.timer = int16_t(old + 1);
            if (uint32_t(old) >= 0x64) f.timer = 0x64;
            else if (old == 0x32) bindActorModel(a, blockModels[0]); // whole again (exe 0x5e0bc0)
        }
        break;
    case BlockVariant::Switched:
        if (a.step == 0) {
            if (enemyGroupMarks(f.group, EnemyGroupMarks::LastTick) != 0) { // exe 0x5e6d60(+0xc4, 2)
                ++a.step;
                f.timer = 0x10;
                a.semiTransparency = semiTransparencyBits(1, 0);
                soundPlayAtPos(kSoundFadeOut, a.pos);
                return;
            }
        } else if (a.step == 1) {
            --f.timer;
            const int32_t t = f.timer;
            if (t == 0) {
                a.flags ^= kActorDrawn;
                ++a.subState;
                a.step = 0;
                actorDetachRiders(a); // exe 0x414e70
                return;
            }
            fade(a, t);
        }
        break;
    default: break;
    }
    actorCollisionSubmitSolid(a, SolidType::StandOn, 0, 0); // exe 0x414050(a, 1, 0, 0)
}

void fadeIn(StaticActor& a, BlockFields& f) { // exe 0x5a460c: the grey level up to 0x10, then appear
    ++f.timer;
    const int32_t t = f.timer;
    if (t == 0x10) {
        a.subState = uint8_t(BlockSub::Appear);
        a.restartStep();
        const uint8_t v = a.variant;
        if (v == uint8_t(BlockVariant::Crumbling) || v == uint8_t(BlockVariant::Switched))
            a.semiTransparency = semiTransparencyBits(0, 0);
    }
    fade(a, t);
}

void hidden(StaticActor& a, BlockFields& f) { // exe 0x5a4570
    const uint8_t v = a.variant;
    if (v == uint8_t(BlockVariant::Switched)) {
        if (enemyGroupMarks(f.group, EnemyGroupMarks::LastTick) == 0) return; // exe 0x5e6d60(+0xc4, 2)
    } else {
        --f.timer;
        if (f.timer != 0) return;
        if (gameState().stage == kPlainModelStage) {
            bindActorModel(a, 0); // exe 0x5e0ba0
            soundPlayAtPos(kSoundBackPlain, a.pos);
        } else {
            bindActorModel(a, blockModels[v]); // exe 0x5e0bc0
            soundPlayAtPos(kSoundBack, a.pos);
        }
    }
    a.flags |= kActorDrawn;
    ++a.subState;
    f.timer = 2;
    a.tint = 0;
    a.semiTransparency = semiTransparencyBits(1, 0);
    fadeIn(a, f); // falls through into exe 0x5a460c
}

void run(const PlayerEnv& env, StaticActor& a, BlockFields& f) { // exe 0x5a4330
    BlockParams params(a);
    const uint8_t mode = params.debrisMode;
    if (mode != 0) { // exe 0x5a464e: a piece
        if (mode == kDebrisBounces) {
            ++a.state;
            integrateVelocity(a, a.velocity.x, a.velocity.y, a.velocity.z);
            return;
        }
        a.velocity.y += 0x30u; // falls until 0x20 above its floor
        const int16_t vy = a.velocity.y;
        if (int32_t(f.floorY) <= int32_t(a.pos.y.whole) + 0x20) ++a.state;
        integrateVelocity(a, a.velocity.x, vy, a.velocity.z);
        return;
    }
    switch (BlockSub(a.subState)) { // [0x5a46ac]
    case BlockSub::Appear: // exe 0x5a435d
        ++a.subState;
        a.flags |= kActorDrawn;
        f.timer = 0x64;
        params.hittable = 1;
        return;
    case BlockSub::Standing: standing(env, a, f); return;
    case BlockSub::Hidden: hidden(a, f); return;
    case BlockSub::FadeIn: fadeIn(a, f); return;
    default: return;
    }
}

// exe 0x5a4eb0 (shared with 0x5a4c60): the pieces of modes 2 / 3 bounce (half the speed back up) and blink 0xc ticks;
// the others are freed
void bounce(StaticActor& a, BlockFields& f) {
    const int8_t mode = int8_t(BlockParams(a).debrisMode);
    if (mode < kDebrisFallsThenBounces || mode > kDebrisBounces) {
        a.enterState(uint8_t(BlockState::Free));
        return;
    }
    if (a.subState == 0) {
        f.timer = 0xc;
        a.subState = 1;
        a.airborne = 1;
        a.velocity.y = int16_t(uint32_t(-int32_t(a.velocity.y)) >> 1);
    }
    a.velocity.y += 0x30u;
    a.flags ^= kActorDrawn;
    --f.timer;
    const int16_t vy = a.velocity.y;
    if (uint16_t(f.timer) == 0) a.enterState(uint8_t(BlockState::Free));
    integrateVelocity(a, a.velocity.x, vy, a.velocity.z);
}

void ruinBlocks(const PlayerEnv& env, StaticActor& a) { // exe 0x5a41a0
    BlockFields& f = classState<BlockFields>(a);
    BlockParams params(a);
    params.hittable = 0;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (BlockState(a.state)) { // [0x8cecdc]
    case BlockState::Init: init(a, f); break;
    case BlockState::Run: run(env, a, f); break;
    case BlockState::Bounce: bounce(a, f); break;
    case BlockState::Free: freeActor(a); break; // exe 0x5afdd0 -> 0x5e1540
    default: notPortedAt(kStoryPort, "0x5a41a0 state (table 0x8cecdc)", a.state);
    }
    const uint8_t v = a.variant;
    if ((v == uint8_t(BlockVariant::Breakable) || v == uint8_t(BlockVariant::Wall)) && params.hittable != 0) {
        hitCapsules(a, f); // exe 0x5a46c0
        f.hitBox.result = 0;
        f.hitBox.previousX = a.prevPos.x.whole;
        f.hitBox.previousZ = a.prevPos.z.whole;
    }
}

} // namespace

void ruinBlocksUpdate(const PlayerEnv& env, uint32_t actor) { ruinBlocks(env, staticActorAt(actor)); }

} // namespace od::port
