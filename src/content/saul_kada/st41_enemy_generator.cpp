// The enemy generator of the pirate base's gate rooms (ST41 / ST42 pool 0x20 type 0x10, exe 0x46d210; hypothesis
// names; the same exe family as the ST28 flyer, content/saul_kada/st28_flyer).
// States [0x890278]: 0 bind 0x46d2c0, 1 run 0x46d3c0 (its sub-states [0x890284]: 0 idle 0x46d520, 1 dying 0x46d6b0,
// 2 flinch 0x46d790), 2 freed 0x468fa0. The run: a hit of the player's flashes it, takes its hit points (dead: an
// explosion, its drops, the defeat count) or counts down to a flinch; idle it turns to the player and, the player
// within 0x5dc and fewer than 3 field reaverbots alive, opens and releases one; then its hit entry.
// The shadow (exe 0x5f20f0(actor, 0x60, 0xc0, 0) when drawn) is render output.
#include "content/saul_kada/st41_enemy_generator.hpp"
#include "content/saul_kada/st41_enemy_generator_state.hpp"

#include "content/saul_kada/field_reaverbot.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/vectors.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st41_enemy_generator_state;

namespace {

enum class GeneratorState : uint8_t { Bind = 0, Run = 1, Free = 2 };
enum class GeneratorSub : uint8_t { Idle = 0, Dying = 1, Flinch = 2 };
enum class IdleStep : uint8_t { Wait = 0, Release = 1, Close = 2 };
enum class GeneratorAnim : uint8_t { Open = 0, Close = 1, Flinch = 2, Destroyed = 3, Idle = 4 };
constexpr uint16_t kGeneratorHarmless = 0x001; // destroyed: its entry a harmless bump
constexpr uint16_t kGeneratorOpen = 0x002;     // opened / flinching
constexpr uint16_t kGeneratorClosing = 0x020;  // the closing animation runs
constexpr uint16_t kGeneratorDying = 0x040;
constexpr uint16_t kGeneratorNoHit = 0x200;
constexpr uint16_t kHurtSound = 0x8d, kFlinchSound = 0x8e;
constexpr uint16_t kExplosionTint = 0x421f;
constexpr int kMaxAlive = 3, kReach = 0x5dc;
constexpr uint32_t kAnimatedSlots = 16; // the slots it counts the field reaverbots in
constexpr uint8_t kReaverbotVariant = 4, kReaverbotMode = 3;
const DataTable<ScalarTable<uint8_t, 4>> reaverbotSpawnBytes{TableId::St41EnemyGeneratorReaverbotSpawnBytes}; // by its own spawn byte

AnimatedActorStats generatorStats(const AnimatedActor& actor) { return animatedStats(actor.type, actor.spawnByte); }
void playAnim(AnimatedActor& actor, GeneratorFields& generator, GeneratorAnim anim) {
    actor.anim = uint8_t(anim);
    generator.animEntry = 0;
}

// exe 0x46d520's release step: once open, one field reaverbot (variant 4) at the generator
void release(AnimatedActor& actor, GeneratorFields& generator) {
    if (!actor.animEnded()) return;
    generator.timer = 0x1e;
    ++actor.step;
    const uint32_t slot = allocActor20(); // exe 0x5e1160
    if (!slot) return;
    AnimatedActor& reaverbot = animatedActorAt(slot);
    FieldReaverbotParams params(animatedActorAt(slot));
    reaverbot.flags = kActorLive;
    reaverbot.type = kFieldReaverbotType;
    reaverbot.subtype = 0;
    reaverbot.variant = kReaverbotVariant;
    reaverbot.spawnByte = reaverbotSpawnBytes[actor.spawnByte];
    reaverbot.loopFlags |= kActorSweepGroup2;
    params.mode = kReaverbotMode;
    params.second = 0;
    params.flag = kFieldReaverbotNoFlag;
    reaverbot.pos = actor.pos;
}
// exe 0x46d520: idle (turned to the player), opening to release, closing
void idle(AnimatedActor& actor, GeneratorFields& generator) {
    switch (IdleStep(actor.step)) {
    case IdleStep::Wait:
        if (generator.flags & kGeneratorClosing) {
            if (actor.animEnded()) { // the closing animation ended
                generator.flags &= uint16_t(0xffc0);
                generator.timer = 0x1e;
                playAnim(actor, generator, GeneratorAnim::Idle);
            }
        } else if (generator.timer == 0) {
            int alive = 0; // the field reaverbots alive
            for (uint32_t i = 0; i < kAnimatedSlots; ++i) {
                const AnimatedActor& other = animatedPool()[i];
                if (other.flags != 0 && other.type == kFieldReaverbotType) ++alive;
            }
            if (planarDistance(player().pos, actor.pos) < kReach && alive < kMaxAlive) { // exe 0x5e3b00
                actor.anim = uint8_t(GeneratorAnim::Open);
                ++actor.step;
                generator.flagsLow |= uint8_t(kGeneratorOpen);
                generator.animEntry = 0;
            }
        } else {
            generator.timer -= 1u;
        }
        turnYawTo(actor.rotation.y, uint32_t(angleOf(player().pos, actor.pos)), 0x40); // exe 0x5e3bc0, 0x5e3df0
        return;
    case IdleStep::Release: release(actor, generator); return;
    case IdleStep::Close:
        generator.timer -= 1u;
        if (generator.timer != 0) return;
        actor.anim = uint8_t(GeneratorAnim::Close);
        generator.flagsLow |= uint8_t(kGeneratorClosing);
        generator.animEntry = 0;
        actor.step = uint8_t(IdleStep::Wait);
        return;
    default: return;
    }
}
// exe 0x46d6b0: an explosion at its bone 0, its drops, a defeat counted; blinking 0x1e ticks, then freed
void dying(AnimatedActor& actor, GeneratorFields& generator) {
    switch (actor.step) {
    case 0: {
        const ShortVec4* bone = bonePointOrNull(actor, 0); // exe 0x5e1810
        if (const uint32_t slot = allocActor60(0)) {        // exe 0x5e1300
            if (!bone) notPorted(kStoryPort, "0x46d6b0: exe 0x5e1810 returned null (the exe reads address 0)");
            StaticActor& explosion = staticActorAt(slot);
            const ShortVec4& point = *bone;
            explosion.flags = kActorLive;
            explosion.loopFlags |= kActorSweepGroup2;
            explosion.type = typeByte(GlobalStaticType::Explosion);
            explosion.variant = 0;
            explosion.pos.x.whole = point.x;
            explosion.pos.y.whole = point.y;
            explosion.pos.z.whole = point.z;
            ExplosionParams params(actorHeaderAt(slot));
            params.spread = 0x60;
            params.flashes = 0x1e;
        }
        ++actor.step;
        generator.timer = 0x1e;
        if (actor.spawnByte != 0) spawnDrops(actor.pos, generatorStats(actor).dropTable); // exe 0x5f1270
        countDefeat();
        return;
    }
    case 1:
        if (generator.timerLow & 1) actor.tint = kExplosionTint;
        if (generator.timer == 0) {
            actor.state = uint8_t(GeneratorState::Free);
            return;
        }
        generator.timer -= 1u;
        return;
    default: return;
    }
}
// exe 0x46d790: the flinch animation ended -> its flinch count back, idle
void flinch(AnimatedActor& actor, GeneratorFields& generator) {
    if (!actor.animEnded()) return;
    generator.flags ^= kGeneratorOpen;
    generator.flinchHits = animatedStats(actor.type).bodyAttack; // the row's byte 8: its flinch count
    playAnim(actor, generator, GeneratorAnim::Idle);
    actor.subState = uint8_t(GeneratorSub::Idle);
}
// exe 0x46d2c0: its model (by its spawn byte), the hit points of its row x 8, its flinch count
void bind(AnimatedActor& actor, GeneratorFields& generator) {
    if (!bindActorModel(actor, actor.spawnByte)) { // exe 0x5e0bc0
        freeActor(actor);                          // exe 0x5e1490
        return;
    }
    actor.flags |= kActorDrawn;
    actor.anim = uint8_t(GeneratorAnim::Idle);
    actor.prevAnim = 0xff;
    generator.animEntry = 0;
    const bool hasRow = hasAnimatedStats(actor.type);
    const uint16_t hp = hasRow ? uint16_t(uint32_t(generatorStats(actor).hitPoints) << 3) : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    if (!hasRow) notPorted(kStoryPort, "0x46d2c0: null attack row (the exe reads address 8)");
    generator.flinchHits = generatorStats(actor).bodyAttack; // the row's byte 8: its flinch count
    actor.collisionBox = nullptr;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.velocity.setXY(0, 0);
    actor.velocity.setZSpare(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    generator.hitBox.result = 0;
    generator.swayAngle = 0;
    generator.bobAngle = 0;
    generator.bobCarry = 0;
    generator.spin = 0x20;
    generator.flags = 0;
    generator.timer = 0;
    actor.state = uint8_t(GeneratorState::Run);
    actor.subState = uint8_t(GeneratorSub::Idle);
}
// exe 0x46d3c0
void run(AnimatedActor& actor, GeneratorFields& generator) {
    if (generator.hitBox.result & kHitAttackByPlayer) { // hit
        actor.tint = kTintFlash;
        if (hitPointsAdd(actor, -int32_t(generator.hitBox.result & kHitDamageMask))) { // exe 0x5e52f0: dead
            if (!(generator.flagsLow & kGeneratorDying)) {
                actor.subState = uint8_t(GeneratorSub::Dying);
                actor.step = 0;
                generator.flagsLow |= uint8_t(kGeneratorHarmless);
                actor.anim = uint8_t(GeneratorAnim::Destroyed);
                generator.flags |= kGeneratorDying;
                generator.animEntry = 0;
            }
        } else {
            generator.flinchHits -= 1u;
            if (generator.flinchHits == 0 && !(generator.flagsLow & kGeneratorOpen)) { // the flinch
                actor.anim = uint8_t(GeneratorAnim::Flinch);
                generator.flags |= kGeneratorOpen;
                generator.animEntry = 0;
                actor.subState = uint8_t(GeneratorSub::Flinch);
                soundPlayAtPos(kFlinchSound, actor.pos);
            } else {
                soundPlayAtPos(kHurtSound, actor.pos);
            }
        }
    }
    switch (GeneratorSub(actor.subState)) { // [0x890284]
    case GeneratorSub::Idle: idle(actor, generator); break;
    case GeneratorSub::Dying: dying(actor, generator); break;
    case GeneratorSub::Flinch: flinch(actor, generator); break;
    default: notPortedAt(kStoryPort, "0x46d3c0 sub-state (table 0x890284)", actor.subState);
    }
    const uint16_t flags = generator.flags;
    if (flags & kGeneratorNoHit) return;
    const int16_t pos[3] = {actor.pos.x.whole, int16_t(uint16_t(actor.pos.y.whole) - 0x64u), actor.pos.z.whole};
    generator.hitBox.previousX = actor.prevPos.x.whole;
    generator.hitBox.previousZ = actor.prevPos.z.whole;
    generator.hitBox.result = 0;
    const AnimatedActorStats stats = animatedStats(actor.type);
    const uint32_t attack = ((flags & kGeneratorHarmless) ? kHitAttackBump : kHitAttackSolid) | stats.attack;
    const uint32_t shape = hitOwner(actor.slot) | hitHeightLevel(stats.hitHeight) | hitGroup(kHitGroupBodies) |
                           kHitContactMotion | hitRadius(0xb8);
    hitListSubmit(pos, generator.hitBox, shape, attack); // exe 0x5e4120
}
void enemyGenerator(AnimatedActor& actor) { // exe 0x46d210
    GeneratorFields& generator = classState<GeneratorFields>(actor);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (GeneratorState(actor.state)) { // [0x890278]
    case GeneratorState::Bind: bind(actor, generator); break;
    case GeneratorState::Run: run(actor, generator); break;
    case GeneratorState::Free: freeActor(actor); break; // 0x468fa0 -> exe 0x5e1490
    default: notPortedAt(kStoryPort, "0x46d210 state (table 0x890278)", actor.state);
    }
    if (actor.anim == actor.prevAnim) animStepPose(actor);           // exe 0x5e1e80
    else animSetPose(actor, actor.anim, int(generator.animEntry)); // exe 0x5e1c30
}

} // namespace

void enemyGeneratorUpdate(const PlayerEnv&, uint32_t actor) { enemyGenerator(animatedActorAt(actor)); }

} // namespace od::port
