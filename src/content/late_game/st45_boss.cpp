// The ST45 area boss (ST45 sub 1, spawned by the script slot's group 0 of the room handler 0x58a880,
// content/late_game/ch4_story): the class exe 0x4c78e0 (pool 0x20 type 0x45; states [0x894f2c]: bind 0x4c79b0, run
// 0x4c7b30 (the moves [0x894f44] on the sub-state), free 0x468fa0, death 0x4cad80 -> story flag 0x176) and the
// children it spawns through the ST45 tables (pool 0x40 0x907884: type 5 = the breath puff 0x5c4820, 0xf = the mouth
// shot 0x5c62d0, 0x1a = the spit blob 0x5ca060; pool 0x60 0x907790: type 0x3c sub 0 = the shock wave 0x5b0230, type
// 0x32 = the footstep dust 0x5acbc0, game/footstep_dust). Names are hypotheses from the observed behaviour. The exe's
// locals it leaves uninitialised are 0 here (noted where it happens).
//
// Its fights by its spawn byte (+7): 0 the first fight (story flag 0x4c clear: the walk, the stomps, the retreat; flag
// 0x4c set: the second fight), 1 a later fight, 2 the ST45 group (no debris rain, no quake). Each fight's stat rows are
// picked from the type's rows by small exe tables of the spawn byte (the second half of a row for the second fight).
//
// Pool 0x60 type 0x3b (sub 1), the falling debris of the stomps / the quake: the ST45 table 0x907790 has no entry 0x3b
// (0 in the file, never written: 0x907790 is static and 0xa5d0dc is only its type-0 sub-table), so the exe would call
// through a null table there. In ST45 it is never spawned: the group record 0x907ac0 gives the boss +7 = 2, which
// skips the debris rain and the quake. The class is 0x5afde0 (ST33's table 0x8f6bdc type 0x3b sub 1, where the same
// boss runs as type 0x45 sub 0); not ported (not reachable here).
// The shock wave's OT ribbon goes to the OT side channel of game/draw_commands; its hit test (exe 0x5b0d20) runs only
// after a full projection.
#include "content/late_game/st45_boss.hpp"
#include "content/late_game/st45_boss_state.hpp"

#include "content/saul_kada/breath_puff.hpp"

#include "game/loaded_data.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/distances.hpp"
#include "game/draw_commands.hpp"
#include "game/dust_puff.hpp"
#include "game/field_alias.hpp"
#include "game/footstep_dust.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/projection_state.hpp"
#include "game/random.hpp"
#include "game/room_grid.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/vectors.hpp"

namespace od::port {

using namespace st45_boss_state;

namespace {

int16_t t16(int64_t v) { return int16_t(uint16_t(uint64_t(v))); }
int16_t wholeOf(int32_t v) { return int16_t(uint32_t(v) >> 16); } // a 16.16 value's integer part
void sound(uint16_t id, const Actor& actor) { soundPlayAtPos(id, actor.pos); } // exe 0x5d5380
uint16_t frameCounter() { return gameState().frame; }
// exe 0x5e1970: the point (x, y, z) through bone `bone` of the actor
void bonePt(const AnimatedActor& actor, uint32_t bone, int x, int y, int z, int16_t out[3]) {
    const int16_t v[3] = {int16_t(x), int16_t(y), int16_t(z)};
    bonePointOffset(actor, bone, v, out);
}
// a 16.16 point's integer parts
void wholes(const int32_t p[3], int16_t out[3]) {
    for (int k = 0; k < 3; ++k) out[k] = wholeOf(p[k]);
}
void wholes(const Fixed16Vec3& p, int16_t out[3]) {
    out[0] = p.x.whole;
    out[1] = p.y.whole;
    out[2] = p.z.whole;
}

// ---- the boss (exe 0x4c78e0) ---------------------------------------------------------------------------------------

enum class BossState : uint8_t { Bind = 0, Run = 1, Free = 2, Death = 3 };
enum class BossMove : uint8_t {
    Walk = 0, Stomp = 1, Retreat = 2, Roar = 3, Quake = 4, SecondWalk = 5, SecondStomp = 6, Bite = 7, Breath = 8
};
// the walk's steps (exe 0x4c8410)
enum class WalkStep : uint8_t {
    Approach = 0, Pause = 1, Decide = 2, Step = 4, Recoil = 5, Stalk = 6, Return = 7, PauseAgain = 8, LongStep = 9,
    Turn = 0xa, BackToArena = 0xb, Fall = 0xc, TurnStep = 0xd
};
// the second fight's walk steps (exe 0x4c9bc0)
enum class SecondWalkStep : uint8_t {
    Approach = 0, Pause = 1, Decide = 2, Step = 4, Stalk = 6, StepBack = 7, PauseAgain = 8, Turn = 9, Charge = 0xa,
    Lunge = 0xb, TurnStep = 0xd
};
enum class StompStep : uint8_t { Land = 0, Rain = 1, Recover = 2, StepBack = 3 };
enum class RetreatStep : uint8_t {
    Start = 0, TurnAway = 1, WalkToAnchor = 2, RoarStart = 3, Roar = 4, RoarEnd = 5, MouthOpen = 6, MouthClose = 7,
    Leap = 8, TurnBack = 9, Land = 0xa
};
enum class RoarStep : uint8_t { Start = 0, Wind = 1, Spit = 2, End = 3 };
enum class QuakeStep : uint8_t { Shake = 0, Debris = 1, End = 2 };
enum class BiteStep : uint8_t { Bite = 0, Recover = 1 };
enum class BreathStep : uint8_t { Start = 0, Breathe = 1, Relax = 2, End = 3 };

// the animation key's event bits (the key's third byte)
constexpr uint8_t kEventPause = 0x01, kEventStill = 0x02, kEventFrontFoot = 0x04, kEventBackFoot = 0x08,
                  kEventMoving = 0x10, kEventLanding = 0x20, kEventMouthOpen = 0x40, kEventMouthClose = 0x80;
// its status bits
constexpr uint8_t kDecodeOnly = 0x01, kFirstFightBeaten = 0x04;
constexpr uint8_t kTurnLeft = 0x2f; // the step key of a turn that lowers the yaw (and of the front foot)
constexpr uint8_t kNoAnimChange = 0xff;
constexpr uint8_t kFightNoDebris = 2; // the spawn byte of the ST45 group: no debris rain, no quake
constexpr uint32_t kFlagSecondFight = 0x4c, kFlagSecondFightWon = 0x4a, kFlagTwoThirdsHurt = 0x680,
                   kFlagRoomOpens = 0x176;
constexpr size_t kHeadPartEntry = 4; // the decoded frame's entry of part 3 (the head)
constexpr size_t kMouthUpperPart = 5, kMouthLowerPart = 4;
constexpr uint8_t kSpitBlobType = 0x1a, kMouthShotType = 0xf, kShockWaveType = 0x3c, kDebrisType = 0x3b;
constexpr uint32_t kShotGroup = 3; // the hit-list group of the boss's shots
// the fight rows' tables (u8 by the spawn byte: the row index)

// One fight row of the boss's type (0x10 bytes; the stat table's row of the fight, its second half for the second
// fight): its hit points, its body's attack, its shots' attack, its hit entries' height, the shock wave's attack, the
// breath's attack
struct FightRow : DataRecord {
    static constexpr uint32_t kBytes = 0xb;
    uint8_t hitPoints = 0; // +0x0
    uint8_t attack = 0; // +0x1
    uint8_t shotAttack = 0; // +0x3
    uint8_t hitHeight = 0; // +0x4
    uint8_t waveAttack = 0; // +0x9
    uint8_t breathAttack = 0; // +0xa
    template <class R> static FightRow load(const R& r) {
        FightRow v;
        v.hitPoints = r.template read<uint8_t>(0x0);
        v.attack = r.template read<uint8_t>(0x1);
        v.shotAttack = r.template read<uint8_t>(0x3);
        v.hitHeight = r.template read<uint8_t>(0x4);
        v.waveAttack = r.template read<uint8_t>(0x9);
        v.breathAttack = r.template read<uint8_t>(0xa);
        return v;
    }
};
FightRow fightRow(const AnimatedActor& actor, uint32_t table, bool second) {
    const uint32_t index = uint32_t(DataArray<uint8_t>(table)[actor.spawnByte]) << 5;
    return dataRecord<FightRow>(index + statTableRow(actorStatTables().animated, actor.type) + (second ? 0x10u : 0u), FightRow::kBytes);
}

bool secondFight() { return storyFlag(kFlagSecondFight); }
bool playerIn(uint32_t box) { return pointInCellBox(player().pos, CellBox(box)); }
int playerDistance(const AnimatedActor& actor) { return planarDistance(player().pos, actor.pos); }
int angleToPlayer(const AnimatedActor& actor) { return angleOf(player().pos, actor.pos); }
void ask(AnimatedActor& actor, BossFields& boss, uint8_t anim, uint8_t key = 0) {
    actor.anim = anim;
    boss.startKey = key;
}
void enterMove(AnimatedActor& actor, BossMove move) {
    actor.subState = uint8_t(move);
    actor.step = 0;
    actor.stepCounter = 0;
}

// exe 0x4caee0(actor, target): the target is within 0x100 of the actor's yaw + 0x780 (it faces it)
bool facingAngle(const AnimatedActor& actor, int angle) {
    return int16_t(uint16_t(angle - uint16_t(actor.rotation.y) - 0x780) & 0xfff) <= 0x100;
}
bool facingPlayer(const AnimatedActor& actor) { return facingAngle(actor, angleOf(actor.pos, player().pos)); }
bool facingPoint(const AnimatedActor& actor, const int32_t t[3]) {
    return facingAngle(actor, angleOf(actor.pos.x, actor.pos.z, t[0], t[2]));
}
// exe 0x4caf10(actor, k, entry): a foot down (the key's front / back foot event, not landing) -> its step key and,
// for k >= 0, the animation of the pair TableId::St45BossFootAnims[k] (front / back); true when no foot is down
bool noFootDown(AnimatedActor& actor, BossFields& boss, int k, uint8_t entry) {
    const uint8_t f = actor.animKey.events;
    const DataArray<uint8_t> footAnims(stateAddress(TableId::St45BossFootAnims));
    if ((f & kEventFrontFoot) && !(f & kEventLanding)) {
        boss.stepKey = kTurnLeft;
        if (k >= 0) ask(actor, boss, footAnims[size_t(k) * 2], entry);
        return false;
    }
    if ((f & kEventBackFoot) && !(f & kEventLanding)) {
        boss.stepKey = 0;
        if (k >= 0) ask(actor, boss, footAnims[size_t(k) * 2 + 1], entry);
        return false;
    }
    return true;
}
// exe 0x4caf90(actor, anim (kNoAnimChange: kept), z, entry): the animation, then the move (0, 0, z)
void animMove(AnimatedActor& actor, BossFields& boss, uint8_t anim, int z, uint8_t entry) {
    if (anim != kNoAnimChange) ask(actor, boss, anim, entry);
    integrateVelocity(actor, 0, 0, int16_t(z)); // exe 0x5e3690
}
// a point of the exe's tables of the arena: its x / z
struct TablePoint : DataRecord {
    static constexpr uint32_t kBytes = 0x4;
    int16_t x = 0; // +0x0
    int16_t z = 0; // +0x2
    template <class R> static TablePoint load(const R& r) {
        TablePoint v;
        v.x = r.template read<int16_t>(0x0);
        v.z = r.template read<int16_t>(0x2);
        return v;
    }
};
// exe 0x4cafd0: the nearest of the 11 points TableId::St45BossNearPoints (x / z) to the bone-3 point (0, 0x10, -0x40)
uint8_t nearestPoint(const AnimatedActor& actor) {
    int16_t out[3];
    bonePt(actor, 3, 0, 0x10, -0x40, out);
    int best = 0x7fff;
    uint8_t k = 0;
    out[1] = 0;
    for (uint8_t i = 0; i < 0xb; ++i) {
        const TablePoint point = dataRecord<TablePoint>(stateAddress(TableId::St45BossNearPoints) + i * 4u, TablePoint::kBytes);
        const int16_t in[3] = {point.x, 0, point.z};
        const int d = planarDistance16(out, in);
        if (best > d) {
            best = d;
            k = i;
        }
    }
    return k;
}
// exe 0x4cb060: the arena point for the player's ninth of the arena (the cell boxes TableId::St45BossArenaCells): x / z the arena
// edges, y the player's; no box (or box 0): the exe leaves its locals uninitialised (0 here)
void arenaPoint(int32_t out[3]) {
    int found = 0;
    for (int i = 0; i < 9; ++i)
        if (pointInCellBox(player().pos, CellBox(stateAddress(TableId::St45BossArenaCells) + uint32_t(i) * 4))) {
            found = i;
            break;
        }
    int16_t x = 0, y = 0, z = 0;
    const Player& p = player();
    const int16_t px = p.pos.x.whole, py = p.pos.y.whole, pz = p.pos.z.whole;
    switch (found - 1) {
    case 0: x = -0x900; y = py; z = -0xb00; break;
    case 1: x = px; y = py; z = -0xb00; break;
    case 2: x = 0x900; y = py; z = -0xb00; break;
    case 3: x = -0x900; y = py; z = pz; break;
    case 4: x = 0x900; y = py; z = pz; break;
    case 5: x = -0x900; y = py; z = 0x700; break;
    case 6: x = px; y = py; z = 0x700; break;
    case 7: x = 0x900; y = py; z = 0x700; break;
    default: break;
    }
    out[0] = int32_t(uint32_t(uint16_t(x)) << 16);
    out[1] = int32_t(uint32_t(uint16_t(y)) << 16);
    out[2] = int32_t(uint32_t(uint16_t(z)) << 16);
}
// the point x = z = -0x100 at the player's height (the exe's local; its fractions uninitialised, 0 here)
void arenaCentre(int32_t out[3]) {
    out[0] = int32_t(0xff000000u);
    out[1] = int32_t(uint32_t(uint16_t(player().pos.y.whole)) << 16);
    out[2] = int32_t(0xff000000u);
}
// exe 0x4cb2c0: kTurnLeft when turning toward the player (step 8) would lower the yaw, else 0 (the yaw kept)
uint8_t turnSide(AnimatedActor& actor) {
    const int16_t yaw = actor.rotation.y;
    turnYawTo(actor.rotation.y, uint32_t(angleToPlayer(actor)), 8);
    const int d = int(yaw) - int(actor.rotation.y);
    actor.rotation.y = yaw;
    return d > 0 ? kTurnLeft : 0;
}
void turnToPlayer(AnimatedActor& actor, int step) { turnYawTo(actor.rotation.y, uint32_t(angleToPlayer(actor)), step); }
void turnToPoint(AnimatedActor& actor, const int32_t t[3], int step) {
    turnYawTo(actor.rotation.y, uint32_t(angleOf(t[0], t[2], actor.pos.x, actor.pos.z)), step);
}
int pointDistance(const AnimatedActor& actor, const int32_t t[3]) { return planarDistance(actor.pos, t); }

// The shock wave's spawn bytes: the grey of its packets, its attack
struct ShockWaveParams {
    explicit ShockWaveParams(const StaticActor& actor) : ShockWaveParams(const_cast<StaticActor&>(actor)) {}
    explicit ShockWaveParams(StaticActor& actor) : grey(actor.params[0]), attack(actor.params[1]) {}
    uint8_t& grey;
    FieldAlias<int8_t, uint8_t> attack;
};
// exe 0x4cb1a0: the shock wave (pool 0x60 type 0x3c sub 0) 0x280 ahead, the boss's root block copied
void spawnWave(const AnimatedActor& actor) {
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (slot == 0) return;
    StaticActor& wave = staticActorAt(slot);
    ShockWaveParams params(staticActorAt(slot));
    wave.flags = kActorLive;
    wave.type = kShockWaveType;
    wave.subtype = 0;
    wave.variant = 0;
    wave.spawnByte = 0;
    int16_t dx = 0, dz = 0;
    angleVector(actor.rotation.y, 0x280, dx, dz); // exe 0x5e40c0
    wave.pos.x.whole = int16_t(actor.pos.x.whole + dx);
    wave.pos.y.whole = actor.pos.y.whole;
    wave.pos.z.whole = int16_t(actor.pos.z.whole + dz);
    wave.rotation.y = actor.rotation.y;
    wave.rotation.z = 0;
    wave.rotation.x = 0;
    params.attack = int8_t(fightRow(actor, stateAddress(TableId::St45BossWaveRows), actor.spawnByte != 0 || secondFight()).waveAttack);
    uint32_t root[8];
    for (size_t i = 0; i < 8; ++i) root[i] = actor.rootWord(i);
    wave.setRootWords(root);
}
// exe 0x4cb300: the next attack (the moves stomp / roar / quake / second stomp / bite / breath)
void pickAttack(AnimatedActor& actor, BossFields& boss) {
    if (!secondFight() && actor.spawnByte != kFightNoDebris) {
        const uint32_t r = rngStep();
        if (r & 3) {
            boss.blobs = 0;
            for (size_t i = 0; i < projectilePool().size(); ++i) {
                const ProjectileActor& shot = projectilePool()[i];
                if (shot.flags != 0 && shot.type == kSpitBlobType) boss.blobs += 1u;
            }
            bool roar = int8_t(boss.counter) > 2;
            if (!roar) roar = (rngStep() & 3) == 0;
            if (roar && int8_t(boss.blobs) < 8) {
                boss.counter = 0;
                enterMove(actor, BossMove::Roar);
                return;
            }
            ask(actor, boss, 0x26);
            enterMove(actor, BossMove::Stomp);
            boss.counter += 1u;
            return;
        }
        ask(actor, boss, 0x24);
        enterMove(actor, BossMove::Quake);
        return;
    }
    if (playerDistance(actor) < 0xa00 && (rngStep() & 1)) {
        ask(actor, boss, turnSide(actor) ? 0x15 : 0x17);
        enterMove(actor, BossMove::Bite);
        boss.soundTimer = 0x16;
        sound(0x20e, actor);
        return;
    }
    const bool stomp = (rngStep() & 3) != 0;
    ask(actor, boss, stomp ? 0x19 : 0x11);
    enterMove(actor, stomp ? BossMove::SecondStomp : BossMove::Breath);
}
// The mouth shot's spawn bytes: its attack (the header's step counter byte)
struct MouthShotParams {
    explicit MouthShotParams(const ProjectileActor& actor) : MouthShotParams(const_cast<ProjectileActor&>(actor)) {}
    explicit MouthShotParams(ProjectileActor& actor) : attack(actor.stepCounter) {}
    uint8_t& attack;
};
// exe 0x4cb4e0: facing the player, the mouth's sequence: its parts closed / opened, at tick 0x14 the shot (pool 0x40
// type 0xf) from bone 3 aimed at the player's chest
void mouthShot(AnimatedActor& actor, BossFields& boss) {
    const FightRow row = fightRow(actor, stateAddress(TableId::St45BossMouthShotRows), actor.spawnByte == kFightNoDebris || secondFight());
    if (!facingPlayer(actor) && boss.mouthTick == 0) return;
    if (actor.animKey.events & kEventMouthOpen) boss.mouthTick = 1;
    switch (boss.mouthTick) {
    case 3: actor.parts[kMouthUpperPart].flags |= kPartHidden; break;
    case 4: actor.parts[kMouthLowerPart].flags |= kPartHidden; break;
    case 0x14: {
        const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
        if (slot == 0) break;
        ProjectileActor& shot = projectileActorAt(slot);
        shot.flags = kActorLive;
        shot.loopFlags |= kActorSweepGroup2;
        shot.type = kMouthShotType;
        // in.x / in.y: the exe's uninitialised stack words (0 here)
        int z = -0x100;
        if (secondFight() || actor.spawnByte == kFightNoDebris) z = -0x80;
        int16_t out[3];
        bonePt(actor, 3, 0, 0, z, out);
        shot.pos.x.whole = out[0];
        shot.pos.y.whole = out[1];
        shot.pos.z.whole = out[2];
        shot.velocity.setXY(0, 0);
        shot.velocity.z = int16_t(0xfb00);
        const Player& p = player();
        const int32_t chest[3] = {p.pos.x, int32_t(uint32_t(p.pos.y) - 0x640000u), p.pos.z};
        int16_t to[3];
        wholes(chest, to);
        shot.rotation.x = int16_t(pitchOf(shot.pos, to) + 0x800);
        shot.rotation.y = int16_t(uint16_t(actor.rotation.y) + uint16_t(boss.headTurn));
        shot.rotation.z = 0;
        shot.setParamsWord(0x40430);
        MouthShotParams(projectileActorAt(slot)).attack = row.shotAttack;
        break;
    }
    case 0x1e: actor.parts[kMouthLowerPart].flags &= uint8_t(~kPartHidden); break;
    case 0x1f:
        actor.parts[kMouthUpperPart].flags &= uint8_t(~kPartHidden);
        boss.mouthTick = 0;
        break;
    default: break;
    }
    if (boss.mouthTick != 0) boss.mouthTick += 1u;
}
// exe 0x4cb730: the head's pitch toward the player (bone 3's point (0, 0, -0x100)), 0x20 a tick, within +-0x200
void headPitchToPlayer(AnimatedActor& actor, BossFields& boss) {
    int16_t out[3], p[3];
    bonePt(actor, 3, 0, 0, -0x100, out);
    wholes(player().pos, p);
    const int16_t want = t16(pitchOf(out, p) + 0x800);
    const int16_t d = t16(angleDiff(uint32_t(int32_t(boss.headPitch)), uint32_t(int32_t(want))));
    if (d + 0x20 > 0x40) boss.headPitch += (d > 0 ? 0x20u : 0xffe0u);
    else boss.headPitch = want;
    const int16_t e = t16(angleDiff(0, uint32_t(int32_t(boss.headPitch))));
    if (e > 0x200) boss.headPitch = 0x200;
    else if (e < -0x200) boss.headPitch = int16_t(0xfe00);
}
// exe 0x4cb800: the head's pitch and turn back to 0 (0x20 a tick)
void headRelax(BossFields& boss) {
    const int16_t p = boss.headPitch;
    boss.headPitch = p > 0x1000 ? int16_t(p - 0x20) : int16_t(0);
    const int16_t t = boss.headTurn;
    if (t > 0) {
        const int16_t n = t16(t - 0x20);
        boss.headTurn = n > 0 ? n : int16_t(0);
    } else if (t < 0) {
        const int16_t n = t16(t + 0x20);
        boss.headTurn = n < 0 ? n : int16_t(0);
    }
}
// exe 0x4cb860: the head's turn toward the player (the body's yaw kept), within +-0x200
void headTurnToPlayer(AnimatedActor& actor, BossFields& boss) {
    const int ang = angleToPlayer(actor);
    const uint16_t yaw = uint16_t(actor.rotation.y);
    actor.rotation.y = int16_t(uint16_t(boss.headTurn) + yaw);
    turnYawTo(actor.rotation.y, uint32_t(ang), 8);
    boss.headTurn = int16_t(uint16_t(actor.rotation.y) - uint16_t(actor.prevRotation.y));
    const int16_t d = t16(angleDiff(0, uint32_t(int32_t(boss.headTurn))));
    if (d > 0x200) boss.headTurn = 0x200;
    else if (d < -0x200) boss.headTurn = int16_t(0xfe00);
    actor.rotation.y = int16_t(yaw);
}
void headFollow(AnimatedActor& actor, BossFields& boss) {
    headPitchToPlayer(actor, boss);
    headTurnToPlayer(actor, boss);
}
// exe 0x4cb8e0(actor, point): a foot on the ground (the point's height checked through the actor) -> the point's y =
// 0x10 and, every 8 ticks, the footstep dust
void footCheck(const PlayerEnv& env, AnimatedActor& actor, int16_t pt[3]) {
    if (secondFight() || actor.spawnByte == kFightNoDebris) return;
    const int32_t x = actor.pos.x, y = actor.pos.y, z = actor.pos.z;
    actor.pos.x.whole = pt[0];
    actor.pos.y.whole = pt[1];
    actor.pos.z.whole = pt[2];
    if (pt[1] > -0x4a && actorGround(env, actor) >= 0x10) { // exe 0x40f190
        pt[1] = 0x10;
        if ((frameCounter() & 7) == 0) footstepDustAt(actor, 1, pt); // exe 0x5ad170
    }
    actor.pos.x = x;
    actor.pos.y = y;
    actor.pos.z = z;
}
// the walk's step undone when it leaves the arena box
void keepInBox(AnimatedActor& actor, const BossFields& boss, uint8_t backStep, uint32_t box) {
    if (actor.step != backStep && !(boss.status & kFirstFightBeaten) && !pointInCellBox(actor.pos, CellBox(box)))
        actor.pos = actor.prevPos;
}

// ---- the moves [0x894f44] ------------------------------------------------------------------------------------------

void walkPark(AnimatedActor& actor, BossFields& boss) { // the shared step back (0x4c845f)
    animMove(actor, boss, kNoAnimChange, -0x630, 0);
}
// the set-up of a step: the step after it, its animation, its start key, its distance
void setUpStep(BossFields& boss, uint8_t next, uint8_t anim) {
    boss.nextStep = next;
    boss.stepAnim = anim;
}
// exe 0x4c8410: walking the arena (approach, pause, decide, step, recoil, stalk + mouth shot, turns, back to the
// arena, fall back); a step that leaves the arena box is undone
void walk(AnimatedActor& actor, BossFields& boss) {
    int32_t target[3] = {}; // the exe's local 16.16 point
    enum class After { Keep, ClearDecode } next = After::Keep;
    auto turnInPlace = [&]() {
        if (!actor.animEnded()) return After::Keep;
        actor.step = uint8_t(WalkStep::Turn);
        actor.anim = 0x29;
        boss.startKey = turnSide(actor);
        return After::ClearDecode;
    };
    auto stepToward = [&]() { // a step toward, animation 0x26
        walkPark(actor, boss);
        actor.step = uint8_t(WalkStep::Step);
        setUpStep(boss, uint8_t(WalkStep::Decide), 0x26);
        boss.stepDistance = 0;
        boss.stepKey = 0;
        return After::ClearDecode;
    };
    auto turnStep = [&]() {
        walkPark(actor, boss);
        actor.step = uint8_t(WalkStep::TurnStep);
        setUpStep(boss, uint8_t(WalkStep::Turn), 0x29);
        boss.stepDistance = 0;
        return After::ClearDecode;
    };
    auto stalk = [&]() {
        turnToPlayer(actor, 8);
        integrateVelocity(actor, 0, 0, -0xb2);
        return After::Keep;
    };
    auto pause = [&]() {
        boss.timer = 4;
        boss.status |= kDecodeOnly;
        ++actor.step;
        return After::Keep;
    };
    auto stepBack = [&]() {
        walkPark(actor, boss);
        actor.step = uint8_t(WalkStep::Step);
        setUpStep(boss, uint8_t(WalkStep::Recoil), 0x10);
        boss.stepDistance = 0;
        boss.stepKey = 0;
        return After::ClearDecode;
    };
    auto attackIfFootDown = [&]() {
        if (!noFootDown(actor, boss, -1, 0)) pickAttack(actor, boss);
        return After::Keep;
    };
    auto closeIn = [&]() {
        if (noFootDown(actor, boss, 0, 0)) return After::Keep;
        walkPark(actor, boss);
        actor.step = uint8_t(WalkStep::Step);
        setUpStep(boss, uint8_t(WalkStep::Stalk), actor.anim);
        boss.stepDistance = 0;
        boss.stepKey = 0;
        return After::ClearDecode;
    };
    auto recoil = [&]() {
        actor.step = uint8_t(WalkStep::Recoil);
        ask(actor, boss, 0x10);
        return After::ClearDecode;
    };
    auto longStep = [&]() {
        if (noFootDown(actor, boss, 1, 0)) return After::Keep;
        actor.step = uint8_t(WalkStep::Step);
        setUpStep(boss, uint8_t(WalkStep::Approach), 1);
        boss.stepKey = 0;
        boss.stepDistance = 0x40;
        return After::ClearDecode;
    };
    auto endStep = [&]() {
        if (!actor.animEnded()) return After::Keep;
        const uint8_t anim = boss.stepAnim;
        if (anim == 0x26) enterMove(actor, BossMove::Stomp);
        else actor.step = boss.nextStep;
        if (boss.stepDistance != 0) {
            animMove(actor, boss, anim, t16(-int(int8_t(boss.stepDistance)) * 16), boss.stepKey);
            return After::Keep;
        }
        if (WalkStep(actor.step) == WalkStep::TurnStep) {
            if (facingPlayer(actor)) {
                actor.step = uint8_t(WalkStep::Stalk);
                return After::Keep;
            }
        }
        const uint8_t d = boss.stepAnim;
        if (int(int8_t(boss.stepAnim)) != int(actor.anim)) ask(actor, boss, d, boss.stepKey);
        return After::Keep;
    };
    switch (WalkStep(actor.step)) {
    case WalkStep::Approach:
        if (actor.animKey.events & kEventPause) {
            next = pause();
            break;
        }
        if ((boss.status & kFirstFightBeaten) && !noFootDown(actor, boss, 0, 0)) {
            next = stepBack();
            break;
        }
        if (playerIn(stateAddress(TableId::St45BossArenaBox))) {
            if (actor.animKey.events & kEventStill) break;
            if (playerDistance(actor) < 0xc00) {
                if (facingPlayer(actor)) {
                    if (!noFootDown(actor, boss, 0, 0)) {
                        next = stepToward();
                        break;
                    }
                } else if (!noFootDown(actor, boss, 0, 0)) {
                    walkPark(actor, boss);
                    actor.step = uint8_t(WalkStep::TurnStep);
                    setUpStep(boss, uint8_t(WalkStep::Turn), 0x29);
                    boss.stepKey = turnSide(actor);
                    boss.stepDistance = 0;
                    next = After::ClearDecode;
                    break;
                }
            }
            next = stalk();
            break;
        }
        arenaPoint(target);
        if (actor.animKey.events & kEventStill) break;
        if (playerDistance(actor) < 0xc00) {
            if (facingPlayer(actor)) {
                if (!noFootDown(actor, boss, 0, 0)) {
                    next = stepToward();
                    break;
                }
            } else if (!noFootDown(actor, boss, 0, 0)) {
                next = turnStep();
                break;
            }
        }
        if (pointDistance(actor, target) < 0x300) {
            if (noFootDown(actor, boss, 0, 0)) break;
            next = turnStep();
            break;
        }
        turnToPoint(actor, target, 0x10);
        integrateVelocity(actor, 0, 0, -0xb2);
        break;
    case WalkStep::Pause:
    case WalkStep::PauseAgain:
        if (boss.timer == 0) {
            actor.step -= 1u;
            next = After::ClearDecode;
        } else {
            boss.timer -= 1u;
        }
        break;
    case WalkStep::Decide:
        if (boss.status & kFirstFightBeaten) {
            actor.step = uint8_t(WalkStep::Fall);
            break;
        }
        if (uint32_t(distance3(player().pos, actor.pos)) < 0xc00)
            next = facingPlayer(actor) ? attackIfFootDown() : turnInPlace();
        else if (facingPlayer(actor)) actor.step = uint8_t(WalkStep::Stalk);
        else next = turnInPlace();
        break;
    case WalkStep::TurnStep:
        if (boss.stepAnim == 0x29 && int(int8_t(boss.stepKey)) != int(turnSide(actor))) turnToPlayer(actor, 8);
        next = endStep();
        break;
    case WalkStep::Step: next = endStep(); break;
    case WalkStep::Recoil:
        if (actor.animEnded()) {
            enterMove(actor, BossMove::Retreat);
            break;
        }
        if (actor.stepCounter) integrateVelocity(actor, 0, 0, -0x160);
        else if (actor.animKey.events & kEventMoving) actor.stepCounter = 1;
        break;
    case WalkStep::Stalk:
        if ((boss.status & kFirstFightBeaten) && actor.animEnded()) {
            next = recoil();
            break;
        }
        mouthShot(actor, boss);
        if (playerIn(stateAddress(TableId::St45BossArenaBox))) {
            if (facingPlayer(actor)) {
                if (playerDistance(actor) < 0xc00 && !noFootDown(actor, boss, -1, 0)) {
                    pickAttack(actor, boss);
                    break;
                }
                next = longStep();
            } else if (actor.animEnded()) {
                next = turnInPlace();
            } else {
                next = longStep();
            }
            break;
        }
        if (facingPlayer(actor) && playerDistance(actor) < 0xc00 && !noFootDown(actor, boss, -1, 0)) {
            pickAttack(actor, boss);
            break;
        }
        arenaPoint(target);
        if (pointDistance(actor, target) > 0x600) {
            next = longStep();
            break;
        }
        if (playerDistance(actor) < 0xc00 || !facingPlayer(actor)) {
            next = turnInPlace();
            break;
        }
        if (noFootDown(actor, boss, -1, 0)) break;
        enterMove(actor, BossMove::Roar);
        break;
    case WalkStep::Return:
        if (actor.animKey.events & kEventPause) {
            next = pause();
            break;
        }
        if ((boss.status & kFirstFightBeaten) && !noFootDown(actor, boss, 0, 0)) {
            next = stepBack();
            break;
        }
        if (playerIn(stateAddress(TableId::St45BossArenaBox))) {
            actor.step = uint8_t(WalkStep::Stalk);
            actor.stepCounter = 0;
            break;
        }
        arenaPoint(target);
        if (actor.animKey.events & kEventStill) break;
        turnToPlayer(actor, 0x10);
        if (!facingPlayer(actor)) break;
        if (pointDistance(actor, target) >= 0x600) {
            actor.step = uint8_t(WalkStep::Approach);
            actor.stepCounter = 0;
            break;
        }
        next = closeIn();
        break;
    case WalkStep::LongStep:
        actor.step = uint8_t(WalkStep::Step);
        setUpStep(boss, uint8_t(WalkStep::Turn), 0x29);
        boss.stepKey = 1;
        boss.stepDistance = 0x40;
        next = After::ClearDecode;
        break;
    case WalkStep::Turn: {
        if ((boss.status & kFirstFightBeaten) && !noFootDown(actor, boss, 4, 0)) {
            next = recoil();
            break;
        }
        if (!(actor.animKey.events & kEventStill)) turnToPlayer(actor, 0xc);
        if (!facingPlayer(actor)) break;
        if (playerDistance(actor) < 0xc00) {
            next = attackIfFootDown();
            break;
        }
        const uint8_t f = actor.animKey.events;
        if (((f & kEventFrontFoot) && !(f & kEventLanding)) || ((f & kEventBackFoot) && !(f & kEventLanding))) {
            actor.step = uint8_t(WalkStep::Stalk);
            next = After::ClearDecode;
        }
        break;
    }
    case WalkStep::BackToArena:
        arenaCentre(target);
        if (actor.animKey.events & kEventStill) break;
        if (facingPoint(actor, target)) {
            integrateVelocity(actor, 0, 0, -0xb2);
            if (!pointInCellBox(actor.pos, CellBox(stateAddress(TableId::St45BossArenaBox)))) break;
            next = closeIn();
        } else {
            turnToPoint(actor, target, 0x10);
        }
        break;
    case WalkStep::Fall:
        if (!actor.animEnded()) break;
        next = recoil();
        break;
    default: break;
    }
    if (next == After::ClearDecode) boss.status &= uint8_t(~kDecodeOnly);
    keepInBox(actor, boss, uint8_t(WalkStep::BackToArena), stateAddress(TableId::St45BossArenaBox));
    if (actor.animKey.events & kEventLanding) {
        cameraShakeAt(0x2000, 0x400, 0x100, 0x1000, actor.pos); // exe 0x40eab0
        int16_t out[3];
        bonePt(actor, (actor.animKey.events & kEventFrontFoot) ? 0xe : 0x11, 0, 0x40, -0x60, out);
        const uint32_t slot = allocActorA0(0); // exe 0x5e13b0: the dust ring (global effect type 7)
        if (slot) {
            EffectActor& dust = effectActorAt(slot);
            DustPuffFields& puff = classState<DustPuffFields>(effectActorAt(slot));
            dust.flags = kActorLive;
            dust.loopFlags |= kActorLateLoop;
            dust.type = typeByte(GlobalEffectType::DustPuff);
            dust.pos.x = out[0];
            dust.pos.y = 0;
            dust.pos.z = out[2];
            puff.size = 0x200;
            puff.ticks = 0x10;
            puff.airborne = 0;
        }
        sound(0x14d, actor);
    }
    headFollow(actor, boss);
}
// The falling debris's spawn bytes (pool 0x60 type 0x3b sub 1; its class is not in the ST45 table): its kind
struct DebrisParams {
    explicit DebrisParams(const StaticActor& actor) : DebrisParams(const_cast<StaticActor&>(actor)) {}
    explicit DebrisParams(StaticActor& actor) : kind(actor.params[0]) {}
    uint8_t& kind;
};
// the debris rain of the stomps: three drops near bone 3 (exe 0x4c8e10 / 0x4ca790 inline it)
void debrisRain(const AnimatedActor& actor) {
    for (int i = 0; i < 3; ++i) {
        const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (slot == 0) continue;
        StaticActor& debris = staticActorAt(slot);
        debris.flags = kActorLive;
        debris.type = kDebrisType;
        debris.subtype = 1;
        debris.variant = 0;
        debris.spawnByte = 0;
        DebrisParams(debris).kind = 1;
        debris.rotation.z = 0;
        debris.rotation.x = 0;
        debris.rotation.y = 0x800;
        int16_t out[3];
        bonePt(actor, 3, 0, 0x10, -0x40, out);
        uint32_t r = rngStep();
        debris.pos.y.whole = int16_t(0xf800);
        debris.pos.x.whole = int16_t((r & 0x1fff) + uint16_t(out[0]) - 0x7ffu);
        r = rngStep();
        debris.velocity.z = 0;
        debris.velocity.x = 0;
        debris.pos.z.whole = int16_t((r & 0x1fff) + uint16_t(out[2]) - 0x7ffu);
    }
}
// exe 0x4c8e10: the stomp (the landing -> the shock wave, the debris rain while the timer runs, the recovery)
void stomp(AnimatedActor& actor, BossFields& boss) {
    switch (StompStep(actor.step)) {
    case StompStep::Land:
        if (actor.animEnded()) {
            boss.timer = 0x1e;
            ++actor.step;
            break;
        }
        if (actor.animKey.events & kEventLanding) {
            cameraShakeAt(0x6000, 0x400, 0x100, 0x1000, actor.pos);
            spawnWave(actor);
            sound(0x149, actor);
        }
        break;
    case StompStep::Rain:
        if (boss.timer == 0) {
            ask(actor, boss, 0x1a);
            integrateVelocity(actor, 0, 0, -0x1d40);
            ++actor.step;
            break;
        }
        boss.timer -= 1u;
        if (actor.spawnByte != kFightNoDebris && !(frameCounter() & 3)) debrisRain(actor);
        break;
    case StompStep::Recover:
        if (!actor.animEnded()) break;
        if (!(boss.status & kFirstFightBeaten)) {
            ask(actor, boss, 0xf);
            ++actor.step;
        } else {
            actor.subState = uint8_t(BossMove::Retreat);
            actor.step = 0;
        }
        break;
    case StompStep::StepBack:
        if (actor.animEnded()) {
            actor.subState = uint8_t(BossMove::Walk);
            actor.step = uint8_t(playerIn(stateAddress(TableId::St45BossRetreatBox)) ? WalkStep::Decide : WalkStep::Stalk);
            actor.stepCounter = 0;
            ask(actor, boss, 0);
            break;
        }
        if (actor.stepCounter) integrateVelocity(actor, 0, 0, 0x120);
        else if (actor.animKey.events & kEventMoving) actor.stepCounter = 1;
        break;
    default: break;
    }
    headRelax(boss);
}

// exe 0x4c9120: the retreat -- walk to the nearest anchor point, the long roar, the mouth parts, the leap back (back
// to the walk's BackToArena); footstep sounds by the animation's key
void retreat(AnimatedActor& actor, BossFields& boss) {
    int16_t from[3] = {}, anchor[3] = {};
    auto points = [&]() {
        wholes(actor.pos, from);
        const TablePoint point = dataRecord<TablePoint>(stateAddress(TableId::St45BossAnchorPoints) + uint32_t(boss.anchor) * 4, TablePoint::kBytes);
        anchor[0] = point.x;
        anchor[1] = 0;
        anchor[2] = point.z;
    };
    auto offAnchor = [&]() {
        return int16_t(uint16_t(angleOf(from, anchor) - uint16_t(actor.rotation.y) - 0x7f0) & 0xfff);
    };
    const uint8_t step = actor.step;
    switch (RetreatStep(step)) {
    case RetreatStep::Start:
        ask(actor, boss, 0xc);
        actor.step = uint8_t(step + 1u);
        actor.stepCounter = 0;
        boss.anchor = nearestPoint(actor);
        break;
    case RetreatStep::TurnAway: {
        if (actor.animEnded()) {
            actor.step = uint8_t(RetreatStep::WalkToAnchor);
            actor.stepCounter = 0;
            ask(actor, boss, 3, 0x1c);
            break;
        }
        const uint8_t f = actor.animKey.events;
        if (f & kEventStill) actor.stepCounter = 0;
        if (actor.stepCounter) {
            points();
            if (offAnchor() <= 0x20) integrateVelocity(actor, 0, 0, -0x280);
            turnYawTo(actor.rotation.y, uint32_t(angleOf(anchor, from)), 8);
        } else if (f & kEventMoving) {
            actor.stepCounter = 1;
        }
        break;
    }
    case RetreatStep::WalkToAnchor:
        if (actor.animKey.events & kEventStill) break;
        points();
        if (offAnchor() > 0x20 && actor.stepCounter == 0) {
            turnYawTo(actor.rotation.y, uint32_t(angleOf(anchor, from)), 8);
        } else {
            integrateVelocity(actor, 0, 0, -0x140);
            actor.stepCounter = 1;
        }
        if (planarDistance16(from, anchor) < 0x400 && !noFootDown(actor, boss, 3, 0)) ++actor.step;
        break;
    case RetreatStep::RoarStart:
        if (!actor.animEnded()) break;
        ask(actor, boss, 0x1e);
        actor.step = uint8_t(step + 1u);
        boss.timer = 0xb4;
        boss.soundTimer = 0x10;
        break;
    case RetreatStep::Roar:
        if (boss.timer != 0) {
            if (boss.soundTimer != 0) {
                boss.soundTimer -= 1u;
            } else {
                sound(0x20c, actor);
                boss.soundTimer = 0x1e;
            }
            boss.timer -= 1u;
            if (int16_t(boss.timer) >= 0xa5) break;
            int16_t out[3];
            bonePt(actor, 3, 0, 0x10, -0x40, out);
            out[1] = 0x10;
            if ((frameCounter() & 7) == 0) footstepDustAt(actor, 1, out); // exe 0x5ad170
        } else if (actor.animEnded()) {
            ask(actor, boss, 0x1f);
            ++actor.step;
        }
        break;
    case RetreatStep::RoarEnd:
        if (!actor.animEnded()) break;
        ask(actor, boss, 0x20);
        actor.step = uint8_t(step + 1u);
        boss.timer = 0x1e;
        break;
    case RetreatStep::MouthOpen:
        if (actor.animEnded()) {
            if (boss.timer != 0) {
                boss.timer -= 1u;
            } else {
                ask(actor, boss, 0x28);
                actor.stepCounter = 0;
                actor.step = uint8_t(step + 1u);
            }
        } else if (actor.animKey.events & kEventMouthOpen) {
            actor.parts[kMouthLowerPart].flags |= kPartHidden;
            actor.parts[kMouthUpperPart].flags |= kPartHidden;
        }
        break;
    case RetreatStep::MouthClose:
        if (actor.animEnded()) {
            actor.step = uint8_t(step + 1u);
        } else if (actor.animKey.events & kEventMouthClose) {
            if (actor.stepCounter == 0) {
                actor.stepCounter = 1;
                actor.parts[kMouthLowerPart].flags &= uint8_t(~kPartHidden);
            } else {
                actor.parts[kMouthUpperPart].flags &= uint8_t(~kPartHidden);
            }
        }
        break;
    case RetreatStep::Leap:
        if (noFootDown(actor, boss, 1, 0)) break;
        actor.anim = 0x29;
        boss.startKey = turnSide(actor);
        actor.step = uint8_t(RetreatStep::TurnBack);
        actor.hitPoints = actor.maxHitPoints;
        boss.status &= uint8_t(~(kDecodeOnly | kFirstFightBeaten));
        integrateVelocity(actor, 0, 0, 0x1e30);
        break;
    case RetreatStep::TurnBack: {
        // the exe fills its local point only when the key's still bit is clear (left as is otherwise; built always
        // here)
        int32_t target[3];
        arenaCentre(target);
        if (!(actor.animKey.events & kEventStill)) turnToPoint(actor, target, 8);
        if (facingPoint(actor, target) && !noFootDown(actor, boss, 1, 0)) actor.step = uint8_t(RetreatStep::Land);
        break;
    }
    case RetreatStep::Land:
        if (!actor.animEnded()) break;
        actor.subState = uint8_t(BossMove::Walk);
        actor.step = uint8_t(WalkStep::BackToArena);
        actor.stepCounter = 0;
        animMove(actor, boss, 1, -0x400, 0);
        break;
    default: break;
    }
    const uint8_t anim = actor.anim, key = actor.keyIndex;
    if (anim == 0xc && key == 0x1b) sound(0x14d, actor);
    if (anim == 3 && (key == 0xc || key == 0x44)) sound(0x14d, actor);
    if ((anim == 4 || anim == 5) && (key == 0x16 || key == 0x32)) sound(0x14d, actor);
}

// The spit blob's spawn bytes: its attack
struct SpitBlobParams {
    explicit SpitBlobParams(const ProjectileActor& actor) : SpitBlobParams(const_cast<ProjectileActor&>(actor)) {}
    explicit SpitBlobParams(ProjectileActor& actor) : attack(actor.params[0]) {}
    FieldAlias<int8_t, uint8_t> attack;
};
// the four spit blobs (pool 0x40 type 0x1a) from bones 0xd / 0x10 (exe 0x4c96d0 / 0x4c9bc0 inline it)
void spitBlobs(const AnimatedActor& actor, const FightRow& row) {
    for (uint8_t i = 0; i < 4; ++i) {
        const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
        if (slot == 0) continue;
        ProjectileActor& blob = projectileActorAt(slot);
        blob.flags = kActorLive;
        blob.type = kSpitBlobType;
        blob.variant = 0;
        blob.loopFlags |= kActorSweepGroup2;
        int16_t out[3];
        if (!(i & 2)) bonePt(actor, 0xd, -0x20, -0x40, -0x40, out);
        else bonePt(actor, 0x10, 0x20, -0x40, -0x40, out);
        const uint32_t r1 = rngStep(), r2 = rngStep(), r3 = rngStep();
        blob.pos.x.whole = out[0];
        blob.pos.y.whole = out[1];
        blob.pos.z.whole = out[2];
        blob.rotation.x = int16_t(((r2 >> 6) & 0x1ff) - 0x180u);
        blob.rotation.y = int16_t(uint16_t(actor.rotation.y) + ((r1 & 0x1ff) * 2 - 0x200u));
        blob.rotation.z = 0;
        blob.velocity.x = 0;
        blob.velocity.y = 0;
        blob.velocity.z = int16_t(-int(((r3 >> 4) & 0xff) + 0x80));
        blob.acceleration.z = 0;
        blob.acceleration.y = 0;
        blob.acceleration.x = 0;
        SpitBlobParams(projectileActorAt(slot)).attack = int8_t(row.shotAttack);
    }
}
// exe 0x4c96d0: the roar -> four spit blobs
void roar(AnimatedActor& actor, BossFields& boss) {
    const FightRow row = fightRow(actor, stateAddress(TableId::St45BossRoarRows), actor.spawnByte == kFightNoDebris || secondFight());
    switch (RoarStep(actor.step)) {
    case RoarStep::Start:
        ask(actor, boss, 0x21);
        ++actor.step;
        boss.timer = 0x1e;
        break;
    case RoarStep::Wind:
        if (!actor.animEnded()) break;
        if (boss.timer == 0) {
            ask(actor, boss, 0x22);
            actor.stepCounter = 0;
            ++actor.step;
            boss.timer = 0xa;
        } else {
            boss.timer -= 1u;
        }
        break;
    case RoarStep::Spit:
        if (!actor.animEnded()) break;
        if (actor.stepCounter == 0) {
            sound(0x14b, actor);
            spitBlobs(actor, row);
            ++actor.stepCounter;
        }
        if (boss.timer != 0) {
            boss.timer -= 1u;
            break;
        }
        ask(actor, boss, 0x23);
        ++actor.step;
        break;
    case RoarStep::End:
        if (!actor.animEnded()) break;
        ask(actor, boss, 0);
        actor.subState = uint8_t(BossMove::Walk);
        actor.step = uint8_t(WalkStep::Stalk);
        actor.stepCounter = 0;
        break;
    default: break;
    }
    headRelax(boss);
}

// exe 0x4c9a00: the quake -- a camera shake, 15 debris drops around the player every 5 ticks, then back to the walk
void quake(AnimatedActor& actor, BossFields& boss) {
    switch (QuakeStep(actor.step)) {
    case QuakeStep::Shake:
        if (actor.animKey.events & kEventLanding) {
            cameraShake(false, 0x1000, 0x80); // exe 0x40ea80
            sound(0x20d, actor);
        }
        if (actor.animEnded()) {
            boss.timer = 5;
            boss.counter = 0xf;
            ++actor.step;
        }
        break;
    case QuakeStep::Debris:
        if (boss.counter == 0) {
            cameraShake(true, 0, 0);
            ask(actor, boss, 0x25);
            ++actor.step;
            break;
        }
        if (boss.timer != 0) {
            boss.timer -= 1u;
            break;
        }
        if (const uint32_t slot = allocActor60(kActorStageClass); slot != 0) { // exe 0x5e12f0
            StaticActor& debris = staticActorAt(slot);
            debris.flags = kActorLive;
            debris.loopFlags |= kActorSweepGroup2;
            debris.type = kDebrisType;
            debris.subtype = 1;
            debris.variant = 0;
            debris.spawnByte = 0;
            debris.rotation.z = 0;
            debris.rotation.x = 0;
            debris.rotation.y = 0x800;
            uint32_t r = rngStep();
            debris.pos.y.whole = int16_t(0xf800);
            debris.pos.x.whole = int16_t((r & 0x3ff) + uint16_t(player().pos.x.whole) - 0x1ffu);
            r = rngStep();
            debris.pos.z.whole = int16_t((r & 0x3ff) + uint16_t(player().pos.z.whole) - 0x1ffu);
        }
        boss.timer = 5;
        boss.counter -= 1u;
        break;
    case QuakeStep::End:
        if (!actor.animEnded()) break;
        ask(actor, boss, 0);
        actor.subState = uint8_t(BossMove::Walk);
        actor.step = uint8_t(WalkStep::Stalk);
        actor.stepCounter = 0;
        break;
    default: break;
    }
    headRelax(boss);
}

// the blobs out (live pool-0x40 type 0x1a) and, every 64 ticks below 8, a volley
void blobVolley(const AnimatedActor& actor, BossFields& boss, const FightRow& row) {
    boss.blobs = 0;
    for (size_t i = 0; i < projectilePool().size(); ++i) {
        const ProjectileActor& shot = projectilePool()[i];
        if (shot.flags != 0 && shot.type == kSpitBlobType) boss.blobs += 1u;
    }
    if ((frameCounter() & 0x3f) || int8_t(boss.blobs) >= 8) return;
    sound(0x14b, actor);
    spitBlobs(actor, row);
}
// exe 0x4c9bc0: the second fight's walk (as the walk with the box TableId::St45BossSecondArenaBox, faster steps, blob volleys)
void secondWalk(AnimatedActor& actor, BossFields& boss) {
    const FightRow row = fightRow(actor, stateAddress(TableId::St45BossSecondWalkRows), actor.spawnByte == kFightNoDebris || secondFight());
    int32_t target[3] = {};
    const uint8_t step = actor.step;
    auto clearDecode = [&]() { boss.status &= uint8_t(~kDecodeOnly); };
    auto noDistance = [&]() {
        boss.stepDistance = 0;
        clearDecode();
    };
    auto turnStep = [&]() { // the turn step with the current animation
        actor.step = uint8_t(SecondWalkStep::TurnStep);
        actor.stepCounter = 0;
        setUpStep(boss, uint8_t(SecondWalkStep::Decide), actor.anim);
        boss.stepDistance = 0;
        boss.stepKey = 0;
        clearDecode();
    };
    auto stepAside = [&]() {
        actor.step = uint8_t(SecondWalkStep::Step);
        actor.stepCounter = 0;
        setUpStep(boss, uint8_t(SecondWalkStep::Turn), 0x2b);
        boss.stepKey = 0;
        noDistance();
    };
    auto turnInPlace = [&]() {
        if (!actor.animEnded()) return;
        actor.step = uint8_t(SecondWalkStep::Turn);
        actor.stepCounter = 0;
        ask(actor, boss, 0x2b);
        clearDecode();
    };
    auto advance = [&]() {
        if (noFootDown(actor, boss, 6, 0)) return;
        actor.step = uint8_t(SecondWalkStep::TurnStep);
        actor.stepCounter = 1;
        setUpStep(boss, uint8_t(SecondWalkStep::Approach), 2);
        boss.stepKey = 0xf;
        noDistance();
    };
    auto endStep = [&]() {
        if (!actor.animEnded()) return;
        actor.step = boss.nextStep;
        if (boss.stepDistance != 0) {
            animMove(actor, boss, boss.stepAnim, t16(-int(int8_t(boss.stepDistance)) * 16), boss.stepKey);
            return;
        }
        if (int(int8_t(boss.stepAnim)) != int(actor.anim)) ask(actor, boss, boss.stepAnim, boss.stepKey);
    };
    switch (SecondWalkStep(step)) {
    case SecondWalkStep::Approach: {
        if (actor.animKey.events & kEventPause) {
            boss.timer = 4;
            boss.status |= kDecodeOnly;
            actor.step = uint8_t(step + 1u);
            break;
        }
        bool volley = true;
        if (playerIn(stateAddress(TableId::St45BossSecondArenaBox))) {
            if (!(actor.animKey.events & kEventStill)) {
                if (playerDistance(actor) < 0xc00) {
                    const bool f = facingPlayer(actor);
                    if (!noFootDown(actor, boss, 5, 0)) {
                        if (f) turnStep();
                        else stepAside();
                        break;
                    }
                }
                turnToPlayer(actor, 8);
                integrateVelocity(actor, 0, 0, -0x2a4);
            }
        } else {
            arenaPoint(target);
            if (!(actor.animKey.events & kEventStill)) {
                if (playerDistance(actor) < 0xc00) {
                    const bool f = facingPlayer(actor);
                    if (!noFootDown(actor, boss, 5, 0)) {
                        if (f) turnStep();
                        else stepAside();
                        break;
                    }
                }
                if (pointDistance(actor, target) < 0x300) {
                    if (!noFootDown(actor, boss, 5, 0)) {
                        actor.step = uint8_t(SecondWalkStep::TurnStep);
                        setUpStep(boss, uint8_t(SecondWalkStep::Charge), 0xb);
                        noDistance();
                        volley = false;
                    }
                } else {
                    turnToPoint(actor, target, 0x10);
                    integrateVelocity(actor, 0, 0, -0x2a4);
                }
            }
        }
        if (volley) blobVolley(actor, boss, row);
        break;
    }
    case SecondWalkStep::Pause:
    case SecondWalkStep::PauseAgain:
        if (boss.timer == 0) {
            actor.step = uint8_t(step - 1u);
            clearDecode();
        } else {
            boss.timer -= 1u;
        }
        break;
    case SecondWalkStep::Decide:
        if (uint32_t(distance3(player().pos, actor.pos)) < 0xc00) {
            if (facingPlayer(actor)) {
                if (!noFootDown(actor, boss, -1, 0)) pickAttack(actor, boss);
            } else {
                turnInPlace();
            }
        } else if (facingPlayer(actor)) {
            actor.step = uint8_t(SecondWalkStep::Stalk);
        } else {
            turnInPlace();
        }
        break;
    case SecondWalkStep::TurnStep: {
        const uint8_t f = actor.animKey.events;
        if (f & kEventStill) actor.stepCounter = 1;
        if (f & kEventMoving) actor.stepCounter = 0;
        if (actor.stepCounter == 0) {
            if (playerIn(stateAddress(TableId::St45BossSecondArenaBox))) {
                turnToPlayer(actor, 8);
            } else {
                arenaPoint(target);
                turnToPoint(actor, target, 0x10);
            }
            integrateVelocity(actor, 0, 0, -0x2a4);
        }
        endStep();
        break;
    }
    case SecondWalkStep::Step: endStep(); break;
    case SecondWalkStep::Stalk:
        mouthShot(actor, boss);
        if (playerIn(stateAddress(TableId::St45BossSecondArenaBox))) {
            if (facingPlayer(actor)) {
                if (playerDistance(actor) < 0xc00) {
                    if (actor.anim == 0x2c) {
                        if (actor.animEnded()) {
                            pickAttack(actor, boss);
                            break;
                        }
                    } else if (!noFootDown(actor, boss, -1, 0)) {
                        pickAttack(actor, boss);
                        break;
                    }
                }
                advance();
            } else if (actor.animEnded()) {
                actor.step = uint8_t(SecondWalkStep::Turn);
                actor.stepCounter = 0;
                ask(actor, boss, 0x2b);
                clearDecode();
            } else {
                advance();
            }
            break;
        }
        if (facingPlayer(actor) && playerDistance(actor) < 0xc00 && !noFootDown(actor, boss, -1, 0)) {
            pickAttack(actor, boss);
            break;
        }
        arenaPoint(target);
        if (pointDistance(actor, target) > 0x600) {
            advance();
            break;
        }
        if (playerDistance(actor) < 0xc00 || !facingPlayer(actor)) {
            turnInPlace();
            break;
        }
        ask(actor, boss, 0x11);
        enterMove(actor, BossMove::Breath);
        break;
    case SecondWalkStep::StepBack:
        if (actor.animEnded()) {
            actor.step = uint8_t(SecondWalkStep::Stalk);
            clearDecode();
        } else {
            integrateVelocity(actor, 0, 0, 0xa2);
        }
        break;
    case SecondWalkStep::Turn:
        if (actor.animEnded()) {
            actor.step = uint8_t(SecondWalkStep::Charge);
            actor.stepCounter = 0;
            ask(actor, boss, turnSide(actor) ? 0x2d : 0x2e);
        } else {
            integrateVelocity(actor, 0, 0, -0xa2);
        }
        break;
    case SecondWalkStep::Charge:
        if (actor.keyIndex == 0x1a) {
            const uint8_t f = boss.status;
            if (f & kDecodeOnly) {
                if (boss.timer != 0) boss.timer -= 1u;
                else boss.status = uint8_t(f & ~kDecodeOnly);
            } else {
                boss.timer = 4;
                boss.status = uint8_t(f | kDecodeOnly);
            }
        } else {
            if (!(actor.animKey.events & kEventStill)) turnToPlayer(actor, 0x14);
            if (facingPlayer(actor)) {
                const bool nearPlayer = playerDistance(actor) < 0xc00;
                if (actor.animEnded()) {
                    if (nearPlayer) {
                        actor.step = uint8_t(SecondWalkStep::Lunge);
                        ask(actor, boss, 0x2c);
                        break;
                    }
                    actor.step = uint8_t(SecondWalkStep::StepBack);
                    clearDecode();
                    ask(actor, boss, 0x2c);
                }
            }
        }
        blobVolley(actor, boss, row);
        break;
    case SecondWalkStep::Lunge:
        if (actor.animEnded()) pickAttack(actor, boss);
        else integrateVelocity(actor, 0, 0, 0xa2);
        break;
    default: break;
    }
    keepInBox(actor, boss, uint8_t(SecondWalkStep::Lunge), stateAddress(TableId::St45BossSecondStayBox));
    if (actor.animKey.events & kEventLanding) {
        cameraShakeAt(0x2000, 0x400, 0x100, 0x1000, actor.pos);
        sound(0x14d, actor);
    }
    headFollow(actor, boss);
}

// exe 0x4ca790: the second fight's stomp (the stomp without the recoil), back to the second walk
void secondStomp(AnimatedActor& actor, BossFields& boss) {
    switch (StompStep(actor.step)) {
    case StompStep::Land:
        if (actor.animEnded()) {
            boss.timer = 0x1e;
            ++actor.step;
        } else if (actor.animKey.events & kEventLanding) {
            cameraShakeAt(0x6000, 0x400, 0x100, 0x1000, actor.pos);
            spawnWave(actor);
            sound(0x149, actor);
        }
        break;
    case StompStep::Rain:
        if (boss.timer == 0) {
            ask(actor, boss, 0x1a);
            ++actor.step;
            break;
        }
        boss.timer -= 1u;
        if (actor.spawnByte != kFightNoDebris && !(frameCounter() & 3)) debrisRain(actor);
        break;
    case StompStep::Recover:
        if (!actor.animEnded()) break;
        actor.subState = uint8_t(BossMove::SecondWalk);
        actor.step = uint8_t(playerIn(stateAddress(TableId::St45BossSecondRetreatBox)) ? SecondWalkStep::Decide : SecondWalkStep::Stalk);
        actor.stepCounter = 0;
        ask(actor, boss, 0xb);
        break;
    default: break;
    }
    headRelax(boss);
}
// exe 0x4ca9d0: the bite (sound 0x20f when the sound timer runs out), back to the second walk
void bite(AnimatedActor& actor, BossFields& boss) {
    if (boss.soundTimer == 0) {
        sound(0x20f, actor);
        boss.soundTimer -= 1u;
    } else if (int16_t(boss.soundTimer.get()) > 0) {
        boss.soundTimer -= 1u;
    }
    switch (BiteStep(actor.step)) {
    case BiteStep::Bite:
        if (!actor.animEnded()) break;
        ask(actor, boss, actor.anim == 0x15 ? 0x16 : 0x18);
        actor.step = uint8_t(BiteStep::Recover);
        break;
    case BiteStep::Recover:
        if (!actor.animEnded()) break;
        ask(actor, boss, 0xb);
        actor.subState = uint8_t(BossMove::SecondWalk);
        actor.step = uint8_t(SecondWalkStep::Decide);
        actor.stepCounter = 0;
        break;
    default: break;
    }
    headRelax(boss);
}
// exe 0x4caa70: the breath -- breath puffs (pool 0x40 type 5) from bone 3 while facing the player
void breath(AnimatedActor& actor, BossFields& boss) {
    switch (BreathStep(actor.step)) {
    case BreathStep::Start:
        if (!actor.animEnded()) return;
        ask(actor, boss, 0x12);
        actor.stepCounter = 0;
        ++actor.step;
        boss.counter = 0xa;
        return;
    case BreathStep::Breathe: {
        const uint8_t n = boss.counter;
        if (actor.animEnded()) {
            if (n != 0) {
                boss.counter = uint8_t(n - 1u);
            } else {
                ask(actor, boss, 0x13);
                ++actor.step;
            }
            return;
        }
        if (int8_t(n) < 6 && !facingPlayer(actor)) boss.counter = 0;
        headFollow(actor, boss);
        if (actor.animKey.events & kEventMoving) actor.stepCounter = 1;
        if (actor.stepCounter == 0) return;
        const DataArray<int8_t> spread(stateAddress(TableId::St45BossBreathSpread));
        const int dp = spread[rngStep() & 3];
        const int dy = spread[rngStep() & 3];
        const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
        if (slot == 0) return;
        ProjectileActor& puff = projectileActorAt(slot);
        BreathPuffParams params(projectileActorAt(slot));
        puff.flags = kActorLive;
        puff.type = kBreathPuffType;
        puff.variant = 0;
        puff.loopFlags |= kActorSweepGroup2;
        int16_t out[3];
        bonePt(actor, 3, 0, 0, -0x80, out);
        puff.pos.x.whole = out[0];
        puff.pos.y.whole = out[1];
        puff.pos.z.whole = out[2];
        puff.rotation.x = int16_t(uint16_t(boss.headPitch) + dp);
        puff.rotation.y = int16_t(uint16_t(boss.headTurn) + uint16_t(actor.rotation.y) + dy);
        puff.rotation.z = actor.rotation.z;
        params.damage = fightRow(actor, stateAddress(TableId::St45BossBreathRows), actor.spawnByte != 0 || secondFight()).breathAttack;
        params.life = 0x18;
        puff.velocity.z = int16_t(0xf880);
        params.frame = uint8_t((frameCounter() >> 1) & 1);
        params.spin = 0x10;
        params.size = 0x60;
        return;
    }
    case BreathStep::Relax: {
        if (actor.animEnded()) {
            ask(actor, boss, 0xb);
            ++actor.step;
            return;
        }
        const int16_t t = boss.headTurn;
        if (t < 0) {
            const int16_t n = t16(t + 8);
            boss.headTurn = n >= 0 ? int16_t(0) : n;
        } else {
            const int16_t n = t16(t - 8);
            boss.headTurn = n <= 0 ? int16_t(0) : n;
        }
        return;
    }
    case BreathStep::End:
        actor.subState = uint8_t(BossMove::SecondWalk);
        actor.step = uint8_t(SecondWalkStep::Decide);
        actor.stepCounter = 0;
        boss.headTurn = 0;
        return;
    default: return;
    }
}

// ---- the boss's states [0x894f2c] ----------------------------------------------------------------------------------

// The arm's hit-box holder (a pool-0xe0 slot, type 0): the boss
struct ArmHolderParams {
    explicit ArmHolderParams(const ControllerActor& actor) : ArmHolderParams(const_cast<ControllerActor&>(actor)) {}
    explicit ArmHolderParams(ControllerActor& actor) : boss(actor.params, 0) {}
    BytesField<uint32_t, 4> boss; // the boss's identity word
};
void bossBind(AnimatedActor& actor, BossFields& boss) { // exe 0x4c79b0
    if (!bindActorModel(actor, actor.spawnByte)) { // exe 0x5e0bc0(actor, &+7)
        freeActor(actor);                          // exe 0x5e1490
        return;
    }
    actor.flags |= kActorScenery;
    actor.scale.x = 0x480;
    actor.scale.y = 0x480;
    actor.scale.z = 0x480;
    actor.collisionBox.pointAt(TableId::St45BossBox);
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
    boss.armBox.result = 0;
    boss.bodyBox.result = 0;
    boss.status = 0;
    boss.counter = 0;
    boss.mouthTick = 0;
    boss.headTurn = 0;
    boss.hurtCooldown = 0;
    actor.state = uint8_t(BossState::Run);
    if (actor.spawnByte == 0 && !secondFight()) {
        const FightRow row = fightRow(actor, stateAddress(TableId::St45BossHitPointRows), false);
        actor.subState = uint8_t(BossMove::Walk);
        ask(actor, boss, 1);
        actor.prevAnim = 0xff;
        const uint16_t hp = uint16_t(row.hitPoints << 5);
        actor.maxHitPoints = int16_t(hp);
        actor.hitPoints = int16_t(hp);
    } else {
        const FightRow row = fightRow(actor, stateAddress(TableId::St45BossHitPointRows), true);
        actor.subState = uint8_t(BossMove::SecondWalk);
        ask(actor, boss, 2);
        actor.prevAnim = 0xff;
        const uint16_t hp = uint16_t(row.hitPoints << 5);
        actor.maxHitPoints = int16_t(hp);
        actor.hitPoints = int16_t(hp);
        hudAddBossGauge(actor, 1, 5, 0x2222aa); // exe 0x415d30
    }
    const uint32_t slot = allocActorE0(); // exe 0x5e1440: the arm's hit-box holder
    if (slot == 0) notPorted(kStoryPort, "0x4c79b0: pool 0xe0 full (the exe writes through null)");
    pointAtWord(boss.armHolder, slot);
    ControllerActor& holder = controllerActorAt(slot);
    holder.type = 0;
    holder.loopFlags |= kActorLateLoop;
    ArmHolderParams(holder).boss = slotIdentityWord(actor);
}
// the hit entries of a body point
void bodyEntry(const AnimatedActor& actor, BossFields& boss, uint32_t owner, uint32_t shape, uint32_t attack, int bone,
               int x, int y, int z, int16_t out[3]) {
    bonePt(actor, uint32_t(bone), x, y, z, out);
    hitListSubmit(out, boss.bodyBox, owner | shape, attack); // exe 0x5e4120
}
void bossRun(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) { // exe 0x4c7b30
    if (boss.hurtCooldown) boss.hurtCooldown -= 1u;
    const FightRow row = fightRow(actor, stateAddress(TableId::St45BossRunRows), actor.spawnByte != 0 || secondFight());
    if (boss.armBox.result & kHitAttackByPlayer) {
        actor.tint = kTintFlash;
        bool hurt = true;
        if (BossMove(actor.subState) != BossMove::Retreat) {
            if (boss.hurtCooldown == 0) {
                const int dead = hitPointsAdd(actor, -int(boss.armBox.result & kHitDamageMask)); // exe 0x5e52f0
                boss.hurtCooldown = 4;
                if (dead) {
                    if (actor.spawnByte != 0) {
                        countDefeat();
                        actor.state = uint8_t(BossState::Death);
                        player().immunity = kImmuneHeld;
                    } else if (secondFight()) {
                        setStoryFlag(kFlagSecondFightWon);
                        countDefeat();
                    } else {
                        boss.status |= kFirstFightBeaten;
                    }
                    hurt = false;
                }
            }
            if (hurt && actor.spawnByte == 0 && secondFight() &&
                int(actor.hitPoints) < (int(actor.maxHitPoints) / 3) * 2 && !storyFlag(kFlagTwoThirdsHurt))
                setStoryFlag(kFlagTwoThirdsHurt);
        }
        sound(0x8d, actor);
    }
    switch (BossMove(actor.subState)) { // [0x894f44]
    case BossMove::Walk: walk(actor, boss); break;
    case BossMove::Stomp: stomp(actor, boss); break;
    case BossMove::Retreat: retreat(actor, boss); break;
    case BossMove::Roar: roar(actor, boss); break;
    case BossMove::Quake: quake(actor, boss); break;
    case BossMove::SecondWalk: secondWalk(actor, boss); break;
    case BossMove::SecondStomp: secondStomp(actor, boss); break;
    case BossMove::Bite: bite(actor, boss); break;
    case BossMove::Breath: breath(actor, boss); break;
    default: notPortedAt(kStoryPort, "0x4c7b30 sub-state ([0x894f44])", actor.subState);
    }
    // the hit entries: the body's (its box, deflecting), the arm's (its box through the holder's slot, solid),
    // capsules along the legs; the feet probed (exe 0x4cb8e0)
    const uint32_t attack = uint32_t(row.attack) | kHitAttackDeflects;
    const uint32_t owner = hitOwner(actor.slot) | hitHeightLevel(row.hitHeight);
    const uint32_t body = hitGroup(kHitGroupBodies) | kHitNoLockOn;
    const uint32_t capsule = owner | body | hitCapsule(1);
    boss.armBox.previousX = actor.prevPos.x.whole;
    boss.armBox.previousZ = actor.prevPos.z.whole;
    boss.armBox.result = 0;
    boss.bodyBox.previousX = actor.prevPos.x.whole;
    boss.bodyBox.previousZ = actor.prevPos.z.whole;
    boss.bodyBox.result = 0;
    int16_t p[3], q[3];
    bodyEntry(actor, boss, owner, body | hitRadius(0x12c), attack, 0, 0, 0, 0, p);
    bodyEntry(actor, boss, owner, body | hitRadius(0x94), attack, 1, 0, -0x30, 0, p);
    bodyEntry(actor, boss, owner, body | hitRadius(0x15c), attack, 2, 0, -0x60, 0, p);
    bodyEntry(actor, boss, owner, body | hitRadius(0x15c), attack, 2, 0, -0x90, 0, p);
    bonePt(actor, 3, 0, 0x10, -0x40, p);
    {
        const uint32_t armOwner = hitOwner(boss.armHolder->slot) | hitHeightLevel(row.hitHeight);
        hitListSubmit(p, boss.armBox, armOwner | hitGroup(kHitGroupBodies) | hitRadius(0xc8),
                      uint32_t(row.attack) | kHitAttackSolid);
    }
    bodyEntry(actor, boss, owner, body | hitRadius(0xc8), attack, 8, 0, 0x70, 0, p);
    footCheck(env, actor, p);
    bodyEntry(actor, boss, owner, body | hitRadius(0xc8), attack, 0xb, 0, 0x70, 0, p);
    footCheck(env, actor, p);
    bodyEntry(actor, boss, owner, body | hitRadius(0x94), attack, 0xe, 0, 0x40, 0, p);
    footCheck(env, actor, p);
    bodyEntry(actor, boss, owner, body | hitRadius(0x94), attack, 0xe, 0, 0x40, -0x60, p);
    footCheck(env, actor, p);
    bodyEntry(actor, boss, owner, body | hitRadius(0x94), attack, 0x11, 0, 0x40, 0, p);
    footCheck(env, actor, p);
    bodyEntry(actor, boss, owner, body | hitRadius(0x94), attack, 0x11, 0, 0x40, -0x60, p);
    footCheck(env, actor, p);
    // the legs: bones 6-7-8, 9-10-11, 12-13-14, 15-16-17 (exe 0x5e4190)
    for (uint32_t leg = 6; leg <= 0xf; leg += 3) {
        bonePt(actor, leg, 0, 0, 0, p);
        bonePt(actor, leg + 1, 0, 0, 0, q);
        hitListSubmitCapsule(p, 0x14, q, 0x14, boss.bodyBox, capsule, attack);
        bonePt(actor, leg + 2, 0, 0, 0, p);
        hitListSubmitCapsule(q, 0x14, p, 0x14, boss.bodyBox, capsule, attack);
    }
}
// The death's sparks' spawn byte: the size's low byte (the exe's byte store into the spark's size)
struct DeathSparkParams {
    explicit DeathSparkParams(const EffectActor& actor) : DeathSparkParams(const_cast<EffectActor&>(actor)) {}
    explicit DeathSparkParams(EffectActor& actor) : size(actor.params[0]) {}
    uint8_t& size;
};
void bossDeath(const AnimatedActor& actor) { // exe 0x4cad80: hit sparks every other tick; the room opens
    if (frameCounter() & 1) {
        const uint32_t slot = allocActorA0(0); // exe 0x5e13b0: a hit spark (global effect type 0)
        if (slot) {
            EffectActor& spark = effectActorAt(slot);
            spark.flags = kActorLive;
            spark.type = typeByte(GlobalEffectType::HitSpark);
            spark.subtype = 0;
            spark.variant = 0;
            spark.runFlags |= kActorWarpKept;
            int16_t out[3];
            uint32_t r = rngStep();
            bonePt(actor, DataArray<uint8_t>(stateAddress(TableId::St45BossDeathBones))[r & 7], 0, 0, 0, out);
            r = rngStep();
            spark.pos.x = int16_t(((((r & 0xff) * 0x180u) - 0xc000u) >> 8) + uint16_t(out[0]));
            r = rngStep();
            spark.pos.y = int16_t(((((r & 0xff) * 0x180u) - 0xc000u) >> 8) + uint16_t(out[1]));
            r = rngStep();
            spark.pos.z = int16_t(((((r & 0xff) * 0x180u) - 0xc000u) >> 8) + uint16_t(out[2]));
            r = rngStep();
            DeathSparkParams(spark).size = uint8_t(((r & 0x7f) + 0x40) & 0xff);
        }
    }
    setStoryFlag(kFlagRoomOpens);
}
void bossTick(const PlayerEnv& env, AnimatedActor& actor) { // exe 0x4c78e0
    BossFields& boss = classState<BossFields>(actor);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (BossState(actor.state)) {
    case BossState::Bind: bossBind(actor, boss); break;
    case BossState::Run: bossRun(env, actor, boss); break;
    case BossState::Free: freeActor(actor); break; // exe 0x468fa0
    case BossState::Death: bossDeath(actor); break;
    default: notPortedAt(kStoryPort, "0x4c78e0 state", actor.state);
    }
    if (actor.anim == actor.prevAnim) {
        if (!(boss.status & kDecodeOnly)) animStep(actor); // exe 0x5e1ea0
        else animDecodeCurrent(actor);                     // exe 0x5e1f30
    } else {
        animSet(actor, actor.anim, boss.startKey); // exe 0x5e1c50
    }
    // the head (part 3's decoded angles) turned by its pitch and turn
    LaneVec4 head = decodedFrame().entries[kHeadPartEntry];
    head.x = int16_t(uint16_t(boss.headPitch) + uint16_t(head.x));
    head.y = int16_t(uint16_t(boss.headTurn) + uint16_t(head.y));
    animPose(actor); // exe 0x5e2d90
}

// ---- the children (pool 0x40) --------------------------------------------------------------------------------------

void savePose40(ProjectileActor& actor) { // the copies at the top of the pool-0x40 classes
    actor.savePreviousPose();
    actor.wasAirborne = actor.airborne;
}

// -- the breath puff (exe 0x5c4820, ST45 pool 0x40 type 5): flies along its pitch / yaw (its rise slowing by 6),
// grows, a hit entry on its frames while alive, then fades; drawn as a 0x2c billboard (tpage word 0x7cd2 / 0x7dd1 by
// the variant bit kBreathWater)
enum class PuffState : uint8_t { Start = 0, Fly = 1, Fade = 2 };
constexpr uint8_t kBreathWater = 0x80;
void breathPuff(const PlayerEnv& env, ProjectileActor& actor) {
    PuffFields& puff = classState<PuffFields>(actor);
    BreathPuffParams params(actor);
    savePose40(actor);
    switch (PuffState(actor.state)) {
    case PuffState::Start:
        puff.clearBlockBytes(); // exe 0x40dc40
        puff.life = uint16_t(int16_t(int8_t(params.life)));
        puff.size = params.size;
        puff.alpha = 0x80;
        actor.velocity.y = 0;
        actor.collisionBox.pointAt(TableId::St45BossPuffBox);
        if (!(actor.variant & 0x7f)) sound(0xb3, actor);
        actor.enterState(uint8_t(PuffState::Fly));
        [[fallthrough]];
    case PuffState::Fly: {
        moveAlongRotation(actor.pos, actor.rotation, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e37b0
        actor.velocity.y -= 6u;
        puff.size += uint16_t(params.spin);
        puff.life -= 1u;
        if (puff.life == 0) actor.enterState(uint8_t(PuffState::Fade));
        if (int32_t(actorTestMove(env, actor)) < 0) { // exe 0x40f570
            actor.velocity.z = 0;
            actor.enterState(uint8_t(PuffState::Fade));
        }
        if (params.frame != 0) {
            const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
            puff.hitBox.previousX = actor.prevPos.x.whole;
            puff.hitBox.previousZ = actor.prevPos.z.whole;
            const uint32_t attack = (((actor.variant & kBreathWater) ? kHitAttackWater : kHitAttackBurning) |
                                     uint32_t(int32_t(int16_t(params.damage)))) |
                                    kHitAttackStrong;
            // the radius (size - 0x20) / 4 with the shots' group, then the owner
            const uint32_t shape =
                uint32_t((int32_t(uint32_t(int32_t(puff.size) - 0x20) | (hitGroup(kShotGroup) << kHitRadiusScale))) >>
                         kHitRadiusScale) |
                hitOwner(actor.slot);
            hitListSubmit(p, puff.hitBox, shape, attack); // exe 0x5e4120
        }
        break;
    }
    case PuffState::Fade:
        integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
        actor.velocity.y -= 6u;
        puff.size += uint16_t(params.spin);
        puff.alpha -= 0x10u;
        if (puff.alpha <= 0) {
            freeActor(actor); // exe 0x5e1500
            return;
        }
        break;
    default: break;
    }
    // the billboard (opcode 0x2c, 10 words; word 0 not written)
    const uint32_t e = uint8_t(puff.alpha);
    const uint32_t tp = ((actor.variant & kBreathWater) ? 0xffu : 0u) + 0x7cd2u;
    const int32_t sz = puff.frame;
    const uint32_t w1 = (((((0x2e00u | e) << 8) | e) << 8) | e);
    dc::emit({0, w1, (uint32_t(int32_t(actor.pos.y.whole)) << 16) | uint16_t(actor.pos.x.whole),
              (tp << 16) | uint32_t(sz) | 0x4000, uint16_t(actor.pos.z.whole), uint32_t(sz + 0x1f) | 0x2f4000,
              uint32_t(int32_t(puff.size)), uint32_t(sz) | 0x5f00, 0, uint32_t(sz + 0x1f) | 0x5f00},
             actor.at, false);
    puff.frame = int16_t((uint8_t(puff.frame) + 0x20u) & 0xff);
}

// the player's position 0x80 up (the exe's local copy)
void playerChest(int32_t out[3]) {
    const Player& p = player();
    out[0] = p.pos.x;
    out[1] = int32_t(uint32_t(p.pos.y) + 0xff800000u);
    out[2] = p.pos.z;
}

// -- the spit blob (exe 0x5ca060, ST45 pool 0x40 type 0x1a): wobbles, homes on the player's chest while its life is
// below 0x172, a hit entry, bursts on a wall / a hit / its timer
enum class BlobState : uint8_t { Start = 0, Fly = 1, Burst = 2 };
constexpr uint8_t kBlobHoming = 0x01;
constexpr uint32_t kBlobGroup = 5; // the hit-list group of the blobs
// exe 0x5ca3d0: two 0x2c billboards (the glow 0x7dd1, its size growing by 0x20 a tick; the core 0x7dd0)
void blobDraw(const ProjectileActor& actor, BlobFields& blob) {
    const uint32_t e = blob.alpha;
    const uint32_t w1 = (e << 16 | e << 8 | e) | 0x2e000000u;
    const int32_t sz = blob.frame;
    const uint32_t w2 = (uint32_t(int32_t(actor.pos.y.whole)) << 16) | uint16_t(actor.pos.x.whole);
    dc::emit({0, w1, w2, uint32_t(sz) | 0x7dd14000u, uint16_t(actor.pos.z.whole), uint32_t(sz + 0x1f) | 0x2f4000u,
              uint32_t(int32_t(blob.glow)), uint32_t(sz) | 0x5f00u, 0, uint32_t(sz + 0x1f) | 0x5f00u},
             actor.at, false);
    blob.frame = int16_t((uint8_t(blob.frame) + 0x20u) & 0xff);
    dc::emit({0, w1, (uint32_t(int32_t(actor.pos.y.whole)) << 16) | uint16_t(actor.pos.x.whole), 0x7dd02020u,
              uint16_t(actor.pos.z.whole), 0x2f203fu, uint32_t(int32_t(blob.glow) / 2), 0x3f20u, 0, 0x3f3fu},
             actor.at, false);
}
void spitBlob(const PlayerEnv& env, ProjectileActor& actor) {
    BlobFields& blob = classState<BlobFields>(actor);
    switch (BlobState(actor.state)) {
    case BlobState::Start: { // exe 0x5ca0a0
        int32_t chest[3];
        playerChest(chest);
        blob.distance = uint32_t(planarDistance(actor.pos, chest));
        blob.hitBox.result = 0;
        blob.life = 0x190;
        blob.burstTicks = 0;
        blob.speed = uint16_t(actor.velocity.z);
        blob.burstStep = 0;
        blob.alpha = 0x80;
        blob.glow = 0x80;
        actor.collisionBox.pointAt(TableId::St45BossBlobBox);
        blob.frame = 0;
        blobDraw(actor, blob);
        actor.state = uint8_t(BlobState::Fly);
        blob.wobbleSpeed = int8_t(rngStep() & 0xbf);
        break;
    }
    case BlobState::Fly: { // exe 0x5ca160
        int32_t chest[3];
        playerChest(chest);
        actor.savePreviousPose();
        const uint16_t phase = uint16_t(int(blob.wobbleSpeed) + int16_t(blob.wobblePhase)) & 0xfff;
        blob.wobblePhase = phase;
        const int16_t bob = int16_t(fixedSin(phase) >> 6);    // exe 0x40b920
        moveAlongYaw(actor.pos, actor.rotation.y, 0, bob, 0); // exe 0x5e36f0
        blob.distance = uint32_t(planarDistance(actor.pos, chest));
        blob.targetYaw = int16_t(angleOf(chest[0], chest[2], actor.pos.x, actor.pos.z));
        int16_t from[3], to[3];
        wholes(chest, from);
        wholes(actor.pos, to);
        blob.targetPitch = int16_t(0x800 - pitchOf(from, to));
        if (int16_t(blob.life) < 0x172) blob.blobFlags |= kBlobHoming;
        else blob.blobFlags &= uint8_t(~kBlobHoming);
        if (blob.blobFlags & kBlobHoming) { // exe 0x5ca390: steer toward the chest
            turnToward(actor.rotation.y, int(blob.targetYaw), 0x10);
            if (int(blob.targetPitch) - int(actor.rotation.x) > 0) actor.rotation.x += 0x10u;
            else actor.rotation.x -= 0x10u;
        }
        moveAlongRotation(actor.pos, actor.rotation, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e37b0
        const uint16_t t = blob.life;
        if (t == 0) {
            actor.state = uint8_t(BlobState::Burst);
        } else {
            if (int16_t(t) < 0x64) blob.glow -= 1u;
            blob.life = uint16_t(t - 1u);
        }
        blobDraw(actor, blob);
        if (playerShieldBlocks(actor, 0x60)) { // exe 0x5737b0
            actor.state = uint8_t(BlobState::Burst);
            break;
        }
        if (blob.hitBox.result & (kHitAttackBump | kHitAttackByPlayer)) actor.state = uint8_t(BlobState::Burst);
        if (int32_t(actorTestMove(env, actor)) < 0) actor.state = uint8_t(BlobState::Burst); // exe 0x40f570
        blob.hitBox.result = 0;
        blob.hitBox.previousX = actor.prevPos.x.whole;
        blob.hitBox.previousZ = actor.prevPos.z.whole;
        const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
        hitListSubmit(p, blob.hitBox, hitOwner(actor.slot) | hitGroup(kBlobGroup) | kHitNoLockOn | hitRadius(0x60),
                      uint32_t(int32_t(SpitBlobParams(actor).attack)) | kHitAttackStrong |
                          hitImmunity(kHitGroupBodies)); // exe 0x5e4120
        break;
    }
    case BlobState::Burst: // exe 0x5ca330
        if (blob.burstTicks == 0) sound(0x89, actor);
        blob.burstTicks += 1u;
        blob.burstStep += 0x20u;
        blobDraw(actor, blob);
        if (int16_t(blob.burstTicks) >= 7) {
            freeActor(actor); // exe 0x5e1500
            return;
        }
        break;
    default: notPortedAt(kStoryPort, "0x5ca060 state", actor.state);
    }
    // exe 0x5f20f0(actor, 0x60, 0x100, 0) between y -= 0x100 / += 0x100: the ground shadow (render output, as the
    // other ports: not drawn)
}

// -- the mouth shot (exe 0x5c62d0, ST45 pool 0x40 type 0xf; variants [0x8d129c]): 0 the shot (0x5c62f0: flies, a hit
// entry, a 16-sprite trail; a wall / a hit -> the splash), 1 the splash (0x5c64a0: 8 falling droplets, 0x3c ticks)
enum class MouthShotState : uint8_t { Start = 0, Fly = 1, Free = 2 };
enum class SplashState : uint8_t { Start = 0, Fall = 1, Free = 2 };
constexpr uint8_t kShotVariant = 0, kSplashVariant = 1;
// The splash's spawn: the shot's roll cleared (exe 0x5c6900)
void shotSplash(ProjectileActor& actor) {
    const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
    if (slot == 0) return;
    ProjectileActor& splash = projectileActorAt(slot);
    splash.flags = kActorLive;
    splash.loopFlags |= kActorSweepGroup2;
    splash.type = kMouthShotType;
    splash.subtype = 0;
    splash.variant = kSplashVariant;
    splash.spawnByte = 0;
    actor.rotation.z = 0;
    splash.rotation.x = 0;
    splash.rotation.y = actor.rotation.y;
    splash.pos = actor.pos;
}
// exe 0x5c65f0: the trail -- 16 0x2c billboards stepping back along the flight
void shotTrail(ProjectileActor& actor, MouthShotFields& shot) {
    const int16_t x = actor.pos.x.whole, y = actor.pos.y.whole, z = actor.pos.z.whole;
    const int back = -(int(actor.velocity.z) >> 2);
    for (int k = 0; k < 0x10; ++k) {
        moveAlongRotation(actor.pos, actor.rotation, actor.velocity.x, actor.velocity.y, back); // exe 0x5e37b0
        const uint32_t size =
            actor.variant == 0 ? uint32_t(0x40 - (((2 * k) * (2 * k)) >> 4)) : uint32_t(0x44 - k * 4);
        uint32_t u = uint32_t((k % 3) << 5) + shot.trailU;
        if ((u & 0xffff) > 0x40) u = 0;
        u &= 0xffff;
        dc::emit({0, 0x2e606060u, (uint32_t(int32_t(actor.pos.y.whole)) << 16) | uint16_t(actor.pos.x.whole),
                  u | 0x7cd24000u, uint16_t(actor.pos.z.whole), (u + 0x1f) | 0x2f4000u, size & 0xffff, u | 0x5f00u, 0,
                  (u + 0x1f) | 0x5f00u},
                 actor.at, false);
    }
    shot.trailU += 0x20u;
    if (shot.trailU > 0x40) shot.trailU = 0;
    actor.pos.x.whole = x;
    actor.pos.y.whole = y;
    actor.pos.z.whole = z;
}
// exe 0x5c6760: the 8 droplets as 0x2c billboards
void splashDrops(ProjectileActor& actor, SplashFields& splash) {
    for (uint32_t i = 0; i < kSplashDrops; ++i) {
        SplashDrop& drop = splash.drops[i];
        const uint8_t al = splash.texelU;
        const uint32_t u = al, u2 = u + 0x1f;
        if (i % 3 == 0) {
            splash.texelU = uint8_t(al + 0x20u);
            if (splash.size > 1 && int(splash.ticks) > int(splash.sizeFloor) + 0x14) splash.size -= 1u;
        }
        actor.pos.x.whole = drop.point.x;
        actor.pos.y.whole = drop.point.y;
        actor.pos.z.whole = drop.point.z;
        if (actor.pos.y.whole < 0) {
            moveAlongYaw(actor.pos, drop.yaw, 0, splash.fall[i], splash.forward[i]); // exe 0x5e36f0
            splash.fall[i] += uint16_t(actor.acceleration.y);
        }
        drop.point.x = actor.pos.x.whole;
        drop.point.y = actor.pos.y.whole;
        drop.point.z = actor.pos.z.whole;
        dc::emit({0, 0x2e606060u, (uint32_t(int32_t(actor.pos.y.whole)) << 16) | uint16_t(actor.pos.x.whole),
                  u | 0x7cd24000u, uint16_t(actor.pos.z.whole), u2 | 0x2f4000u, splash.size, u | 0x5f00u, 0,
                  u2 | 0x5f00u},
                 actor.at, false);
    }
    splash.ticks += 1u;
}
void mouthShotTick(const PlayerEnv& env, ProjectileActor& actor) {
    savePose40(actor);
    if (actor.variant == kShotVariant) {
        MouthShotFields& shot = classState<MouthShotFields>(actor);
        switch (MouthShotState(actor.state)) {
        case MouthShotState::Start: // exe 0x5c6330
            actor.collisionBox.pointAt(TableId::St45BossMouthShotBox);
            shot.word = uint16_t(rngStep() & 0x1f);
            shot.byte = uint8_t((rngStep() & 0x3f) + 0x70);
            shot.trailU = 0;
            shot.hitBox.result = 0;
            shot.hitBox.previousX = actor.pos.x.whole;
            shot.hitBox.previousZ = actor.pos.z.whole;
            ++actor.state;
            sound(0xb3, actor);
            return;
        case MouthShotState::Fly: { // exe 0x5c63d0
            moveAlongRotation(actor.pos, actor.rotation, actor.velocity.x, actor.velocity.y, actor.velocity.z);
            if (int32_t(actorTestMove(env, actor)) < 0 || (shot.hitBox.result & kHitAttackBump)) { // exe 0x40f570
                actor.state = uint8_t(MouthShotState::Free);
                shotSplash(actor);
                return;
            }
            shot.hitBox.previousX = actor.prevPos.x.whole;
            shot.hitBox.previousZ = actor.prevPos.z.whole;
            shot.hitBox.result = 0;
            const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
            hitListSubmit(p, shot.hitBox,
                          hitOwner(actor.slot) | hitGroup(kShotGroup) | kHitContactMotion | hitRadius(0x20),
                          uint32_t(MouthShotParams(actor).attack) | kHitAttackStrong);
            shotTrail(actor, shot);
            return;
        }
        case MouthShotState::Free: freeActor(actor); return; // exe 0x5d0bc0
        default: notPortedAt(kStoryPort, "0x5c62f0 state", actor.state);
        }
    }
    if (actor.variant != kSplashVariant) notPortedAt(kStoryPort, "0x5c62d0 variant", actor.variant);
    SplashFields& splash = classState<SplashFields>(actor);
    switch (SplashState(actor.state)) {
    case SplashState::Start: // exe 0x5c64e0
        actor.collisionBox.pointAt(TableId::St45BossMouthShotBox);
        splash.sizeFloor = uint16_t(rngStep() & 0x1f);
        splash.size = uint8_t((rngStep() & 0x1f) + 0x20);
        splash.texelU = 0;
        actor.acceleration.y = 0xc0;
        splash.ticks = 0;
        splash.life = 0x3c;
        for (uint32_t k = 0; k < kSplashDrops; ++k) {
            SplashDrop& drop = splash.drops[k];
            drop.point.x = actor.pos.x.whole;
            drop.point.y = actor.pos.y.whole;
            drop.point.z = actor.pos.z.whole;
            drop.yaw = int16_t(k << 9);
            splash.fall[k] = int16_t(0xfd00);
            splash.forward[k] = int16_t(0xfd00);
        }
        ++actor.state;
        return;
    case SplashState::Fall: // exe 0x5c65c0
        if (splash.life != 0) {
            splashDrops(actor, splash);
            splash.life -= 1u;
        } else {
            actor.state = uint8_t(SplashState::Free);
        }
        return;
    case SplashState::Free: freeActor(actor); return; // exe 0x5d0bc0
    default: notPortedAt(kStoryPort, "0x5c64a0 state", actor.state);
    }
}

// ---- the shock wave (exe 0x5b0230, ST45 pool 0x60 type 0x3c sub 0; states [0x8cfa2c]) ------------------------------

enum class WaveState : uint8_t { Start = 0, Run = 1, Free = 2 };
// exe 0x5b0250: set up, both arm ends at the spawn point
void waveInit(StaticActor& actor, WaveFields& wave) {
    actor.enterStateBytes(uint8_t(WaveState::Run));
    actor.clearStepBytes();
    ShockWaveParams(actor).grey = 0x60;
    wave.reach = 0x10;
    wave.growth = 0x10;
    wave.shrink = 0;
    wave.outerHeight = 2;
    wave.innerHeight = 1;
    wave.spread = 0;
    wave.word = 0;
    wave.stopped = 0;
    wave.rayPoint.x = actor.pos.x.whole;
    wave.rayPoint.y = actor.pos.y.whole;
    wave.rayPoint.z = actor.pos.z.whole;
    wave.origin.x = actor.pos.x.whole;
    wave.origin.y = actor.pos.y.whole;
    wave.origin.z = actor.pos.z.whole;
    wave.startYaw = actor.rotation.y;
    for (uint32_t k = 0; k < 2; ++k) {
        WaveArm& arm = wave.arms[k];
        arm.point.x = actor.pos.x.whole;
        arm.point.y = actor.pos.y.whole;
        arm.point.z = actor.pos.z.whole;
        arm.yaw = actor.rotation.y;
    }
}
// one projected ribbon segment of 0x5b0330: v0 = P + out - (0, w, 0), v1 = P - (0, wPrev, 0), v2 = P + out, v3 = P;
// the projection scratch written as the exe does (the corners, their screen points, the depth); true = the depth is
// inside 1 .. the depth range (the segment is linked)
struct WaveSeg {
    int16_t v[4][3];
    uint32_t xy[4];
    int32_t otz;
};
bool waveSegment(const int16_t pt[3], const int16_t out[3], uint16_t w, uint16_t wPrev, WaveSeg& s) {
    ProjectionScratch& scratch = projectionScratch();
    const int16_t v[4][3] = {{t16(pt[0] + out[0]), t16(pt[1] - w + out[1]), t16(pt[2] + out[2])},
                             {pt[0], t16(pt[1] - wPrev), pt[2]},
                             {t16(pt[0] + out[0]), t16(pt[1] + out[1]), t16(pt[2] + out[2])},
                             {pt[0], pt[1], pt[2]}};
    LaneVec4 last = scratch.quadCorners[3];
    Lane16 lastLanes[3] = {last.x, last.y, last.z};
    for (uint32_t k = 0; k < 3; ++k) {
        LaneVec4 corner = scratch.quadCorners[k];
        corner.x = v[k][0];
        corner.y = v[k][1];
        corner.z = v[k][2];
        lastLanes[k] = v[3][k];
    }
    int16_t sxy[4][2];
    s.otz = projectQuad(v, sxy);
    for (uint32_t i = 0; i < 4; ++i) {
        for (int k = 0; k < 3; ++k) s.v[i][k] = v[i][k];
        s.xy[i] = (uint32_t(uint16_t(sxy[i][1])) << 16) | uint16_t(sxy[i][0]);
        scratch.quadScreenPoints[i] = s.xy[i];
    }
    scratch.quadDepth = s.otz;
    return !(s.otz > int32_t(stage().depthRange) || s.otz < 1);
}
// the POLY_FT4 packet (code 0x2e, the grey, CLUT 0x7c91, tpage 0x2f, uv (0xe0, 0x20) .. (0xff, 0x3f)) linked at the
// depth (render output: the corners go to the OT side channel of game/draw_commands)
void wavePacket(StaticActor& actor, const uint32_t xy[4], const int16_t* const v[4]) {
    const uint32_t c = ShockWaveParams(actor).grey;
    const uint32_t w[10] = {0, (((0x2e00u | c) << 8 | c) << 8) | c, xy[0], 0x7c9120e0, xy[1], 0x2f20ff, xy[2], 0x3fe0,
                            xy[3], 0x3fff};
    int16_t q[4][3];
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 3; ++k) q[i][k] = v[i][k];
    dc::otQuadFT4(actor.at, w, q);
}
// the actor's point moved by v through its root matrix (exe 0x40bb30)
void moveByRoot(StaticActor& actor, const int16_t v[3]) {
    int16_t out[3];
    transformShortVector(actor.rootMatrix, v, out);
    actor.pos.x.whole = int16_t(uint16_t(actor.pos.x.whole) + uint16_t(out[0]));
    actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) + uint16_t(out[1]));
    actor.pos.z.whole = int16_t(uint16_t(actor.pos.z.whole) + uint16_t(out[2]));
}
// the reach step of 0x5b0330: past the reach (0x100, or stopped) the actor's point moves (0, 0, -reach) along its root
// matrix; returns the point P
void waveReach(StaticActor& actor, const WaveFields& wave, int16_t pt[3]) {
    if (!(wave.reach < 0x100 && wave.stopped == 0)) {
        const int16_t v[3] = {0, 0, int16_t(-wave.reach)};
        moveByRoot(actor, v);
    }
    pt[0] = actor.pos.x.whole;
    pt[1] = actor.pos.y.whole;
    pt[2] = actor.pos.z.whole;
}
// exe 0x5b0d20: the player beyond the arc 0x300 inside the reach (reach * 4 from the centre ray's point, the spawn's
// yaw) but not beyond the reach's arc, inside the spread (the spawn's yaw - the first arm's) seen from the origin and
// within the allowed height -> a hit entry at the player (y - 0x40)
void waveHit(const StaticActor& actor, WaveFields& wave) {
    const uint16_t ca = uint16_t(wave.startYaw);
    int16_t dx = 0, dz = 0;
    int16_t p1[3], p2[3];
    angleVector(wave.startYaw, t16(int(wave.reach) << 2), dx, dz); // exe 0x5e40c0
    p1[0] = int16_t(wave.rayPoint.x + dx);
    p1[1] = wave.rayPoint.y;
    p1[2] = int16_t(wave.rayPoint.z + dz);
    wholes(player().pos, p2);
    if (int16_t((uint32_t(angleOf(p1, p2)) - ca - 0x400) & 0xfff) <= 0x800) return; // exe 0x5e3c00
    const int32_t r = int32_t(wave.reach) << 2;
    if (r - 0x300 <= 0) return;
    angleVector(wave.startYaw, t16(r - 0x300), dx, dz);
    p1[0] = int16_t(wave.rayPoint.x + dx);
    p1[1] = wave.rayPoint.y;
    p1[2] = int16_t(wave.rayPoint.z + dz);
    wholes(player().pos, p2);
    if (int16_t((uint32_t(angleOf(p1, p2)) - uint16_t(wave.startYaw) - 0x400) & 0xfff) > 0x800) return;
    wholes(player().pos, p2);
    const int16_t origin[3] = {wave.origin.x, wave.origin.y, wave.origin.z};
    const int yaw = angleOf(origin, p2);
    const int32_t spread0 = 0x1000 - int32_t(uint32_t(uint16_t(wave.startYaw) - uint16_t(wave.arms[0].yaw)) & 0xfff);
    const int32_t spread = spread0 + t16(spread0) / 4;
    const int32_t d = int32_t(uint32_t(spread - int32_t(ca) + yaw - 0x800) & 0xfff);
    if (d > int32_t(t16(spread)) * 2) return;
    const int32_t dy = int32_t(p1[1]) - int32_t(player().pos.y.whole);
    if ((dy < 0 ? -dy : dy) >= wave.rayHeight) return;
    p2[1] = int16_t(uint16_t(p2[1]) - 0x40u);
    hitListSubmit(p2, wave.hitBox, hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x200),
                  uint32_t(int32_t(ShockWaveParams(actor).attack)) | kHitAttackSolid); // exe 0x5e4120
}
// one strip of the wave (an arm or a ray): up to 8 segments from the point P along (0, 0, -reach) through the root
// matrix, the heights doubling; false when 8 segments go by without 4 linked (the tick ends there)
template <class Linked>
bool waveStrip(StaticActor& actor, const WaveFields& wave, int16_t pt[3], WaveSeg& s, Linked linked) {
    uint16_t wPrev = uint16_t(wave.innerHeight), w = uint16_t(wave.outerHeight);
    const int16_t in[3] = {0, 0, int16_t(-wave.reach)};
    for (uint8_t j = 0;;) {
        int16_t out[3];
        transformShortVector(actor.rootMatrix, in, out); // exe 0x40bb30
        linked.before(w, out);
        const bool on = waveSegment(pt, out, w, wPrev, s);
        wPrev = w;
        w = uint16_t(w * 2u);
        for (int i = 0; i < 3; ++i) pt[i] = s.v[2][i];
        ++j;
        if (!on) {
            if (j >= 8) return false;
            continue;
        }
        linked.after(s);
        if (j >= 4) return true;
    }
}
// an arm's linked segment: its packet, and its v0 / v2 kept for the joins
struct ArmLink {
    StaticActor& actor;
    uint32_t& xy0;
    uint32_t& xy2;
    int16_t* v0;
    int16_t* v2;
    void before(uint16_t, const int16_t*) {}
    void after(const WaveSeg& seg) {
        const int16_t* v[4] = {seg.v[0], seg.v[1], seg.v[2], seg.v[3]};
        wavePacket(actor, seg.xy, v);
        xy0 = seg.xy[0];
        xy2 = seg.xy[2];
        for (int i = 0; i < 3; ++i) {
            v0[i] = seg.v[0][i];
            v2[i] = seg.v[2][i];
        }
    }
};
// a ray's segment: the outer height - out.y kept (the exe's stack word), nothing linked per segment
struct RayLink {
    uint16_t& height;
    void before(uint16_t w, const int16_t* out) { height = uint16_t(w - uint16_t(out[1])); }
    void after(const WaveSeg&) {}
};
// exe 0x5b0330: grow, then the two arms (ribbons of 4 segments from the arm ends 0x20 aside, yaw +- the spread) and
// three rays between them joined by single packets; a segment is linked only when its depth is in range, and 8
// segments of one strip without reaching 4 end the tick there (no hit test); then the hit test
void waveRun(StaticActor& actor, WaveFields& wave) {
    if (wave.spread < 0x40) wave.spread += 4u;
    if (wave.word < 0x20) wave.word += 1u;
    if (wave.reach < 0x100 && wave.stopped == 0) {
        wave.reach += wave.growth;
        wave.growth += 4u;
        if (wave.innerHeight < 0x10) wave.innerHeight += 2u;
        else wave.innerHeight = 0x10;
        if (wave.outerHeight < 0x20) wave.outerHeight += 4u;
        else wave.outerHeight = 0x20;
    } else {
        wave.shrink += 1u;
        const uint16_t n = wave.shrink;
        wave.innerHeight -= n;
        wave.stopped = 1;
        if (wave.innerHeight < 2) wave.innerHeight = 1;
        wave.outerHeight -= n;
        if (wave.outerHeight < 2) {
            wave.outerHeight = 1;
            actor.state = uint8_t(WaveState::Free);
        }
    }
    // the exe's stack words: screen x / y of v0 / v2 of each arm's last linked segment (slot 0 then follows the
    // rays); the port keeps their corners for the side channel
    uint32_t xy0[2] = {}, xy2[2] = {};
    int16_t wv0[2][3] = {}, wv2[2][3] = {};
    for (uint32_t k = 0; k < 2; ++k) { // the arms
        WaveArm& arm = wave.arms[k];
        actor.pos.x.whole = arm.point.x;
        actor.pos.y.whole = arm.point.y;
        actor.pos.z.whole = arm.point.z;
        const uint16_t yaw = uint16_t(arm.yaw);
        actor.rotation.y = int16_t(k ? uint32_t(yaw - uint16_t(wave.spread)) : uint32_t(uint16_t(wave.spread) + yaw));
        actor.rootMatrix = rotationZYX(actor.rotation.x, actor.rotation.y, actor.rotation.z); // exe 0x40c070
        const int16_t side[3] = {int16_t(k ? 0x20 : -0x20), 0, 0};
        moveByRoot(actor, side);
        int16_t pt[3];
        waveReach(actor, wave, pt);
        WaveSeg s;
        if (!waveStrip(actor, wave, pt, s, ArmLink{actor, xy0[k], xy2[k], wv0[k], wv2[k]})) return;
        arm.point.x = actor.pos.x.whole;
        arm.point.y = actor.pos.y.whole;
        arm.point.z = actor.pos.z.whole;
        arm.yaw = actor.rotation.y;
    }
    // the rays: yaw = the spawn's - 2d + 1, then + d each, d = (the spawn's yaw - the first arm's) / 4 / 2 (truncated)
    const int32_t diff = int32_t(wave.startYaw) - int32_t(wave.arms[0].yaw);
    const int32_t d = (diff / 4) / 2;
    uint32_t ang = uint32_t(uint16_t(wave.startYaw)) - uint32_t(int32_t(t16(d)) * 2) + 1u;
    int16_t centre[4] = {};
    uint16_t height = 0;
    for (uint32_t k2 = 0; k2 < 3; ++k2) {
        actor.pos.x.whole = wave.rayPoint.x;
        actor.pos.y.whole = wave.rayPoint.y;
        actor.pos.z.whole = wave.rayPoint.z;
        actor.rotation.y = int16_t(ang);
        actor.rootMatrix = rotationZYX(actor.rotation.x, actor.rotation.y, actor.rotation.z); // exe 0x40c070
        int16_t pt[3];
        waveReach(actor, wave, pt);
        WaveSeg s{};
        if (!waveStrip(actor, wave, pt, s, RayLink{height})) return;
        // one packet from the previous strip's last segment to this ray's (linked at this ray's depth)
        const uint32_t xy[4] = {xy0[0], s.xy[0], xy2[0], s.xy[2]};
        const int16_t* v[4] = {wv0[0], s.v[0], wv2[0], s.v[2]};
        wavePacket(actor, xy, v);
        xy0[0] = s.xy[0];
        xy2[0] = s.xy[2];
        for (int i = 0; i < 3; ++i) {
            wv0[0][i] = s.v[0][i];
            wv2[0][i] = s.v[2][i];
        }
        if (k2 == 1) {
            centre[0] = actor.pos.x.whole;
            centre[1] = actor.pos.y.whole;
            centre[2] = actor.pos.z.whole;
            centre[3] = int16_t(height);
        }
        ang = (uint32_t(d) + ang) & 0xfff;
    }
    wave.rayPoint.x = centre[0];
    wave.rayPoint.y = centre[1];
    wave.rayPoint.z = centre[2];
    wave.rayHeight = centre[3];
    { // the last ray to the second arm (linked at the last depth)
        const uint32_t xy[4] = {xy0[0], xy0[1], xy2[0], xy2[1]};
        const int16_t* v[4] = {wv0[0], wv0[1], wv2[0], wv2[1]};
        wavePacket(actor, xy, v);
    }
    wave.hitBox.result = 0;
    wave.hitBox.previousX = actor.prevPos.x.whole;
    wave.hitBox.previousZ = actor.prevPos.z.whole;
    waveHit(actor, wave); // exe 0x5b0d20
}
void shockWave(StaticActor& actor) { // states [0x8cfa2c]: set up, run, free (exe 0x5afdd0 -> 0x5e1540)
    WaveFields& wave = classState<WaveFields>(actor);
    switch (WaveState(actor.state)) {
    case WaveState::Start: waveInit(actor, wave); return;
    case WaveState::Run: waveRun(actor, wave); return;
    case WaveState::Free: freeActor(actor); return;
    default: notPortedAt(kStoryPort, "0x5b0230 state", actor.state);
    }
}

} // namespace

void st45BossUpdate(const PlayerEnv& env, uint32_t actor) { bossTick(env, animatedActorAt(actor)); }
void st45BreathPuffUpdate(const PlayerEnv& env, uint32_t actor) { breathPuff(env, projectileActorAt(actor)); }
void st45SpitBlobUpdate(const PlayerEnv& env, uint32_t actor) { spitBlob(env, projectileActorAt(actor)); }
void st45MouthShotUpdate(const PlayerEnv& env, uint32_t actor) { mouthShotTick(env, projectileActorAt(actor)); }
void st45ShockWaveUpdate(const PlayerEnv&, uint32_t actor) { shockWave(staticActorAt(actor)); }

} // namespace od::port
