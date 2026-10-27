// Actor system port (game/actors) vs. the game, tick by tick (tests/data/act_*.bin -- not in git (*.bin), regenerate
// Each golden holds the game state before and
// after every actor-pool loop of the frame body FUN_00542280. For every tick and loop the test
// loads the state before the loop, runs od::port::updatePool and compares, for each actor whose class is ported,
// its whole struct with the state after the loop (and its bone blocks); the globals the classes write (RNG, event
// request, event flags, transition request, hit list, actor collision queues) are compared when the pool held no
// unported actor (an unported actor's update would change them in between).
// Not compared by design: class 0x20 actor +0x86..+0x93 (FUN_005e2de0 copies 8 dwords of a stack matrix with 5
// valid ones: pad + stack garbage), the pad short of every bone-block matrix (+0x12, Maybe_MulMatrix3x3 copies a
// stack pad) and bytes +0x14..+0x1f of each matrix (not written by the pose).
//   test_actors <dat dir> <dash2.exe> <tests/data> [max reported failures] [name filter]
// Recipes:
//   act_ST09_idle.bin  --stage 09 --script "300 -"     (two route walkers, town NPC class 0x441950)
//   act_ST09_sign.bin  --stage 09 --teleport -2048 0 -3888 --yaw 0 --script "5 -; 1 circle; 60 -; 1 circle; 30 -;
//                      1 circle; 40 -"   (sign 0x58d6e0, message 15; ST09 static type 1 variants 0 / 2 / 3 / 4 / 5)
//   act_ST09_prop.bin  (same) --teleport -384 0 -6240 --yaw 0   (ST09 static type 1 variant 5: talk, message 0x13)
//   act_ST08_prop.bin  --stage 08 --teleport -288 0 2976 --yaw 0 (same script)   (ST08 static type 1 variant 4)
//   act_ST09_kick.bin  --stage 09 --teleport -1000 0 -2000 --yaw 0 --script "5 -; 3 square; 30 -; 3 square; 30 -"
//                      (the town "buster" = the kick: Maybe_TownShotUpdate 0x56bde0, pool 0x10 type 0; its sound 0x95
//                      is in the compared sound queue)
//   act_ST13_lockon.bin --stage 13 --combat --psx --teleport -2688 -192 -2200 --yaw 2048 --script "5 -; 30 r2;
//                      3 r2+square; 25 r2; 15 -"   (lock-on FUN_005780e0 with r2 held, the lock timer +0xd2, shots
//                      homing FUN_005732f0;
//   act_ST13_lockproj.bin / act_ST09_lockproj.bin: the selection with the renderer's projection constants (poked:
//                      P="--poke 0xa59d10 0x80 --poke 0xa59d11 1 --poke 0xa59a20 0xc0 --poke 0xa59d20 0xa0
//                      --poke 0xa59d1c 0x78"), checks the reticle (Maybe_DrawLockOnReticle 0x5f2870) on the states:
//                      --stage 13 --combat --psx --teleport -2688 -192 -2200 --yaw 2048 $P
//                      --script "5 -; 30 -; 30 r2; 3 r2+square; 25 r2; 20 -"
//                      --stage 09 --combat --psx --teleport -2409 0 -3640 --yaw 2048 $P
//                      --script "5 -; 40 up; 30 -; 40 r2; 20 -"
//   act_ST0B_save.bin  --stage 0B --sub 1 --teleport 576 -32 352 --yaw 2048 --script "5 -; 1 circle; 30 -; 1 down;
//                      3 -; 1 down; 3 -; 1 circle; 60 -; 1 circle; 40 -; 1 circle; 30 -; 1 right; 3 -; 1 circle;
//                      40 -; 1 cross; 60 -"   (Data, the talking NPC 0x449620: menu, save menu FUN_00548f60 states
//                      0 / 3 / 1: slot list, slot 0 exists -> overwrite? -> no -> cancel; nothing is written)
//   act_ST09_talk.bin  --stage 09 --teleport -204 0 -1117 --script "5 -; 1 circle; 60 -; 1 circle; 30 -; 1 circle;
//                      30 -; 1 circle; 30 -; 1 circle; 30 -; 1 circle; 60 -"   (talk to the walker: sub-states 2 / 3)
//   act_ST09_bump.bin  --stage 09 --teleport -204 0 -1117 --script "5 -; 40 up; 60 -"   (walk into it: sub-state 4)
//   act_ST0A_talk.bin  --stage 0A --sub 2 --teleport 256 0 272 --yaw 0 --script (as ST09_talk) + "; 100 -"
//                      (stationary NPC, +0xd 0x88)
//   act_ST08_idle.bin  --stage 08 --script "300 -"     (route walker +0xd 0x10)
//   act_ST19_idle.bin  --stage 19 --sub 1 --script "300 -"   (fidget kind 0x81, partner NPCs)
//   act_ST19_pause.bin --stage 19 --sub 1 --warmup 600 --script "150 -"   (route step 4: pause, tick 66)
//   act_ST12_door.bin  --stage 12 --sub 1 --teleport -3584 -192 -6048 --yaw 0 --range 0x7f100080 4 (the CRT seed)
//                      --script "3 -; 2 circle; 45 -; 1 circle; 60 -"  (door actor FUN_0059ce50: event, "yes")
//   act_ST1A_platform.bin --stage 1A --script "150 -"  (0x590100 movers)
//   act_ST24_servbots.bin --stage 24 --save ".../DASH2-02.DAT" --warmup 1 --range 0xa714c0 4 --script "400 -"
//                      (servbots 0x4628b0: state 0, the spawn drop (sub-state 10), routes incl. pause / wall turn)
//   act_ST24_servbot_talk.bin (save 02) --warmup 10 --range 0xa714c0 4 --teleport -2048 -300 600 --yaw 2048
//                      --script "40 -; 1 circle; 60 -; 1 circle; 30 -; 1 circle; 30 -; 1 circle; 60 -"   (face / talk)
//   act_ST24_servbot_kick.bin (save 02) --warmup 10 --range 0xa714c0 4 --teleport -2048 -300 700 --yaw 2048
//                      --script "20 -; 12 up; 3 square; 10 -; 3 square; 10 -; 3 square; 150 -"   (kicked: 8, 9)
// The goldens above carry no PS1 RAM image: the room block and the model
// bank come from ST<stage>.DAT, mapped where the game had them ([0x6d0084] - pool offset, [0x6d0074]).
// Combat goldens (game/hit_list and the combat classes, docs/combat.md): --combat --psx, all with --stage 13 unless
// noted
//   act_ST13_buster_wall.bin   --teleport -2688 -192 -2200 --yaw 0 --script "5 -; 3 square; 45 -"
//   act_ST13_buster_kill.bin   --teleport -2688 -192 -2200 --yaw 2048 --script "5 -; 3 square; 5 -; 3 square;
//                              5 -; 3 square; 5 -; 3 square; 5 -; 3 square; 5 -; 3 square; 5 -; 3 square; 40 -"
//   act_ST13_enemy_airkill.bin --teleport -2688 -192 -2200 --yaw 2048 --script "5 -; 3 square; 15 -; 3 square; 75 -"
//   act_ST13_enemy_hurt.bin    --teleport -2688 -192 -1124 --script "5 -; 80 -; 20 up; 60 -"
//   act_ST13_enemy_jump.bin    --teleport -2688 -192 -1700 --yaw 2048 --script "5 -; 1 cross; 40 -"
//   act_ST13_enemy_turn.bin    --teleport -2688 -192 -3500 --yaw 0 --script "250 -"
//   act_ST13_pickup.bin        (buster_kill's teleport / yaw) --script (buster_kill's without "40 -") + "; 10 -; 45 up;
//   20 -" act_ST14_enemy_hunt.bin    --stage 14 --sub 1 --script "150 -" act_ST16_lever.bin  --from levpre.bin --combat
//   --psx $W --range 0x8e25c8 0x40 --script "20 -; 1 cross; 30 -" (the ST16 bars 0x58f040 + player
//                       sub-state 11; st16_500 (story.md 16, the missions) +
//                       flag 0x682, box static slot 2 (0xa679f8) moved to (1472, -2048, 80) and the player on it (1472,
//                       -2208, 80): jump -> grab at 23, the lever down -> flag 0x27 at 34, script 0x34; plus the gate
//                       halves 0 / 1)
//   act_ST18_gunship_4750 / _5000 / _5250.bin --from kk_N.bin --combat --psx --script "1 r2+square; 2 r2" x 100 (x 87
//                       for 5250) (the gunship static 0x59f880 after script 0x37 + the still prop 0x5abb70: fly / burst
//                       / dive, shot down -> sinks, rocks, flag 0x2b at 85;
//                       the missions) + --flag 0x2a -> s37 (script 0x37 to its end), then 1500 ticks of "1 r2+square; 2
//                       r2" with the life refilled every tick, checkpoints every 250 -> kk_N)
//   act_ST16_box_carry.bin --from st16_250.bin --combat --psx $W --range 0x8e25c8 0x40 --poke 0xa71eac 0 --teleport
//   1440 -2048 80 --yaw 2048
//                       --script "5 -; 10 triangle; 40 -; 20 up; 10 -; 1 triangle; 50 -" (the lift / carry system: box
//                       static 0x5a89b0 slot 2 lifted (player sub-state 16, FUN_00578b10), carried while walking,
//                       thrown (FUN_00573870) onto box slot 4 (stand-on: e0 0x4250d0); st16_250 = the missions' start
//                       (save 02,
//                       --stage 16 --sub 0, story 6, flags 0x580 / 0x581 / 0x24 / 0x31 / 0x25, "1 cross; 29 -"
//                       repeated, every 250); 0xa71eac = 0: no special weapon equipped, so triangle lifts)
//   act_ST13_carry_throw.bin --from st13.bin --combat --psx --poke 0xa71eac 0 --poke 0xa6e678 0 --teleport -2688 -192
//   -1500
//                       --yaw 2048 --script ("1 triangle; 1 -" x 15) + "; 40 -; 1 triangle; 70 -" (the hopper 0x49f660
//                       lifted as it lands in front (its partner, slot 2, removed), carried (state 2 0x4a08d0 sub
//                       0x4a0940), thrown (sub 0x4a0a60: two bounces, then main sub-state 7); st13 = python -m
//                       --save DASH2-00.DAT --stage 13 --sub 0)
//   act_ST13_carry_drop.bin  (same, the partner kept) --script ("1 triangle; 1 -" x 40) (lifted, the partner hits the
//                       player: dropped -- FUN_00573950 = 2)
//   act_ST30_npc_carry.bin --from x1_900.bin --combat --psx $W --range 0x8f5cf8 0x40 --poke 0xa71eac 0 --teleport -7 0
//   2400
//                       --yaw 2048 --script "5 -; 10 triangle; 40 -; 20 up; 10 -; 1 triangle; 60 -" (ST30 sub 2 in the
//                       escape: the walkers 0x493a60 (type 0x2d), the turrets 0x4e0f50 (type 0x57), Shu 0x4f2ee0 (slot
//                       11, wanders: CRT rand after the turrets), the gunship static 0x59f880 variant 3; x1_900 =
// 19 -" --repeat
//                       --every 100)
//   act_ST30_gunship_bomb.bin --from x1_900.bin --combat --psx $W --range 0x8f5cf8 0x40 --teleport -1024 0 600 --yaw 0
//                       --script "120 -" (the gunship static 0x59f880 variant 3 over the player: FUN_0059ff00 bombs
//                       every 4th tick for 0x10 ticks (static type 0x22, the bomb 0x5a7330: game/bomb), then the cooldown
//                       0xf0)
//   act_ST16_birdbot_hit.bin --from st16_500.bin --combat --psx $W --range 0x8e25c8 0x40 --teleport 1000 -2048 1500
//   --yaw 2048
//                       --script "5 -; " + "1 r2+square; 4 r2" x 30 + "; 30 -" (the Nino mission Birdbots = walkers
//                       0x493a60 variant 0 shot down: 0x4959f0 returns 2 when the hit points run out -> state 3 -> the
//                       knocked-down Birdbot 0x492330 (type 0x2d sub 0);
//                       1500 --save DASH2-02.DAT
//                       --stage 16 --sub 0 + the mission pokes / flags (test_story, the missions) "1 cross; 29 -"
//                       --repeat --every 250)
//   act_ST16_birdbot_down.bin (same) --script "5 -; " + "1 r2+square; 4 r2" x 30 + "; " + "1 square; 9 -" x 30 + "; 40
//   -"
//                       (0x492330 subs 0 / 1 / 2 / 4 / 5: bounced, runs around the player, knocked, falls, hops; ends
//                       before tick 506, where the unported carrier 0x4c7400 (type 0x44) moves the variant-3
//                       reinforcements)
//   act_ST15_tanks.bin --from st15_500.bin --combat --psx $W --range 0x8e1cc0 0x40 --script "400 -" (the mission tanks
//                       0x4a1c40 (type 0x33) aiming / firing at the type-0x1b statics, their shells 0x5c6cb0 (pool 0x40
//                       type 0x11); st15_500 = explore st15 2000 --save DASH2-02.DAT --stage 15 --sub 0 + the mission
//                       pokes / flags, "1 cross; 29 -" --repeat --every 250)
//   act_ST15_tank_hurt.bin (same) --teleport 1500 -2048 0 --yaw 1024 --script "5 -; " + "1 r2+square; 4 r2" x 56 +
//                       "; 215 -" (500 ticks: the tanks hit (FUN_004a2770), death 0x4a23d0, the wreck; the gunships
//                       0x59d310 (type 0x1b sub 0: cruise, bomb runs FUN_0059e390, shot down 0x5a28e0), their bombs
//                       0x5a7330 (type 0x22: bounces, fuse, blast) and the mission targets 0x5ab8b0 (type 0x2d))
//   act_ST16_crane.bin --from st16_1000.bin --combat --psx $W --range 0x8e25c8 0x40 --script "250 -" (the crane
//   0x4c7400
//                       (type 0x44): parked -> a box (static type 0x25 variant 0x80, +3 = 8) carried to x 0x610, let
//                       go, turns and flies back; st16_N = explore st16 1500 from the mission start (as st16_500)
//                       --every 250)
//   act_ST18_bombers.bin --from st18_250.bin --combat --psx $W --script "400 -" (the bomber Birdbots 0x495af0, ST18
//   type
//                       0x2d sub 1: take a bomb, walk in, throw it, spin / hop; st18_250 = explore st18 1500 --save
//                       DASH2-02.DAT --stage 18 --sub 0 + the mission pokes / flags + --flag 0x27 0x28 0x29, "1 cross;
//                       29 -"
//                       --repeat --every 250; --script "320 -" since the bomb 0x5a7330 is ported: at tick 323 the
//                       unported ST18 static 0x59e630 (type 0x1b) consumes the RNG before a bouncing bomb. Shot down
//                       (0x4964b0) is still not covered)
//   act_ST17_boat.bin --from st17_250.bin --combat --psx $W --range 0x8e30d0 0x40 --script ("1 cross; 29 -" x 20) (the
//                       ST17 mission: the ferry boat 0x5a5ea0 variant 1 (walkers 0x493a60 variant 4 down its gangway,
//                       sub 14 0x4951f0 / FUN_004952c0), the opening's end: the carrier boat variant 0 (its two walker
//                       spawners e0 type 0xa variants 1 / 0, sub 0 waiting), the target 0x5ab8b0 variant 2; st17_250 =
//                       explore st17 2000 --save DASH2-02.DAT --stage 17 --sub 0 + the mission pokes / flags + --flag
//                       0x27 0x28, "1 cross; 29 -" --repeat --every 250)
// $W (2026-10-11, the walker class 0x493a60 and every golden where it runs): --range 0xa6c420 0x20 --range 0xa7b080
// 0x120 --range 0x7de700 0x80 --range 0x7f100080 4 -- the enemy list mask 0xa6c42c, the tracked set 0xa7b088 /
// 0xa7b0a0, the neighbour list 0x7de700 (in the PS1 image, static otherwise), the CRT rand seed; plus the stage's
// group table *0xa7258c (ST30 0x8f5cf8, ST16 0x8e25c8).
// , so the ST16 goldens below were regenerated with $W + --range 0x8e25c8 0x40.
// On these the test also runs the player phase (FUN_00574860), the player-update phase (FUN_004140e0 +
// Maybe_UpdatePlayer), the hit-list phase (FUN_005e4260) and a closed loop of ported game frames (see closedLoop).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "check.hpp"
#include "data_source_binding.hpp"
#include "content/registry.hpp"
#include "engine/bank.hpp"
#include "engine/dat.hpp"
#include "engine/room.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_pools.hpp"
#include "game/actors.hpp"
#include "game/camera.hpp"
#include "game/effects_draw.hpp"
#include "translator/game_memory.hpp"
#include "game/hit_list.hpp"
#include "game/modern_play.hpp"
#include "game/player/player_update.hpp"
#include "game/fade.hpp"
#include "game/room_uv_animation.hpp"
#include "game/stage_state.hpp"
#include "game/transition.hpp"
#include "golden_delta.hpp"
#include "translator/state_translator.hpp"

namespace fs = std::filesystem;
using od::port::ActorPool;

namespace {

using od::test::Range;
using od::test::Rec;
struct Golden : od::test::DeltaGolden { // tests/golden_delta.hpp (ACT1: no segment fields)
    bool psx = false;    // a static block at 0x630000 (PS1 RAM image)
    bool combat = false;
};

bool readGolden(const fs::path& path, od::GameMem& m, Golden& g) {
    if (!od::test::readDeltaGolden(path, "ACT1", m, g, true, false)) return false;
    for (const Range& rg : g.ranges) g.combat |= rg.addr == 0xa599e0;
    for (uint32_t a : g.statics) g.psx |= a == 0x630000;
    for (const Range& rg : g.ranges)
        if (!m.mapped(rg.addr, rg.size)) m.mapZero(rg.addr, rg.size);
    return true;
}

struct Phase {
    ActorPool pool;
    uint8_t before, after;
    const char* name;
    uint32_t base, stride, count;
};
const Phase kPhases[] = {
    {ActorPool::Controllers, 0, 1, "pool e0", 0xa6bfa0, 0x20, 32},
    {ActorPool::Animated, 2, 3, "pool 20", 0xa6e3a0, 0x16c, 24},
    {ActorPool::Projectiles, 3, 4, "pool 40", 0xa78420, 0xdc, 32},
    {ActorPool::Effects, 4, 5, "pool a0", 0xa6c6e0, 0x28, 64},
    {ActorPool::Statics, 5, 6, "pool 60", 0xa67860, 0xcc, 64},
    {ActorPool::Shots, 7, 8, "pool 10", 0xa6ab80, 0x100, 16},
    {ActorPool::EffectsLate, 9, 10, "pool a0 late", 0xa6c6e0, 0x28, 64},
    {ActorPool::StaticsLate, 10, 11, "pool 60 late", 0xa67860, 0xcc, 64},
    {ActorPool::ControllersLate, 11, 12, "pool e0 late", 0xa6bfa0, 0x20, 32},
};

struct Runner {
    od::GameMem& m;
    const Golden& g;
    fs::path datDir;
    int maxReports;
    od::port::PlayerEnv env;
    std::map<int, od::GameFile> stFiles;
    uint32_t roomKey = 0xffffffff;
    bool psxImage = false; // the golden stored the PS1 RAM image
    uint32_t bankAt = 0;
    int reported = 0;
    int compared = 0;
    // the lock-on reticle (Maybe_DrawLockOnReticle 0x5f2870 = od::fx reticle) on the state after the hit list:
    // ticks with a target (+0xcc) and the countdown +0xd0 at 8 (not locked) / < 1 (locked, flown in), and how many
    // of those drew it
    int reticleIdle = 0, reticleIdleDrawn = 0, reticleLocked = 0, reticleLockedDrawn = 0;

    const od::GameFile& stFile(int stage) {
        auto it = stFiles.find(stage);
        if (it != stFiles.end()) return it->second;
        char name[16];
        std::snprintf(name, sizeof name, "ST%02X.DAT", stage);
        return stFiles[stage] = od::pcGameFile(name, od::readFile(datDir / name));
    }
    // the active grid, mapped where the game had its room block (see test_transition)
    void bindRoom() {
        const int stage = m.u8(0xa6d190);
        const uint32_t pool = m.u32(0x6d0084);
        const uint32_t key = uint32_t(stage) << 24 ^ pool ^ (uint32_t(m.u8(0x6d0094)) << 8) ^ m.u8(0x6d0095) ^
                             (m.u32(0x6d0088) * 0x9e3779b1u);
        if (key == roomKey) return;
        const auto block = od::roomBlock(stFile(stage));
        for (const od::RoomGrid& gr : od::roomGrids(block)) {
            if (gr.x0 != m.u8(0x6d0094) || gr.z0 != m.u8(0x6d0095)) continue;
            const od::CollisionRoom probe = od::collisionRoom(block, gr);
            const uint32_t base = pool - uint32_t(probe.pool);
            if (base + probe.segments != m.u32(0x6d008c) || base + probe.cells != m.u32(0x6d0088)) continue;
            // the room block where the game had it in PS1 RAM (from the DAT unless the golden carries the image)
            if (!psxImage) m.map(base, block);
            const auto [shared, origin] = m.sharedAt(base, uint32_t(block.size()));
            env.room = od::collisionRoom(shared.share(), gr, origin); // the image's block itself (a copy detaches on the first write)
            break;
        }
        roomKey = key;
        // the stage's model bank (animation / skeleton / collision data the actors point into) where the game had it
        const uint32_t bank = m.u32(0x6d0074);
        if (!psxImage && bank && bank != bankAt) {
            m.map(bank, od::modelBank(stFile(stage)));
            bankAt = bank;
        }
    }
    void load(const Rec& r) {
        od::test::storeNativeState(m);
        size_t o = 0;
        for (const Range& rg : g.ranges) {
            m.write(rg.addr, r.blob.data() + o, rg.size);
            o += rg.size;
        }
        // goldens recorded without 0xa714c0: the value FUN_0050f260 derives from the difficulty 0xa6d196 at game
        // start / load (exe table 0x8993a4), constant during play -- the servbot class 0x4628b0 reads it every tick
        uint8_t dummy;
        if (!want(r, 0xa714c0, dummy)) {
            if (!m.mapped(0xa714c0, 4)) m.mapZero(0xa714c0, 4);
            m.set32(0xa714c0, m.u32(0x8993a4 + uint32_t(m.u8(0xa6d196)) * 4));
        }
        bindRoom();
        od::test::loadNativeState(m);
        bindUvAnimations(r);
    }
    // goldens recorded without the room's texture animation list (0xa6d0b0 names it, its entries are not captured):
    // the entries the room setup built from the room's table (game/room_uv_animation, the rooms that have one), once;
    // their frame / tick start at 0 (they only time the room's texel writes, not captured either). load() keeps them:
    // it stores the native state before writing the golden's ranges
    void bindUvAnimations(const Rec& r) {
        od::port::StageState& s = od::port::stage();
        uint8_t v;
        if (s.textureAnimations == 0 || s.textureAnimationCount == 0 || want(r, s.textureAnimations, v) ||
            od::port::roomUvAnimations().entries[0].steps != 0)
            return;
        static constexpr std::pair<od::TableId, od::TableId> kLists[] = {
            {od::TableId::IntroRoomsSt00UvAnimList, od::TableId::IntroRoomsSt00UvAnimTable},
            {od::TableId::IntroRoomsSt03UvAnimList, od::TableId::IntroRoomsSt03UvAnimTable},
            {od::TableId::TownRoomsSt0aUvAnimList, od::TableId::TownRoomsSt0aUvAnimTable}};
        for (const auto& [list, table] : kLists)
            if (od::stateAddress(list) == s.textureAnimations) {
                od::port::roomUvAnimationSetup(s.textureAnimations, od::stateAddress(table));
                return;
            }
    }
    // the golden's byte at address a in record r (false: not captured)
    bool want(const Rec& r, uint32_t a, uint8_t& v) const {
        size_t o = 0;
        for (const Range& rg : g.ranges) {
            if (a >= rg.addr && a < rg.addr + rg.size) {
                v = r.blob[o + (a - rg.addr)];
                return true;
            }
            o += rg.size;
        }
        return false;
    }
    int compareRange(const char* what, uint32_t tick, uint32_t a0, uint32_t n, const Rec& after,
                     bool (*skip)(uint32_t off) = nullptr) {
        int diffs = 0;
        for (uint32_t o = 0; o < n; ++o) {
            if (skip && skip(o)) continue;
            uint8_t w;
            if (!want(after, a0 + o, w)) continue;
            const uint8_t got = m.u8(a0 + o);
            if (got == w) continue;
            if (reported < maxReports && diffs < 10)
                std::fprintf(stderr, "  tick %u %s %08x +0x%03x: want %02x got %02x\n", tick, what, a0, o, w, got);
            ++diffs;
        }
        return diffs;
    }

    // one pool loop of one tick; returns false on a mismatch
    bool phase(const Phase& ph, const Rec& before, const Rec& after, od::port::ActorContext& ctx) {
        load(before);
        std::vector<uint32_t> ported;
        for (uint32_t i = 0; i < ph.count; ++i) {
            const uint32_t a = ph.base + i * ph.stride;
            const od::RoutineRef h = od::port::actorHandler(od::port::actorHeaderAt(a), ph.pool);
            if (h && od::port::portedActorClass(h.id())) ported.push_back(a);
        }
        if (ported.empty()) return true;
        bool ok = true;
        try {
            od::port::updatePool(env, ctx, ph.pool);
        } catch (const std::exception& e) {
            if (reported < maxReports) std::fprintf(stderr, "  tick %u %s: %s\n", before.tick, ph.name, e.what());
            ok = false;
        }
        if (!ctx.disabled.empty()) {
            if (reported < maxReports)
                std::fprintf(stderr, "  tick %u %s: %zu class(es) disabled (not ported branch)\n", before.tick, ph.name,
                             ctx.disabled.size());
            ctx.disabled.clear();
            ok = false;
        }
        od::test::storeNativeState(m); // the image compared below
        int diffs = 0;
        for (const uint32_t a : ported) {
            ++compared;
            if (ph.pool == ActorPool::Animated) {
                // Glyde's tank (type 0x40 sub 1): its turret yaw +0x2e may be off by one, the exe's aim point having
                // stale stack fractions (content/calbania/glyde_base_tank aimTurret); then its pose is too
                bool turretOff = false;
                if (uint8_t lo, hi; m.u8(a + 4) == 0x40 && m.u8(a + 5) == 1 && want(after, a + 0x2e, lo) &&
                                    want(after, a + 0x2f, hi)) {
                    const int d = int(m.s16(a + 0x2e)) - int(int16_t(uint16_t(lo | hi << 8)));
                    turretOff = d == 1 || d == -1;
                }
                diffs += compareRange(ph.name, before.tick, a, ph.stride, after, turretOff ? +[](uint32_t o) {
                    return (o >= 0x86 && o < 0x94) || o == 0x2e || o == 0x2f;
                } : +[](uint32_t o) { return o >= 0x86 && o < 0x94; });
                for (int k = 0; !turretOff && k < 8; ++k) { // the actor's bone blocks
                    int left = k + 1;
                    uint32_t blk = 0;
                    const uint32_t mask = m.u32(a + 0xcc);
                    for (int i = 0, bit = 31; bit >= 0; ++i, --bit)
                        if ((mask & (1u << bit)) && --left == 0) {
                            blk = 0xa72660 + uint32_t(i) * 0xb0;
                            break;
                        }
                    if (!blk) break;
                    diffs += compareRange("bone block", before.tick, blk, 0xa0, after, [](uint32_t o) {
                        return o < 0x80 ? (o & 0x1f) >= 0x12 : (o & 7) >= 6;
                    });
                }
            } else {
                diffs += compareRange(ph.name, before.tick, a, ph.stride, after);
            }
        }
        if (ctx.skipped.empty()) { // globals the ported classes write
            diffs += compareRange("rng", before.tick, 0x6d0000, 4, after);
            diffs += compareRange("event", before.tick, 0xa5f280, 0x20, after);
            diffs += compareRange("flags", before.tick, 0xa73cc0, 0x720, after);
            // message windows the talking NPC's menus open / close (game/save saveMenuTick), the save slot state
            diffs += compareRange("windows", before.tick, 0xa7adc0, 0x280, after);
            diffs += compareRange("window bits", before.tick, 0xa72234, 4, after);
            diffs += compareRange("save slots", before.tick, 0xa7b040, 0x28, after);
            diffs += compareRange("save slots", before.tick, 0xa5a248, 4, after);
            diffs += compareRange("request", before.tick, 0xa6d2e0, 0x1c, after);
            diffs += compareRange("hit list", before.tick, 0xa7a184, 4, after);
            diffs += compareRange("hit list", before.tick, 0xa7a1a0, m.u32(0xa7a184) * 0x18, after);
            diffs += compareRange("solids", before.tick, 0xa743e0, 0x10, after);
            diffs += compareRange("solids", before.tick, 0xa5f2a0, m.u32(0xa743e8) - 0xa5f2a0, after);
            diffs += compareRange("movers", before.tick, 0xa5f5a0, m.u32(0xa743e4) - 0xa5f5a0, after);
            diffs += compareSound("sound queue", before.tick, after); // game/audio_logic requests
            if (g.combat) { // what the combat classes spawn / free in other pools (drops, explosions, effects)
                diffs += compareRange("spawned statics", before.tick, 0xa67860, 0x40 * 0xcc, after);
                diffs += compareRange("spawned statics", before.tick, 0xa79fa8, 8, after);
                diffs += compareRange("spawned effects", before.tick, 0xa6c6e0, 0x40 * 0x28, after);
                diffs += compareRange("spawned effects", before.tick, 0xa71120, 8, after);
                diffs += compareRange("pending frees", before.tick, 0xa6ab64, 4, after);
                diffs += compareRange("pending frees", before.tick, 0xa78400, 4, after);
                diffs += compareRange("pending frees", before.tick, 0xa79ff0, 8, after);
                diffs += compareRange("bone blocks", before.tick, 0xa6bb90, 4, after);
                diffs += compareRange("kill counters", before.tick, 0xa6d1b8, 8, after);
                diffs += compareRange("zenny", before.tick, 0xa6d1a8, 4, after);
                diffs += compareRange("player life", before.tick, 0xa71d90, 4, after);
                diffs += compareRange("player energy", before.tick, 0xa71eb0, 4, after);
                diffs += compareRange("player magnet", before.tick, 0xa71dea, 1, after);
                diffs += compareRange("wall hits", before.tick, 0xa5a77c, 0x204, after);
                diffs += compareRange("player (shots)", before.tick, 0xa71ebc, 2, after);
                diffs += compareRange("player (shots)", before.tick, 0xa71e5c, 2, after);
            }
        }
        return ok && diffs == 0;
    }

    // ---- combat goldens (--combat): the steps between the pool loops that the hit list + the player port cover --
    int comparePools(const char* what, uint32_t tick, const Rec& after) {
        int diffs = 0;
        diffs += compareRange(what, tick, 0xa6ab80, 0x10 * 0x100, after);                       // pool 10 (shots)
        diffs += compareRange(what, tick, 0xa6c6e0, 0x40 * 0x28, after);                        // pool a0 (effects)
        diffs += compareRange(what, tick, 0xa743ec, 4, after);                                  // pool 10 live mask
        diffs += compareRange(what, tick, 0xa71120, 8, after);                                  // pool a0 live masks
        for (uint32_t i = 0; i < 24; ++i) { // pool 20 (enemies): skip the stack-garbage tail of +0x74
            const uint32_t a = 0xa6e3a0 + i * 0x16c;
            diffs += compareRange(what, tick, a, 0x16c, after, [](uint32_t o) { return o >= 0x86 && o < 0x94; });
        }
        return diffs;
    }
    int compareSound(const char* what, uint32_t tick, const Rec& after) {
        int d = compareRange(what, tick, 0xa676c0, 0x180, after);
        d += compareRange(what, tick, 0xa7223c, 4, after);
        d += compareRange(what, tick, 0xa6d33c, 4, after);
        return d;
    }
    int comparePlayer(const char* what, uint32_t tick, const Rec& after) {
        return compareRange(what, tick, 0xa71d20, 0x2a4, after, [](uint32_t o) { return o == 0x86 || o == 0x87; });
    }
    int compareHitList(const char* what, uint32_t tick, const Rec& after) {
        int d = compareRange(what, tick, 0xa7a184, 4, after);
        d += compareRange(what, tick, 0xa7a1a0, m.u32(0xa7a184) * 0x18, after);
        return d;
    }
    // FUN_00574860 (points 1 -> 2): player input + state dispatch -- the buster spawns its shot here
    bool playerPhase(const Rec& before, const Rec& after) {
        load(before);
        int diffs = 0;
        try {
            od::port::playerInputAndDispatch(env);
        } catch (const std::exception& e) {
            if (reported < maxReports) std::fprintf(stderr, "  tick %u player: %s\n", before.tick, e.what());
            return false;
        }
        od::test::storeNativeState(m);
        diffs += comparePlayer("player", before.tick, after);
        diffs += compareSound("player sound queue", before.tick, after);
        diffs += compareRange("player rng", before.tick, 0x6d0000, 4, after);
        diffs += compareRange("player shots", before.tick, 0xa6ab80, 0x10 * 0x100, after);
        diffs += compareRange("player shots", before.tick, 0xa743ec, 4, after);
        diffs += compareRange("movers", before.tick, 0xa5f5a0, m.u32(0xa743e4) - 0xa5f5a0, after);
        ++compared;
        return diffs == 0;
    }
    // FUN_004140e0 + Maybe_UpdatePlayer (points 6 -> 7): actor collision pass, ground / pose, the player's hit box
    bool playerUpdatePhase(const Rec& before, const Rec& after) {
        load(before);
        int diffs = 0;
        try {
            od::port::actorCollision();
            od::port::playerUpdate(env);
        } catch (const std::exception& e) {
            if (reported < maxReports) std::fprintf(stderr, "  tick %u player update: %s\n", before.tick, e.what());
            return false;
        }
        od::test::storeNativeState(m);
        diffs += comparePlayer("player update", before.tick, after);
        diffs += compareSound("player update sound queue", before.tick, after);
        diffs += compareHitList("player update hit list", before.tick, after);
        ++compared;
        return diffs == 0;
    }
    // FUN_005e4260 (points 13 -> 14): lock-on target FUN_005e4b60 and the pair tests FUN_005e4350
    bool hitListPhase(const Rec& before, const Rec& after) {
        load(before);
        int diffs = 0;
        try {
            od::port::hitListPass();
        } catch (const std::exception& e) {
            if (reported < maxReports) std::fprintf(stderr, "  tick %u hit list: %s\n", before.tick, e.what());
            return false;
        }
        od::test::storeNativeState(m);
        diffs += comparePlayer("hit list: player", before.tick, after);
        diffs += comparePools("hit list: actors", before.tick, after);
        diffs += compareRange("hit list: lock-on", before.tick, 0xa6d1cb, 1, after);
        diffs += compareRange("hit list: count", before.tick, 0xa7a184, 4, after);
        if (m.u8(0xa71dec) != 0 && (m.s8(0xa71df0) == 8 || m.s8(0xa71df0) < 1)) {
            std::vector<od::fx::Prim> prims;
            od::fx::State st;
            od::fx::TextureCache tex;
            od::fx::collect(st, tex, prims);
            int n = 0;
            for (const od::fx::Prim& pr : prims) n += pr.object == 0x50000u;
            if (m.s8(0xa71df0) == 8) {
                ++reticleIdle;
                reticleIdleDrawn += n != 0;
            } else {
                ++reticleLocked;
                reticleLockedDrawn += n != 0;
            }
        }
        ++compared;
        return diffs == 0;
    }
    // Closed loop (combat goldens): load the state before tick 1's first pool loop, finish that tick with the ported
    // frame body, then run whole ported game frames (od::port::gameTick: field mode FUN_005413a0 -- RNG step, player
    // flags, events, scripts -- with the frame body FUN_00542280 as its hook, then FUN_005e15f0), injecting only the
    // pad (0xa6bec0, from each tick's first record). Returns the number of ticks after which the player struct, the
    // pool-0x20 actors of ported classes and the shots still equal the game's (after FUN_005e4260).
    uint32_t closedLoop(const Golden& gd, const std::map<std::pair<uint32_t, uint8_t>, size_t>& at, uint32_t ticks) {
        load(gd.at(at.at({1, 0})));
        od::port::ActorContext lc;
        const int savedReported = reported;
        reported = std::getenv("OD_LOOP_DEBUG") ? 0 : maxReports; // quiet unless OD_LOOP_DEBUG
        uint32_t good = 0;
        std::vector<uint32_t> tracked; // pool-0x20 actors of ported classes at the start
        for (uint32_t i = 0; i < 24; ++i) {
            const od::RoutineRef h = od::port::actorHandler(od::port::actorHeaderAt(0xa6e3a0 + i * 0x16c), ActorPool::Animated);
            if (h && od::port::portedActorClass(h.id())) tracked.push_back(0xa6e3a0 + i * 0x16c);
        }
        bool fromE0 = true; // tick 1 starts at the first pool loop
        od::port::TransitionHooks hooks;
        od::port::FadeTask fade;
        hooks.frame = [&] { // the frame body after the script runner, as in the app (main.cpp PlayerView::setupHooks)
            if (fromE0) fromE0 = false;
            od::port::updatePool(env, lc, ActorPool::Controllers);
            od::port::playerInputAndDispatch(env);
            od::port::updatePool(env, lc, ActorPool::Animated);
            od::port::updatePool(env, lc, ActorPool::Projectiles);
            od::port::updatePool(env, lc, ActorPool::Effects);
            od::port::updatePool(env, lc, ActorPool::Statics);
            od::port::actorCollision();
            od::port::playerUpdate(env);
            od::port::updatePool(env, lc, ActorPool::Shots);
            od::port::cameraUpdate();
            od::port::updatePool(env, lc, ActorPool::EffectsLate);
            od::port::updatePool(env, lc, ActorPool::StaticsLate);
            od::port::updatePool(env, lc, ActorPool::ControllersLate);
            od::port::hitListPass();
        };
        hooks.camera = [&] { od::port::cameraUpdate(); };
        for (uint32_t t = 1; t <= ticks; ++t) {
            const auto p0 = at.find({t, 0}), p14 = at.find({t, 14});
            if (p0 == at.end() || p14 == at.end()) break;
            od::test::storeNativeState(m);
            size_t o = 0; // the pad of this tick
            for (const Range& rg : gd.ranges) {
                if (rg.addr == 0xa6bec0) m.write(rg.addr, gd.at(p0->second).blob.data() + o, rg.size);
                o += rg.size;
            }
            od::test::loadNativeState(m);
            try {
                if (t == 1) {
                    hooks.frame();
                } else {
                    od::port::gameTick(env, hooks, fade);
                }
            } catch (const std::exception& e) {
                if (std::getenv("OD_LOOP_DEBUG")) std::fprintf(stderr, "  loop tick %u: %s\n", t, e.what());
                break;
            }
            od::test::storeNativeState(m);
            const Rec& w14 = gd.at(p14->second);
            int d = comparePlayer("loop", t, w14);
            d += compareRange("loop shots", t, 0xa6ab80, 0x10 * 0x100, w14);
            for (const uint32_t a : tracked) // the actors of ported classes (the others stay frozen here)
                d += compareRange("loop pool 20", t, a, 0x16c, w14, [](uint32_t off) { return off >= 0x86 && off < 0x94; });
            if (d != 0) break;
            good = t;
            if (t == 1) od::port::applyActorFrees(); // FUN_005e15f0 (gameTick does it from tick 2 on)
        }
        reported = savedReported;
        return good;
    }
};

} // namespace

int main(int argc, char** argv) {
    od::content::registerAll(); // the game code dispatches into the content through its registry
    if (argc < 4) return 2;
    const fs::path datDir = argv[1], exe = argv[2], data = argv[3];
    od::bindTestDataSource(exe);
    od::port::setModernPlay(false); // the goldens hold the exe's gameplay (game/modern_play)
    const int maxReports = argc > 4 ? std::stoi(argv[4]) : 3;
    const std::string only = argc > 5 ? argv[5] : ""; // optional: only goldens whose name contains this
    std::vector<fs::path> files;
    for (const auto& de : fs::directory_iterator(data))
        if (de.path().extension() == ".bin" && de.path().filename().string().rfind("act_", 0) == 0 &&
            de.path().filename().string().find(only) != std::string::npos)
            files.push_back(de.path());
    std::sort(files.begin(), files.end());
    CHECK(!files.empty());
    for (const fs::path& path : files) {
        od::GameMem m;
        const od::ScopedGameMemory bound(m);
        m.mapExeData(exe);
        Golden g;
        CHECK(readGolden(path, m, g));
        od::test::loadNativeState(m); // the native state from this golden's image (nothing of the previous one)
        Runner run{m, g, datDir, maxReports};
        run.psxImage = g.psx;
        std::map<std::pair<uint32_t, uint8_t>, size_t> at;
        for (size_t i = 0; i < g.recs.size(); ++i) at[{g.recs[i].tick, g.recs[i].point}] = i;
        od::port::ActorContext ctx;
        if (std::getenv("OD_ACT_LOG")) ctx.log = [](const std::string& msg) { std::fprintf(stderr, "  %s\n", msg.c_str()); };
        std::map<std::string, int> phaseTicks;
        int failed = 0, passed = 0;
        uint32_t ticks = 0;
        for (const auto& r : g.recs) ticks = std::max(ticks, r.tick);
        for (uint32_t t = 1; t <= ticks; ++t) {
            bool ok = true;
            auto step = [&](uint8_t pb, uint8_t pa, const char* name, auto fn) {
                const auto b = at.find({t, pb}), a = at.find({t, pa});
                if (b == at.end() || a == at.end()) return;
                const int before = run.compared;
                const Rec recBefore = g.copy(b->second); // the reader decodes one record at a time
                ok = fn(recBefore, g.at(a->second)) && ok;
                if (run.compared != before) ++phaseTicks[name];
            };
            for (const Phase& ph : kPhases) {
                if (g.combat && ph.pool == ActorPool::Shots) // in frame order: the player update comes before pool 10
                    step(6, 7, "player update", [&](const Rec& b, const Rec& a) { return run.playerUpdatePhase(b, a); });
                step(ph.before, ph.after, ph.name,
                     [&](const Rec& b, const Rec& a) { return run.phase(ph, b, a, ctx); });
                if (g.combat && ph.pool == ActorPool::Controllers)
                    step(1, 2, "player", [&](const Rec& b, const Rec& a) { return run.playerPhase(b, a); });
            }
            if (g.combat) step(13, 14, "hit list", [&](const Rec& b, const Rec& a) { return run.hitListPhase(b, a); });
            if (!ok) {
                ++failed;
                ++run.reported;
            } else {
                ++passed;
            }
        }
        std::printf("%s: %u ticks: %d ok, %d failing; %d ported actor updates compared (", path.filename().string().c_str(),
                    ticks, passed, failed, run.compared);
        for (const auto& [name, n] : phaseTicks) std::printf(" %s %d ticks", name.c_str(), n);
        std::printf(" )\n");
        CHECK(failed == 0);
        if (g.combat) { // closed loop: ported game frames only, from the first record
            const auto first = at.find({1, 0});
            if (first != at.end()) {
                const uint32_t chained = run.closedLoop(g, at, ticks);
                std::printf("  closed loop (ported game frames): player, enemies and shots bit-exact for %u of %u ticks\n",
                            chained, ticks);
                // ST13: no unported actor in the room -- the whole fight must chain
                if (path.filename().string().rfind("act_ST13_", 0) == 0) CHECK(chained == ticks);
            }
        }
        if (path.filename().string().find("lockproj") != std::string::npos) {
            // recorded with the renderer's projection constants: the selection fires; not locked, the reticle point
            // is the camera eye (+0xd0 = 8) and the depth test rejects it; locked, it is drawn at the target
            std::printf("  reticle: target, not locked %d ticks (%d drawn); locked %d ticks (%d drawn)\n", run.reticleIdle,
                        run.reticleIdleDrawn, run.reticleLocked, run.reticleLockedDrawn);
            CHECK(run.reticleIdle > 0 && run.reticleIdleDrawn == 0);
            CHECK(run.reticleLocked > 0 && run.reticleLockedDrawn > 0);
        }
        CHECK(run.compared > 0);
    }
    return testResult("test_actors");
}
