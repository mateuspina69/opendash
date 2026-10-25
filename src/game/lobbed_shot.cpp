// The lobbed shot class 0x5c4a90 (see lobbed_shot.hpp). States [0x8d11d8]: 0 0x5c4ad0 (set up, the launch's sound), 1
// 0x5c4b20 (the flight).
#include "game/actor_state.hpp"
#include "game/lobbed_shot_state.hpp"
#include "game/lobbed_shot.hpp"

#include "engine/data_source.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/draw_commands.hpp"
#include "game/explosions.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/port_gap.hpp"

namespace od::port {

using namespace lobbed_shot_state;

namespace {

constexpr const char* kLobbedShotPort = "lobbed shot port"; // the area of its port-gap errors

enum class LobbedShotState : uint8_t { Setup = 0, Fly = 1 };

const DataTable<CollisionBoxRecord> lobbedShotBox{TableId::LobbedShotBox};
// the sprite by variant & 0xf (exe data 0x8d11b4 / 0x8d11c0 / 0x8d11cc, 4 bytes each): the u / v offsets (s16 pairs),
// the CLUT words, the tpage words
const DataTable<ScalarTable<int16_t, 2 * 16>> lobbedSpriteUv{TableId::LobbedShotSpriteUv};
const DataTable<ScalarTable<uint32_t, 16>> lobbedSpriteCluts{TableId::LobbedShotSpriteCluts},
    lobbedSpriteTpages{TableId::LobbedShotSpriteTpages};
constexpr int16_t kSpriteStep = 0x20, kSpriteLastU = 0xbf;
constexpr uint32_t kShotShape = hitGroup(kHitGroupHeavyThrown) | hitCapsule(1);
constexpr uint8_t kSparkLoops = 2; // the spark's loop flags

void lobbedShot(const PlayerEnv& env, ProjectileActor& a) { // exe 0x5c4a90
    LobbedShotFields& f = classState<LobbedShotFields>(a);
    a.savePreviousPose();
    a.wasAirborne = a.airborne;
    switch (LobbedShotState(a.state)) {
    case LobbedShotState::Setup: // exe 0x5c4ad0
        a.collisionBox.pointAt(lobbedShotBox.stateAddress());
        f.clearBlockBytes();
        a.enterState(uint8_t(LobbedShotState::Fly));
        if (f.launchSound >= 0) soundPlayAtPos(uint16_t(f.launchSound), a.pos);
        return;
    case LobbedShotState::Fly: { // exe 0x5c4b20: a wall / the floor or a hit -> spark (or a blast), freed
        integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
        const int16_t cur[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
        a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
        const int16_t atk = f.damage;
        const uint32_t r = actorTestMove(env, a);
        if (int32_t(r) < 0 || (r & 0x20000000) || (f.hitBox.result & kHitAttackBump)) {
            queueBreakableWallHit(r, atk);
            if (!(a.variant & kLobbedShotBlasts)) {
                spawnHitSpark(kSparkLoops, cur[0], cur[1], cur[2]);
                freeActor(a);
                return;
            }
            if (const uint32_t address = allocActor60(0)) { // exe 0x5e1300: the blast (game/explosions)
                StaticActor& x = staticActorAt(address);
                BlastParams blast(x);
                x.flags = kActorLive;
                x.type = typeByte(GlobalStaticType::Blast);
                x.loopFlags |= kActorSweepGroup2;
                x.variant = 0;
                x.pos = a.pos;
                blast.size = f.blastSize;
                blast.attack = uint16_t(atk);
            }
            freeActor(a);
            return;
        }
        const int16_t prev[3] = {a.prevPos.x.whole, a.prevPos.y.whole, a.prevPos.z.whole};
        f.hitBox.result = 0;
        f.hitBox.previousX = prev[0];
        f.hitBox.previousZ = prev[2];
        const int sz = f.size;
        const uint16_t rad = uint16_t((sz - (sz >> 2)) >> 2);
        hitListSubmitCapsule(cur, rad, prev, rad, f.hitBox, hitOwner(a.slot) | kShotShape,
                             uint32_t(int32_t(atk)) | kHitAttackStrong); // exe 0x5e4190
        { // the sprite: a 0x2c draw command (word 0 unwritten), variant k = & 0xf: u = the frame's u + the variant's,
          // v the variant's (32 x 32 texels), its CLUT and tpage words, colour 0x808080, the size (exe 0x5c4c6a..:
          // 32-bit ORs as the exe, unmasked)
            const uint32_t k = a.variant & 0xfu;
            const uint32_t v = uint32_t(int32_t(lobbedSpriteUv[2 * k + 1]));
            const uint32_t u = uint32_t(int32_t(f.spriteU) + int32_t(lobbedSpriteUv[2 * k]));
            const uint32_t v1 = (v + 0x1f) * 0x100u;
            const uint32_t clut = lobbedSpriteCluts[k];
            const uint32_t tpage = lobbedSpriteTpages[k];
            dc::emit({0, 0x2c808080u, uint32_t(uint16_t(a.pos.x.whole)) | uint32_t(uint16_t(a.pos.y.whole)) << 16,
                      (clut << 8 | v) << 8 | u, uint32_t(uint16_t(a.pos.z.whole)), (tpage << 8 | v) << 8 | (u + 0x1f),
                      uint32_t(int32_t(f.size)), v1 | u, 0, v1 | (u + 0x1f)},
                     a.at, false);
        }
        f.spriteU = int16_t(uint16_t(f.spriteU) + uint16_t(kSpriteStep));
        if (f.spriteU > kSpriteLastU) f.spriteU = 0;
        return;
    }
    default: notPortedAt(kLobbedShotPort, "0x5c4a90 state", a.state);
    }
}

} // namespace

void lobbedShotUpdate(const PlayerEnv& env, uint32_t actor) { lobbedShot(env, projectileActorAt(actor)); }

} // namespace od::port
