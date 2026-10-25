// ST31 sub 0 (Glyde's base, the box room): the three walkers of the room's 7 x 7 walk grid (pool 0x20 type 0x38, subs
// 0..2, spawned by the room handler 0x585f80) and the rolling bomb they throw. One routine per exe routine; the names
// are hypotheses from the observed behaviour.
//   0x4accc0  pool 0x20 type 0x38: states [0x894108] (init, run, death); the run's sub-states [0x894130] (think, path
//             walks, turns, hurt, box throw, route walk, charge, back to a cell centre, busy, gunfire); the actions
//             [0x894118] run beside them (idle / walk / stand / swing / bomb throw)
//   0x5c8420  pool 0x40 ST31 stage type 0x16: the bomb (an arc, then it rolls)
// The walk grid: a byte per cell, bits 0x80 >> walker the walkers' cells (a walker's bit is cleared at the top of
// its tick and set again after its sub-state), & 3 the boxes' cells. Render output left out: the shadows (exe
// 0x5f20f0). The exe's stack vectors whose 16.16 fractions it leaves uninitialised (the box throw, the way back to a
// cell centre) get zero fractions here.
#include "content/nino/glyde_box_walkers.hpp"
#include "content/nino/glyde_box_walkers_state.hpp"

#include "content/calbania/calbania_rooms.hpp"
#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/collision.hpp"
#include "game/cutscene_actor.hpp"
#include "game/distances.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/lift_box.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"
#include "game/visibility.hpp"

using namespace od::port::glyde_box_walkers_state;

namespace od::port {

using namespace glyde_box_walkers_state;

namespace {

using content::glydeBaseRoomState;

constexpr int kGridSize = 7;
constexpr uint8_t kGridBoxes = 3; // a cell's box bits
// the cell of a coordinate (x / z, integer parts)
int cellOfX(int x) { return (x + 0xc00) >> 10; }
int cellOfZ(int z) { return (z + 0xe00) >> 10; }
uint8_t& gridCell(int index) { return glydeBaseRoomState().walkGrid[size_t(index)]; }

constexpr int kSearchDepths = 8;
// the target's cell as one value (x << 8 + z)
uint32_t targetCell() { return (uint32_t(search().targetX) << 8) + uint32_t(search().targetZ); }

// The walker's spawn parameter: its index (its grid bit 0x80 >> index, its route)
struct WalkerParams {
    explicit WalkerParams(const AnimatedActor& actor) : WalkerParams(const_cast<AnimatedActor&>(actor)) {}
    explicit WalkerParams(AnimatedActor& actor) : index(actor.params[3]) {}
    uint8_t& index;
};
constexpr uint8_t kWalkerNoHitEntry = 0x01;
constexpr uint8_t kWalkerNoDamage = 0x02;
constexpr uint8_t kWalkerHoldAim = 0x08;  // the gunfire's aim kept (the animation not posed this tick)
constexpr uint8_t kWalkerCarrying = 0x10; // a box between its hands
constexpr uint8_t kWalkerBusy = 0x20;     // an action runs that the thinking waits for
constexpr uint8_t kWalkerDying = 0x80;
// The walker's stats row: its +8 is its hit radius
struct WalkerStats : AnimatedActorStats {
    static constexpr uint32_t kBytes = 0xd;
    uint8_t hitRadiusBytes = 0; // +0x8
    template <class R> static WalkerStats load(const R& r) {
        WalkerStats v;
        static_cast<AnimatedActorStats&>(v) = AnimatedActorStats::load(r);
        v.hitRadiusBytes = r.template read<uint8_t>(0x8);
        return v;
    }
};
WalkerStats walkerStats(const AnimatedActor& actor) { return dataRecord<WalkerStats>(animatedStatRow(actor.type), WalkerStats::kBytes); }

enum class WalkerState : uint8_t { Init = 0, Run = 1, Death = 2 };
enum class WalkerSub : uint8_t {
    Think = 0,      // stand, every 0x10 ticks plan the path to the target
    Walk = 1,       // walk the path cell by cell
    BoxAhead = 2,   // a box on the target's cell: turn to it, wait, then gunfire
    Turn = 3,       // turn to the heading, then run
    Run = 4,        // run the path cell by cell
    Stop = 5,       // a stop after the run
    Hurt = 6,
    ThrowBox = 7,
    Route = 8,      // the first walker's route
    Charge = 9,
    ToCentre = 0xa, // back to the nearest cell centre
    Busy = 0xb,     // an action (the bomb throw) runs
    Gunfire = 0xc,
};
enum class WalkerAction : uint8_t { None = 0, Idle = 1, Walk = 2, Stand = 3, Swing = 4, Bomb = 5 };
enum class DeathPhase : uint8_t { Fall = 0, Down = 1, Explode = 2, Fade = 3 };
// the first step's direction of a path (1 z - 1, 2 x - 1, 3 z + 1, 4 x + 1)
enum PathDir : int { kNoPath = 0, kDirZMinus = 1, kDirXMinus = 2, kDirZPlus = 3, kDirXPlus = 4 };

// A gun shot's spawn parameters (the stage's pool-0x40 type 9; hypothesis: its attack and its life)
struct GunShotParams {
    explicit GunShotParams(const ProjectileActor& actor) : GunShotParams(const_cast<ProjectileActor&>(actor)) {}
    explicit GunShotParams(ProjectileActor& actor) : attack(actor.params, 0), life(actor.params, 2) {}
    BytesField<uint16_t, 4> attack;
    BytesField<uint16_t, 4> life;
};
const DataTable<ScalarTable<uint8_t, 3>> walkerModels{TableId::GlydeBoxWalkersWalkerModels};
// the routes (exe data: per walker 16 bytes, two halves of 4 (x, z) cells; the half by the box bit of the cell
// (3, 1))
const DataTable<ScalarTable<uint8_t, 0x30>> walkerRoutes{TableId::GlydeBoxWalkersWalkerRoutes};
constexpr int kRouteHalfCell = 10;
const DataTable<CollisionBoxRecord> walkerBox{TableId::GlydeBoxWalkersWalkerBox};
constexpr int16_t kWalkerScale = 0x300;
constexpr uint8_t kWreckType = 0x2d;
constexpr uint8_t kGunShotType = 9, kGunShotVariant = 0x85, kBombType = 0x16;
constexpr uint16_t kTintDeathBlink = 0x4214;
constexpr uint32_t kHitStrongOrLevel = kHitLevelAbove | kHitLevelWithin | kHitLevelBelow | kHitAttackStrongPush |
                                       kHitAttackThrown;

int16_t t16(int v) { return int16_t(uint16_t(uint32_t(v))); }
int cellIndex(const WalkerFields& w) { return int(w.cellZ) * kGridSize + int(w.cellX); }
uint16_t cellWord(const WalkerFields& w) { return uint16_t(t16(cellIndex(w))); }
uint8_t gridBit(const AnimatedActor& actor) { return uint8_t(0x80u >> WalkerParams(actor).index); }
void gridClear(const AnimatedActor& actor, const WalkerFields& w) {
    uint8_t& cell = gridCell(cellIndex(w));
    cell = uint8_t(cell & uint8_t(0xffu - gridBit(actor)));
}
void gridSet(const AnimatedActor& actor, const WalkerFields& w) {
    uint8_t& cell = gridCell(cellIndex(w));
    cell = uint8_t(cell | gridBit(actor));
}
void sound(uint16_t id, const AnimatedActor& actor) { soundPlayAtPos(id, actor.pos); } // exe 0x5d5380
// the route's cell k of the walker (its half by the box bit of the cell (3, 1))
size_t routeCell(const AnimatedActor& actor, uint32_t k) {
    return size_t(int32_t(int8_t(WalkerParams(actor).index)) * 16) + ((gridCell(kRouteHalfCell) & 1) ? 0u : 8u) +
           k * 2;
}

// ---- the grid ------------------------------------------------------------------------------------------------------
// exe 0x4af0b0: outside the grid or occupied -> 1
int cellBlocked(int x, int z) {
    if (x < 0 || x > 6 || z < 0 || z > 6) return 1;
    return gridCell(z * kGridSize + x) != 0 ? 1 : 0;
}
// exe 0x4af2a0(x, z, depth, dir): the depth-first search for the target's cell; the first step's direction; at depth 0
// the target's cell itself: -dir when a box is there
int pathSearch(int x, int z, int depth, int dir) {
    if (depth == 0) {
        if (x < 0 || x > 6 || z < 0 || z > 6) return 0;
        if (x != search().targetX || z != search().targetZ) return 0;
        const uint8_t g = gridCell(z * kGridSize + x);
        if (g & kGridBoxes) return -dir;
        return g != 0 ? 0 : dir;
    }
    if (cellBlocked(x, z) != 0) return 0;
    --depth;
    if (dir != kDirZPlus && pathSearch(x, z - 1, depth, kDirZMinus) != 0) return kDirZMinus;
    if (dir != kDirXPlus && pathSearch(x - 1, z, depth, kDirXMinus) != 0) return kDirXMinus;
    if (dir != kDirZMinus && pathSearch(x, z + 1, depth, kDirZPlus) != 0) return kDirZPlus;
    if (dir != kDirXMinus && pathSearch(x + 1, z, depth, kDirXPlus) != 0) return kDirXPlus;
    return kNoPath;
}
// exe 0x4af180(actor, x, z): the depths 0..7 (the depth used kept, 8 = none)
int pathFind(int x, int z) {
    int d = 0;
    for (; d < kSearchDepths; ++d) {
        int r = pathSearch(x, z - 1, d, kDirZMinus);
        if (r != 0) {
            search().depth = int16_t(d);
            return r > 0 ? kDirZMinus : -kDirZMinus;
        }
        r = pathSearch(x - 1, z, d, kDirXMinus);
        if (r != 0) {
            search().depth = int16_t(d);
            return r > 0 ? kDirXMinus : -kDirXMinus;
        }
        r = pathSearch(x, z + 1, d, kDirZPlus);
        if (r != 0) {
            search().depth = int16_t(d);
            return r > 0 ? kDirZPlus : -kDirZPlus;
        }
        r = pathSearch(x + 1, z, d, kDirXPlus);
        if (r != 0) {
            search().depth = int16_t(d);
            return r > 0 ? kDirXPlus : -kDirXPlus;
        }
    }
    search().depth = int16_t(d);
    return kNoPath;
}
int16_t headingOf(int dir) { return int16_t((dir << 10) - 0x400); }
// exe 0x4af0f0(actor, x, z): -1 at the target's cell; no path -> think (0); a box on the target -> think (busy) or the
// box ahead (0); else 1. The heading = the first step's direction.
int pathHeading(AnimatedActor& actor, WalkerFields& w, int x, int z) {
    if (x == search().targetX && z == search().targetZ) return -1;
    int r = pathFind(x, z);
    int ret = 0;
    if (r == 0) {
        actor.subState = uint8_t(WalkerSub::Think);
        actor.restartStep();
        return 0;
    }
    if (r < 0) {
        if (w.flags & kWalkerBusy) {
            actor.subState = uint8_t(WalkerSub::Think);
            actor.restartStep();
        } else {
            actor.subState = uint8_t(WalkerSub::BoxAhead);
            w.setAction(uint8_t(WalkerAction::None));
            actor.restartStep();
        }
        r = -r;
    } else {
        ret = 1;
    }
    w.heading = headingOf(r);
    return ret;
}
// exe 0x4af3e0: as exe 0x4af0f0 without the sub-state changes
int pathHeadingOnly(WalkerFields& w, int x, int z) {
    if (x == search().targetX && z == search().targetZ) return -1;
    const int r = pathFind(x, z);
    if (r == 0) return 0;
    if (r < 0) {
        w.heading = headingOf(-r);
        return 0;
    }
    w.heading = headingOf(r);
    return 1;
}
// exe 0x4af4e0(actor, yaw, px, pz): the cells from the walker's to (px, pz) along the yaw's axis all free -> the cell
// distance, else -1 (also when not on the same row / column or behind)
int lineClear(const WalkerFields& w, uint32_t yaw, int px, int pz) {
    const int cx = cellOfX(px), cz = cellOfZ(pz);
    if (yaw & 0x400) {
        if (cz != w.cellZ) return -1;
        int c = w.cellX;
        const int dist = cx - c < 0 ? c - cx : cx - c;
        if (yaw & 0x800) {
            if (c > cx) return -1;
            for (; c <= cx; ++c)
                if (gridCell(cz * kGridSize + c) != 0) return -1;
        } else {
            if (c < cx) return -1;
            for (; c >= cx; --c)
                if (gridCell(cz * kGridSize + c) != 0) return -1;
        }
        return dist;
    }
    if (cx != w.cellX) return -1;
    int c = w.cellZ;
    const int dist = cz - c < 0 ? c - cz : cz - c;
    if (yaw & 0x800) {
        if (c > cz) return -1;
        for (; c <= cz; ++c)
            if (gridCell(c * kGridSize + cx) != 0) return -1;
    } else {
        if (c < cz) return -1;
        for (; c >= cz; --c)
            if (gridCell(c * kGridSize + cx) != 0) return -1;
    }
    return dist;
}
// exe 0x4af4a0(actor, x, y, z): moved by (0, 0, z) along the yaw (exe 0x5e3690), then its grid cell
void moveAhead(AnimatedActor& actor, WalkerFields& w, int z) {
    integrateVelocity(actor, 0, 0, z);
    w.cellX = int8_t(cellOfX(actor.pos.x.whole));
    w.cellZ = int8_t(cellOfZ(actor.pos.z.whole));
}
// exe 0x4af450(actor, x, y, target): the speed toward the target by target / 8 (clamped), then moveAhead
void accel(AnimatedActor& actor, WalkerFields& w, int target) {
    int v = actor.velocity.z;
    if (target < 0) {
        if (v > target) {
            v += target >> 3;
            if (v < target) v = target;
        } else if (v < target) {
            v = target;
        }
    } else {
        if (v < target) {
            v += target >> 3;
            if (v > target) v = target;
        } else if (v > target) {
            v = target;
        }
    }
    actor.velocity.z = t16(v);
    moveAhead(actor, w, t16(v));
}
// the speed turned back and the walker moved by it (a walker met an occupied cell)
void bounceBack(AnimatedActor& actor, WalkerFields& w) {
    const int16_t v = t16(-int(actor.velocity.z));
    actor.velocity.z = v;
    moveAhead(actor, w, v);
}
// exe 0x4af710: x and z within 0x20 of a cell centre
bool nearCentre(const AnimatedActor& actor) {
    const uint32_t dz = uint16_t(actor.pos.z.whole) & 0x3ffu;
    const uint32_t dx = (uint32_t(uint16_t(actor.pos.x.whole)) - 0x200u) & 0x3ffu;
    if (dz < 0x3e0 && dz > 0x20) return false;
    return dx >= 0x3e0 || dx <= 0x20;
}
// exe 0x4af750: the coordinate across the heading snapped into the cell centre's band
void snapCentre(AnimatedActor& actor) {
    if (uint16_t(actor.rotation.y) & 0x400) {
        const uint32_t z = uint16_t(actor.pos.z.whole), e = z & 0x3ff;
        if (e > 0x200) {
            if (e < 0x3e0) actor.pos.z.whole = int16_t(uint16_t(z - e + 0x3e0));
        } else if (e < 0x200 && e > 0x20) {
            actor.pos.z.whole = int16_t(uint16_t(z + (0x20 - e)));
        }
        return;
    }
    const uint32_t x = uint16_t(actor.pos.x.whole), e = (x - 0x200) & 0x3ff;
    if (e > 0x200) {
        if (e < 0x3e0) actor.pos.x.whole = int16_t(uint16_t(x - e + 0x3e0));
    } else if (e < 0x200 && e > 0x20) {
        actor.pos.x.whole = int16_t(uint16_t(x + (0x20 - e)));
    }
}
// exe 0x4af690: not busy and none of its route's first 3 cells within 2 cells
bool offRoute(const AnimatedActor& actor, const WalkerFields& w) {
    if (w.flags & kWalkerBusy) return false;
    const int cx = w.cellX, cz = w.cellZ;
    for (uint32_t k = 0; k < 3; ++k) {
        const size_t e = routeCell(actor, k);
        const int dx = int(walkerRoutes[e]) - cx, dz = int(walkerRoutes[e + 1]) - cz;
        if ((dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz) <= 2) return false;
    }
    return true;
}
void routeWalk(AnimatedActor& actor, WalkerFields& w); // exe 0x4ade00
// exe 0x4af620: at a cell centre, the first walker off its route -> its route (no target; 1); else the target = the
// player
int chooseTarget(AnimatedActor& actor, WalkerFields& w) {
    if (!nearCentre(actor)) {
        w.target = kPlayer;
        return 0;
    }
    if (w.flags & kWalkerBusy) return 0;
    if (actor.variant == 0 && offRoute(actor, w)) {
        actor.subState = uint8_t(WalkerSub::Route);
        w.target = 0;
        routeWalk(actor, w);
        return 1;
    }
    w.target = kPlayer;
    return 0;
}
// exe 0x4ae850(actor, block): a free box in line (ahead, left or right) at most one cell away -> its throw
int boxCheck(AnimatedActor& actor, WalkerFields& w) {
    if (w.flags & kWalkerBusy) return 0;
    auto&& boxes = glydeBaseRoomState().boxes;
    for (size_t i = 0; i < boxes.size(); ++i) {
        const uint32_t address = boxes[i];
        if (!address) notPorted(kStoryPort, "0x4ae850: null box slot (the exe reads through it)");
        const Actor& box = actorLayoutAt(address);
        if (box.pos.x.whole > 0xa00 && box.pos.z.whole < -0x800) continue;
        if (!(box.flags & kActorLive) || !LiftBoxHolding(actorHeaderAt(address)).unheld()) continue;
        if (actor.pos.y.whole != box.pos.y.whole) continue;
        const int bx = box.pos.x.whole, bz = box.pos.z.whole;
        if (gridCell(cellOfZ(bz) * kGridSize + cellOfX(bx)) != 0) continue;
        const uint32_t yaw = uint16_t(actor.rotation.y);
        bool inLine = false;
        for (uint32_t y : {yaw, (yaw + 0x400) & 0xfff, (yaw - 0x400) & 0xfff}) {
            const int r = lineClear(w, y, bx, bz);
            if (r >= 0 && r <= 1) {
                inLine = true;
                break;
            }
        }
        if (!inLine) continue;
        actor.subState = uint8_t(WalkerSub::ThrowBox);
        actor.restartStep();
        w.target = address;
        w.setAction(uint8_t(WalkerAction::None));
        return 1;
    }
    return 0;
}
// exe 0x4ae7c0(actor, block): a box -> its throw; else (not busy, no box on the target's cell) the target near
// (<= 0x300) -> the swing; the others' walkers, < 0x500 and at a cell centre -> the bomb throw
void think(AnimatedActor& actor, WalkerFields& w) {
    if (boxCheck(actor, w)) return;
    if (w.flags & kWalkerBusy) return;
    if (gridCell(int(search().targetZ) * kGridSize + int(search().targetX)) & kGridBoxes) return;
    const int d = distance3(actor.pos, actorLayoutAt(w.target).pos); // exe 0x5e3a10
    if (d <= 0x300) {
        w.setAction(uint8_t(WalkerAction::Swing));
        return;
    }
    if (actor.variant != 0 && d < 0x500 && nearCentre(actor)) {
        w.setAction(uint8_t(WalkerAction::Bomb));
        actor.subState = uint8_t(WalkerSub::Busy);
        actor.restartStep();
    }
}
// exe 0x4aed70(actor, angle): the head's yaw toward angle - yaw by 0x40 a tick; 0 = reached
int headTurn(const AnimatedActor& actor, WalkerFields& w, int angle) {
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angle)) - w.headYaw;
    const uint16_t h = uint16_t(w.headYaw);
    if (d >= 0) {
        if (d > 0x40) {
            w.headYaw = int16_t(uint16_t(h + 0x40));
            return 1;
        }
    } else if (d < -0x40) {
        w.headYaw = int16_t(uint16_t(h - 0x40));
        return 1;
    }
    w.headYaw = int16_t(uint16_t(h + uint32_t(d)));
    return 0;
}
// the head turned toward the player (exe 0x4aed70 with the angle from the player to the walker)
int headTurnToPlayer(const AnimatedActor& actor, WalkerFields& w) {
    return headTurn(actor, w, angleOf(player().pos, actor.pos));
}
// exe 0x4ae600: the guns' pitch and spread from the player's distance and height
void aim(const AnimatedActor& actor, WalkerFields& w) {
    int d = planarDistance(actor.pos, player().pos) - 0xc8;
    if (d < 0) d = 1;
    const int16_t dy = int16_t(uint16_t(0xff42u - uint16_t(player().pos.y.whole)));
    const int p = atanAngle(float(t16(d)), float(dy)) & 0xfff; // exe 0x5e3c50({0, 0}, {d, dy})
    w.gunPitch = int16_t(angleDiff(0x400, uint32_t(p)));
    const int q = atanAngle(float(-0x8c), float(t16(d))) & 0xfff; // exe 0x5e3c50({0x8c, 0}, {0, d})
    const int r = angleDiff(0, uint32_t(q));
    w.spreadLeft = int16_t(r);
    w.spreadRight = int16_t(-r);
    w.gunPitch = int16_t(uint16_t(w.gunPitch) + uint16_t(r >> 1));
}
// exe 0x4aefd0: the damage of its hit box -> a flash, the hurt sound (every 0x5a ticks), a carried box let go, strong
// / level hits -> hurt; the hit points (none: hurt, -1)
int damage(AnimatedActor& actor, WalkerFields& w) {
    const uint32_t v = w.hit.result;
    if (w.hurtSoundTicks != 0) w.hurtSoundTicks -= 1u;
    const uint32_t dmg = v & kHitDamageMask;
    if (dmg == 0) return 0;
    if (w.hurtSoundTicks == 0) {
        w.hurtSoundTicks = 0x5a;
        sound(0x1bf, actor);
    }
    if (w.flags & kWalkerCarrying) {
        Actor& box = actorLayoutAt(w.target);
        box.velocity.x = 0;
        box.velocity.y = 0;
        box.velocity.z = 0;
        LiftBoxHolding(box).holder = kLiftBoxLetGo;
        w.flags &= uint8_t(~kWalkerCarrying);
    }
    actor.tint = kTintFlash;
    if (v & kHitStrongOrLevel) {
        actor.subState = uint8_t(WalkerSub::Hurt);
        actor.restartStep();
        w.setAction(uint8_t(WalkerAction::None));
    }
    if ((v & (kHitAttackStrongPush | kHitAttackByPlayer)) && w.target == 0u) w.target = kPlayer;
    if (hitPointsAdd(actor, -int(dmg)) != 0) {
        actor.subState = uint8_t(WalkerSub::Hurt);
        actor.restartStep();
        w.setAction(uint8_t(WalkerAction::None));
        return -1;
    }
    return 0;
}
// the footsteps: a step sound every 0x18 ticks
void footsteps(const AnimatedActor& actor, WalkerFields& w) {
    const uint16_t v = w.footstepTicks;
    w.footstepTicks = uint16_t(v - 1u);
    if (v == 0) {
        w.footstepTicks = 0x17;
        sound(0x1b3, actor);
    }
}
// a sub-state's first tick: step 1, the neutral tint
void beginSub(AnimatedActor& actor) {
    actor.enterStep(1);
    actor.tint = kTintNeutral;
}
void enterSub(AnimatedActor& actor, WalkerSub sub) {
    actor.subState = uint8_t(sub);
    actor.restartStep();
}

// ---- the sub-states [0x894130] ------------------------------------------------------------------------------------
// exe 0x4ad130: stand facing the heading; every 0x10 ticks plan the path to the target
void thinkSub(AnimatedActor& actor, WalkerFields& w) {
    if (actor.step == 0) {
        if (chooseTarget(actor, w)) return;
        w.flags &= kWalkerBusy;
        beginSub(actor);
        actor.velocity.z = 0;
        w.spreadLeft = 0;
        w.spreadRight = 0;
        w.gunPitch = 0;
        w.ticks = 5;
        w.heading = -1;
        accel(actor, w, 0);
        w.lastCell = int16_t(cellWord(w));
        if (!(w.flags & kWalkerBusy)) w.setAction(uint8_t(WalkerAction::Idle));
    }
    if (w.heading >= 0) turnYawTo(actor.rotation.y, uint32_t(int32_t(w.heading)), 0x40);
    w.ticks -= 1u;
    if (w.ticks != 0) return;
    if (chooseTarget(actor, w)) return;
    const int r = pathHeading(actor, w, w.cellX, w.cellZ);
    if (r < 0) {
        w.ticks = 0x10;
        w.heading = int16_t((angleOf(actorLayoutAt(w.target).pos, actor.pos) + 0x200) & 0xc00);
        think(actor, w);
        return;
    }
    if (r > 0) {
        enterSub(actor, WalkerSub::Walk);
        think(actor, w);
        return;
    }
    w.ticks = 0x10;
    think(actor, w);
}
// exe 0x4ad270 (walk: speed 0x200) / 0x4ad720 (run: speed 0x300): the path cell by cell
void pathWalk(AnimatedActor& actor, WalkerFields& w, bool run) {
    const int speed = run ? -0x300 : -0x200;
    switch (actor.step) {
    case 0:
        if (run) w.target = kPlayer;
        w.flags &= kWalkerBusy;
        beginSub(actor);
        actor.velocity.z = 0;
        if (run) w.lastCell = int16_t(cellWord(w));
        if (!(w.flags & kWalkerBusy)) w.setAction(uint8_t(run ? WalkerAction::Stand : WalkerAction::Walk));
        [[fallthrough]];
    case 1:
        if (turnYawTo(actor.rotation.y, uint32_t(int32_t(w.heading)), 0x40) != 0) {
            if (run) actor.velocity.z = 0;
            think(actor, w);
            return;
        }
        if (!run) actor.velocity.z = 0;
        actor.step += 1u;
        [[fallthrough]];
    case 2: {
        accel(actor, w, speed);
        const int c = cellIndex(w);
        if (w.lastCell != c) {
            if (gridCell(c) != 0) {
                bounceBack(actor, w);
                actor.enterStep(1);
                w.heading = int16_t(uint16_t(w.heading) ^ 0x800u);
                w.lastCell = int16_t(c);
                if (run) think(actor, w);
                return;
            }
            const int16_t saved = w.heading;
            const int r = pathHeadingOnly(w, w.cellX, w.cellZ);
            if (r > 0) {
                if (((uint16_t(actor.rotation.y) ^ uint16_t(w.heading)) & 0x400) == 0) return;
                actor.step = 3;
                w.lastCell = int16_t(cellWord(w));
                return;
            }
            // the walk writes the result (0 / -1) as the heading, the run restores the saved one
            w.heading = run ? saved : int16_t(r);
            w.lastCell = int16_t(cellWord(w));
        }
        if (!nearCentre(actor)) {
            think(actor, w);
            return;
        }
        if (chooseTarget(actor, w)) return;
        const int r = pathHeading(actor, w, w.cellX, w.cellZ);
        if (r <= 0) {
            enterSub(actor, WalkerSub::Think);
            w.target = kPlayer;
            think(actor, w);
            return;
        }
        const bool far = search().depth >= 4;
        if (far != run) { // the walk keeps walking at a depth >= 4, the run below 4
            actor.step = 1;
            w.lastCell = int16_t(cellWord(w));
            think(actor, w);
            return;
        }
        if (w.flags & kWalkerBusy) {
            enterSub(actor, WalkerSub::Think);
            think(actor, w);
            return;
        }
        actor.subState = uint8_t(run ? WalkerSub::Stop : WalkerSub::Turn);
        w.setAction(uint8_t(WalkerAction::None));
        actor.restartStep();
        think(actor, w);
        return;
    }
    case 3: // a turn onto the next cell's axis
        if (actor.stepCounter == 0) {
            actor.stepCounter = 1;
            actor.velocity.z = int16_t(run ? -0x300 : -0x200);
            w.ticks = int16_t(run ? 0xf : 0x18);
        }
        accel(actor, w, speed);
        turnYawTo(actor.rotation.y, uint32_t(int32_t(w.heading)), run ? 0x40 : 0x28);
        w.ticks -= 1u;
        if (w.ticks == 0) {
            actor.step = 2;
            actor.stepCounter = 0;
            actor.rotation.y = w.heading;
            snapCentre(actor);
        }
        boxCheck(actor, w);
        return;
    default: think(actor, w); return;
    }
}
// exe 0x4ad4f0: turn to the heading (footsteps), animations 0x15 .. 0x17; the target moved -> think, else gunfire
void boxAhead(AnimatedActor& actor, WalkerFields& w) {
    switch (actor.step) {
    case 0:
        w.flags = 0;
        beginSub(actor);
        actor.anim = 1;
        w.setAction(uint8_t(WalkerAction::None));
        w.footstepTicks = 0;
        w.plannedCell = int16_t(targetCell());
        [[fallthrough]];
    case 1:
        if (turnYawTo(actor.rotation.y, uint32_t(int32_t(w.heading)), 0x40) == 0) {
            actor.anim = 0x15;
            actor.step += 1u;
        }
        footsteps(actor, w);
        return;
    case 2:
        if (!actor.animEnded()) return;
        actor.anim = 0x16;
        actor.step = 3;
        w.ticks = 0x28;
        sound(0x1c3, actor);
        return;
    case 3:
        w.ticks -= 1u;
        if (w.ticks != 0) return;
        actor.anim = 0x17;
        actor.step += 1u;
        return;
    case 4:
        if (!actor.animEnded()) return;
        if (int32_t(w.plannedCell) != int32_t(targetCell())) {
            enterSub(actor, WalkerSub::Think);
            return;
        }
        w.setAction(uint8_t(WalkerAction::None));
        enterSub(actor, WalkerSub::Gunfire);
        return;
    default: return;
    }
}
// exe 0x4ad660: turn to the heading (footsteps), animation 0x11 + sound 0x1c1, then the run
void turnSub(AnimatedActor& actor, WalkerFields& w) {
    switch (actor.step) {
    case 0:
        w.flags = 0;
        beginSub(actor);
        actor.anim = 1;
        w.setAction(uint8_t(WalkerAction::None));
        w.footstepTicks = 0;
        [[fallthrough]];
    case 1:
        if (turnYawTo(actor.rotation.y, uint32_t(int32_t(w.heading)), 0x40) == 0) {
            actor.step += 1u;
            actor.anim = 0x11;
            sound(0x1c1, actor);
        }
        footsteps(actor, w);
        return;
    case 2:
        if (actor.animEnded()) enterSub(actor, WalkerSub::Run);
        return;
    default: return;
    }
}
// exe 0x4ad9e0: animations 0x12 / 0x13 (sound 0x1c2, 0x3c ticks) / 0x14, then think
void stopSub(AnimatedActor& actor, WalkerFields& w) {
    switch (actor.step) {
    case 0:
        w.flags = 0;
        beginSub(actor);
        actor.anim = 0x12;
        return;
    case 1:
        if (!actor.animEnded()) return;
        actor.anim = 0x13;
        actor.step = 2;
        w.ticks = 0x3c;
        sound(0x1c2, actor);
        return;
    case 2:
        w.ticks -= 1u;
        if (w.ticks != 0) return;
        actor.anim = 0x14;
        actor.step += 1u;
        return;
    case 3:
        if (actor.animEnded()) enterSub(actor, WalkerSub::Think);
        return;
    default: return;
    }
}
// exe 0x4adaa0: hurt (animation 0xe), the head back -> to a cell centre
void hurtSub(AnimatedActor& actor, WalkerFields& w) {
    switch (actor.step) {
    case 0:
        w.flags = kWalkerNoHitEntry;
        beginSub(actor);
        actor.anim = 0xe;
        return;
    case 1:
        if (actor.animEnded() && headTurn(actor, w, actor.rotation.y) == 0) enterSub(actor, WalkerSub::ToCentre);
        return;
    default: return;
    }
}
// exe 0x4addd0: the box freed / taken -> to a cell centre (1)
int boxGone(AnimatedActor& actor, uint32_t address) {
    if (!(actorHeaderAt(address).flags & kActorLive) || !LiftBoxHolding(actorHeaderAt(address)).unheld()) {
        enterSub(actor, WalkerSub::ToCentre);
        return 1;
    }
    return 0;
}
// exe 0x4adb00: walk to the box, lift it (animation 0x1c), throw it at a random free cell
void throwBox(AnimatedActor& actor, WalkerFields& w) {
    const uint32_t address = w.target;
    switch (actor.step) {
    case 0:
        w.flags = 0;
        beginSub(actor);
        actor.anim = 0;
        actor.velocity.z = 0;
        [[fallthrough]];
    case 1:
        if (boxGone(actor, address)) return;
        if (turnYawTo(actor.rotation.y, uint32_t(angleOf(actorLayoutAt(address).pos, actor.pos)), 0x40) != 0) return;
        actor.step += 1u;
        return;
    case 2:
        if (boxGone(actor, address)) return;
        accel(actor, w, -0x200);
        if (uint32_t(distance3(actor.pos, actorLayoutAt(address).pos)) >= 0x1c0) return;
        actor.anim = 0x1c;
        actor.step += 1u;
        w.ticks = 5;
        return;
    case 3:
        w.ticks -= 1u;
        if (w.ticks != 0) return;
        if (boxGone(actor, address)) return;
        LiftBoxHolding(actorHeaderAt(address)).holder = kLiftBoxHolds;
        actor.step += 1u;
        w.flags |= kWalkerCarrying;
        sound(0x1ba, actor);
        return;
    case 4: {
        if (!actor.animEnded()) return;
        actor.step = 5;
        int x = 0, z = 0;
        for (int k = 0; k < 5; ++k) {
            x = int(actor.pos.x.whole) + 0xc00;
            z = int(actor.pos.z.whole) + 0xe00;
            x = (int(crtRand() & 0x7ff) + x + 0x400) >> 10;
            z = (z - int(crtRand() & 0x7ff) - 0x400) >> 10;
            if (x > 6) x = 6;
            if (z < 0) z = 0;
            if (!(gridCell(z * kGridSize + x) & kGridBoxes)) break;
        }
        // the landing point: integer parts only (the exe leaves the fractions as they are)
        const int32_t to[3] = {int32_t(uint32_t(uint16_t((x << 10) - 0xa00)) << 16), actor.pos.y,
                               int32_t(uint32_t(uint16_t((z - 3) << 10)) << 16)};
        w.heading = int16_t(angleOf(to[0], to[2], actor.pos.x, actor.pos.z));
        Actor& box = actorLayoutAt(address);
        box.velocity.x = 0;
        box.velocity.z = -0x660;
        const int32_t from[3] = {box.pos.x, box.pos.y, box.pos.z};
        box.velocity.y = int16_t(launchSpeed(from, to, 0x660, 0x40, 0xffffffffu));
        return;
    }
    case 5:
        if (turnYawTo(actor.rotation.y, uint32_t(int32_t(w.heading)), 0x40) != 0) return;
        actor.anim = 0x1d;
        actor.step += 1u;
        w.ticks = 8;
        return;
    case 6:
        w.ticks -= 1u;
        if (w.ticks == 0) {
            LiftBoxHolding(actorHeaderAt(address)).holder = kLiftBoxLetGo;
            w.flags &= uint8_t(~kWalkerCarrying);
            sound(0x1bb, actor);
        }
        if (actor.animEnded()) enterSub(actor, WalkerSub::ToCentre);
        return;
    default: return;
    }
}
// exe 0x4ade00: the first walker walks its route (its 4 cells, the step's & 3); the player in a free line ahead -> the
// charge
void routeWalk(AnimatedActor& actor, WalkerFields& w) {
    const size_t e = routeCell(actor, w.routeStep & 3u);
    search().targetX = int32_t(walkerRoutes[e]);
    search().targetZ = int32_t(walkerRoutes[e + 1]);
    switch (actor.step) {
    case 0:
        w.flags = 0;
        accel(actor, w, 0);
        beginSub(actor);
        actor.velocity.z = 0;
        w.setAction(uint8_t(WalkerAction::Walk));
        w.lastCell = int16_t(cellWord(w));
        [[fallthrough]];
    case 1: {
        const int r = pathHeadingOnly(w, w.cellX, w.cellZ);
        if (r == 0) {
            enterSub(actor, WalkerSub::Think);
            return;
        }
        if (r < 0) {
            w.routeStep += 1u;
            return;
        }
        actor.step = 2;
        [[fallthrough]];
    }
    case 2:
        if (turnYawTo(actor.rotation.y, uint32_t(int32_t(w.heading)), 0x40) != 0) {
            actor.velocity.z = 0;
            return;
        }
        actor.step += 1u;
        [[fallthrough]];
    case 3: {
        if (boxCheck(actor, w)) return;
        accel(actor, w, -0x200);
        const int c = cellIndex(w);
        if (w.lastCell != c) {
            if (gridCell(c) != 0) {
                bounceBack(actor, w);
                actor.enterStep(2);
                w.heading = int16_t(uint16_t(w.heading) ^ 0x800u);
                w.lastCell = int16_t(c);
                return;
            }
            const int16_t saved = w.heading;
            const int r = pathHeadingOnly(w, w.cellX, w.cellZ);
            if (r > 0) {
                if (((uint16_t(actor.rotation.y) ^ uint16_t(w.heading)) & 0x400) == 0) return;
                actor.step = 4;
                w.lastCell = int16_t(cellWord(w));
                return;
            }
            w.heading = saved;
            w.lastCell = int16_t(cellWord(w));
        }
        if (!nearCentre(actor)) return;
        int r = lineClear(w, uint16_t(actor.rotation.y), player().pos.x.whole, player().pos.z.whole);
        if (r > 0) {
            enterSub(actor, WalkerSub::Charge);
            w.ticks = int16_t(r);
            return;
        }
        r = pathHeadingOnly(w, w.cellX, w.cellZ);
        if (r == 0) {
            enterSub(actor, WalkerSub::Think);
            return;
        }
        actor.step = 2;
        if (r >= 0) {
            w.lastCell = int16_t(cellWord(w));
            return;
        }
        w.routeStep += 1u;
        return;
    }
    case 4: // a turn onto the next cell's axis
        if (actor.stepCounter == 0) {
            actor.stepCounter = 1;
            actor.velocity.z = -0x200;
            w.ticks = 0x18;
        }
        accel(actor, w, -0x200);
        turnYawTo(actor.rotation.y, uint32_t(int32_t(w.heading)), 0x28);
        w.ticks -= 1u;
        if (w.ticks == 0) {
            actor.step = 3;
            actor.stepCounter = 0;
            actor.rotation.y = w.heading;
            snapCentre(actor);
        }
        boxCheck(actor, w);
        return;
    default: return;
    }
}
// exe 0x4ae0d0: the charge along the line (its cells << 10), sound 0x1bc; an occupied cell or the cell ahead blocked
// -> stop
void charge(AnimatedActor& actor, WalkerFields& w) {
    switch (actor.step) {
    case 0:
        w.flags = 0;
        beginSub(actor);
        actor.anim = 2;
        w.setAction(uint8_t(WalkerAction::None));
        w.ticks = int16_t(uint16_t(uint32_t(uint16_t(w.ticks)) << 10));
        w.lastCell = int16_t(cellWord(w));
        return;
    case 1:
        if (!actor.animEnded()) return;
        actor.enterStep(2);
        actor.anim = 3;
        actor.velocity.z = -0x400;
        w.flags |= kWalkerBusy;
        sound(0x1bc, actor);
        [[fallthrough]];
    case 2: {
        accel(actor, w, -0x400);
        w.ticks = int16_t(uint16_t(w.ticks) + uint16_t(t16(actor.velocity.z >> 4)));
        const int c = cellIndex(w);
        if (w.lastCell != c && gridCell(c) != 0) {
            bounceBack(actor, w);
        } else {
            if (!nearCentre(actor)) return;
            w.lastCell = int16_t(cellWord(w));
            int32_t o[3];
            rotateByYaw(actor.rotation.y, 0, 0, -0x4000, o); // exe 0x5e36f0 on a copy of the position
            const int px = int16_t(uint16_t((uint32_t(actor.pos.x) + uint32_t(o[0])) >> 16));
            const int pz = int16_t(uint16_t((uint32_t(actor.pos.z) + uint32_t(o[2])) >> 16));
            if (cellBlocked(cellOfX(px), cellOfZ(pz)) == 0 && w.ticks > 0) return;
        }
        actor.enterStep(3);
        actor.anim = 4;
        w.flags &= uint8_t(~kWalkerBusy);
        return;
    }
    case 3:
        if (actor.animEnded()) enterSub(actor, WalkerSub::ToCentre);
        return;
    default: return;
    }
}
// exe 0x4ae6b0: back to the nearest cell centre, then think
void toCentre(AnimatedActor& actor, WalkerFields& w) {
    switch (actor.step) {
    case 0: {
        if (nearCentre(actor)) {
            enterSub(actor, WalkerSub::Think);
            return;
        }
        w.flags &= kWalkerBusy;
        beginSub(actor);
        if (!(w.flags & kWalkerBusy)) w.setAction(uint8_t(WalkerAction::Walk));
        // the centre: integer parts only (see the box throw)
        const int32_t x = int32_t(uint32_t(uint16_t((uint16_t(actor.pos.x.whole) & 0xfc00u) + 0x200u)) << 16);
        const int32_t z = int32_t(uint32_t(uint16_t((uint16_t(actor.pos.z.whole) + 0x200u) & 0xfc00u)) << 16);
        w.heading = int16_t(angleOf(x, z, actor.pos.x, actor.pos.z));
        [[fallthrough]];
    }
    case 1:
        if (turnYawTo(actor.rotation.y, uint32_t(int32_t(w.heading)), 0x40) != 0) {
            actor.velocity.z = 0;
            return;
        }
        actor.step += 1u;
        [[fallthrough]];
    case 2:
        if (nearCentre(actor)) {
            enterSub(actor, WalkerSub::Think);
            return;
        }
        accel(actor, w, -0x200);
        return;
    default: return;
    }
}
// exe 0x4ae2d0: busy while the action runs, then think
void busySub(AnimatedActor& actor, WalkerFields& w) {
    switch (actor.step) {
    case 0:
        w.flags = kWalkerBusy;
        beginSub(actor);
        [[fallthrough]];
    case 1:
        if (!(w.flags & kWalkerBusy)) enterSub(actor, WalkerSub::Think);
        return;
    default: return;
    }
}
// a gun shot (pool 0x40 type 9 variant 0x85) from bone 3 (left) / 4 (right), along the walker's yaw + the head, the
// guns' pitch and spread, jittered
void fireShot(const AnimatedActor& actor, const WalkerFields& w, int k) {
    const int16_t muzzle[3] = {0, 0x168, int16_t((k & 4) ? 0x38 : 0x10)};
    int16_t out[3];
    const int left = k & 2;
    bonePointOffset(actor, left ? 3 : 4, muzzle, out);
    const uint32_t s = allocActor40(kActorStageClass); // exe 0x5e1280
    if (s == 0) return;
    ProjectileActor& shot = projectileActorAt(s);
    shot.flags = kActorLive;
    shot.loopFlags |= kActorSweepGroup2;
    shot.type = kGunShotType;
    shot.variant = kGunShotVariant;
    shot.pos.x.whole = out[0];
    shot.pos.y.whole = out[1];
    shot.pos.z.whole = out[2];
    shot.rotation = actor.rotation;
    shot.rotation.x += uint16_t(w.gunPitch);
    shot.rotation.y += uint16_t(w.headYaw);
    shot.rotation.y += uint16_t(left ? w.spreadLeft : w.spreadRight);
    shot.rotation.x += uint32_t(int(crtRand() & 0x7f) - 0x40);
    const uint32_t r = crtRand();
    GunShotParams(shot).attack = 0x1e;
    GunShotParams(shot).life = 0xa;
    shot.rotation.y += uint32_t(int(r & 0x7f) - 0x40);
}
// exe 0x4ae310: aim and fire for 0x3c ticks
void gunfire(AnimatedActor& actor, WalkerFields& w) {
    switch (actor.step) {
    case 0:
        w.flags = kWalkerBusy;
        beginSub(actor);
        actor.anim = 0xb;
        w.spreadLeft = 0;
        w.spreadRight = 0;
        w.gunPitch = 0;
        w.shotCount = 0;
        w.plannedCell = int16_t(targetCell());
        return;
    case 1:
        if (!actor.animEnded()) return;
        actor.enterStep(2);
        w.ticks = 0x14;
        return;
    case 2:
        aim(actor, w);
        headTurnToPlayer(actor, w);
        w.ticks -= 1u;
        if (w.ticks != 0) return;
        actor.enterStep(3);
        actor.anim = 0xc;
        w.ticks = 0x3c;
        w.flags &= uint8_t(~kWalkerHoldAim);
        return;
    case 3: {
        aim(actor, w);
        headTurnToPlayer(actor, w);
        int k = w.shotCount;
        w.shotCount -= 1u;
        bool skip = false;
        if (pointVisibleOrNear(actor.pos, 0) != 0) { // exe 0x5e59a0
            if (k & 1) skip = true;
            else k >>= 1;
        }
        if (!skip && !(k & 1)) fireShot(actor, w, k);
        w.ticks -= 1u;
        if (w.ticks != 0) return;
        if (int32_t(w.plannedCell) == int32_t(targetCell())) {
            actor.enterStep(2);
            actor.anim = 0xc;
            w.ticks = 0x14;
            w.flags |= kWalkerHoldAim;
            return;
        }
        actor.enterStep(4);
        actor.anim = 0xd;
        return;
    }
    case 4:
        if (!actor.animEnded() || headTurn(actor, w, actor.rotation.y) != 0) return;
        actor.subState = uint8_t(WalkerSub::Think);
        w.gunPitch = 0;
        w.spreadLeft = 0;
        w.spreadRight = 0;
        actor.restartStep();
        w.flags &= uint8_t(~kWalkerBusy);
        return;
    default: return;
    }
}

// ---- the actions [0x894118] -----------------------------------------------------------------------------------------
// exe 0x4ae990: animation 0
void idleAction(AnimatedActor& actor, WalkerFields& w) {
    if (w.actionStep != 0) return;
    w.actionStep = 1;
    actor.anim = 0;
}
// exe 0x4ae9b0: the walk animation 1 with footsteps
void walkAction(AnimatedActor& actor, WalkerFields& w) {
    if (w.actionStep == 0) {
        w.actionStep = 1;
        w.footstepTicks = 0;
        actor.anim = 1;
    }
    footsteps(actor, w);
}
// exe 0x4aea00: animation 1
void standAction(AnimatedActor& actor, WalkerFields& w) {
    if (w.actionStep != 0) return;
    w.actionStep = 1;
    actor.anim = 1;
}
// exe 0x4aea20: the swing (animations 8 / 9 / 0xa), a hit capsule between bones 3 and 4
void swingAction(AnimatedActor& actor, WalkerFields& w) {
    w.flags |= kWalkerBusy;
    const uint8_t flags = w.flags;
    switch (w.actionStep) {
    case 0:
        w.actionStep = 1;
        actor.anim = 8;
        return;
    case 1:
        if (!actor.animEnded()) return;
        w.actionTicks = 0x3c;
        w.actionStep = 2;
        actor.anim = 9;
        sound(0x1be, actor);
        return;
    case 2: {
        if (actor.animKey.frame == 0xc) sound(0x1b9, actor);
        const int16_t ends[2][3] = {{0, 0x120, -0x120}, {0, 0x120, 0x120}};
        int16_t p0[3], p1[3];
        bonePointOffset(actor, 3, ends[0], p0);
        bonePointOffset(actor, 4, ends[1], p1);
        hitListSubmitCapsule(p0, 0x28, p1, 0x28, w.hit,
                             hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitCapsule(2),
                             hitImmunity(kHitGroupLiftable) | kHitAttackStrong | kHitAttackThrown | 0x20u);
        w.actionTicks -= 1u;
        if (w.actionTicks != 0) return;
        w.actionStep = 3;
        actor.anim = 0xa;
        return;
    }
    case 3:
        if (!actor.animEnded()) return;
        w.setAction(uint8_t(WalkerAction::Idle));
        w.flags = uint8_t(flags & ~kWalkerBusy);
        return;
    default: return;
    }
}
// exe 0x4aeb90: the rolling bomb (pool 0x40 type 0x16) thrown at the target
void bombAction(AnimatedActor& actor, WalkerFields& w) {
    w.flags |= kWalkerBusy;
    switch (w.actionStep) {
    case 0:
        w.actionStep = 1;
        actor.anim = 5;
        sound(0x1b5, actor);
        return;
    case 1:
        if (actor.animEnded()) w.actionStep = 2;
        return;
    case 2:
        if (headTurn(actor, w, angleOf(actorLayoutAt(w.target).pos, actor.pos)) != 0) return;
        w.actionStep = 3;
        actor.anim = 6;
        return;
    case 3: {
        headTurn(actor, w, angleOf(actorLayoutAt(w.target).pos, actor.pos));
        if (!actor.animEnded()) return;
        sound(0x1bd, actor);
        w.actionTicks = 0xa;
        w.actionStep = 4;
        const uint32_t s = allocActor40(kActorStageClass); // exe 0x5e1280
        if (s == 0) return;
        ProjectileActor& bomb = projectileActorAt(s);
        bomb.flags = kActorLiveDrawn;
        bomb.loopFlags |= kActorSweepGroup2;
        bomb.type = kBombType;
        bomb.rotation = actor.rotation;
        bomb.rotation.y += uint16_t(w.headYaw);
        classState<BombFields>(bomb).thrower = actor.at;
        sound(0x1b7, actor);
        return;
    }
    case 4:
        w.actionTicks -= 1u;
        if (w.actionTicks != 0) return;
        w.actionStep = 5;
        actor.anim = 7;
        sound(0x1b6, actor);
        return;
    case 5:
        if (!actor.animEnded() || headTurn(actor, w, actor.rotation.y) != 0) return;
        switch (WalkerSub(actor.subState)) {
        case WalkerSub::Walk: w.setAction(uint8_t(WalkerAction::Walk)); break;
        case WalkerSub::Run: w.setAction(uint8_t(WalkerAction::Stand)); break;
        default: w.setAction(uint8_t(WalkerAction::Idle)); break;
        }
        w.flags &= uint8_t(~kWalkerBusy);
        return;
    default: return;
    }
}

// ---- the states [0x894108] ------------------------------------------------------------------------------------------
// exe 0x4acf60: the model of its sub, scaled 0x300, its row's hit points; registered as an enemy; the first walker
// starts on its route
void walkerInit(AnimatedActor& actor, WalkerFields& w) {
    if (!bindActorModel(actor, walkerModels[actor.subtype])) { // exe 0x5e0bc0
        freeActor(actor);                                      // exe 0x5e1490
        return;
    }
    actor.anim = 0;
    actor.prevAnim = kNoAnimShown;
    actor.scale.x = kWalkerScale;
    actor.scale.y = kWalkerScale;
    actor.scale.z = kWalkerScale;
    w.spreadLeft = 0;
    w.spreadRight = 0;
    w.gunPitch = 0;
    w.footstepTicks = 0;
    w.hurtSoundTicks = 0;
    const int16_t hp = hasAnimatedStats(actor.type) ? int16_t(animatedStats(actor.type).hitPoints) : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.collisionBox.pointAt(walkerBox.stateAddress());
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock(); // exe 0x40dc40
    w.hit.previousX = actor.pos.x.whole;
    w.hit.previousZ = actor.pos.z.whole;
    w.hit.result = 0;
    w.heading = actor.rotation.y;
    actor.enterState(uint8_t(WalkerState::Run), uint8_t(actor.variant != 0 ? WalkerSub::Think : WalkerSub::Route));
    registerEnemy(actor.slot, 0); // exe 0x5e5c10
}
// exe 0x4aede0: animations 0xf / 0x10 (sound 0x1c0), the death explosion, 0x28 ticks blinking, the drops, the wreck
void walkerDeath(AnimatedActor& actor, WalkerFields& w) {
    actor.tint = kTintFlash;
    switch (DeathPhase(actor.subState)) {
    case DeathPhase::Fall:
        w.flags = kWalkerNoHitEntry;
        actor.anim = 0xf;
        actor.subState = uint8_t(DeathPhase::Down);
        return;
    case DeathPhase::Down:
        if (!actor.animEnded()) return;
        actor.anim = 0x10;
        actor.subState = uint8_t(DeathPhase::Explode);
        sound(0x1c0, actor);
        return;
    case DeathPhase::Explode: {
        actor.velocity.x = 0;
        actor.subState = uint8_t(DeathPhase::Fade);
        actor.velocity.y = 0;
        actor.velocity.z = 0;
        w.ticks = 0x28;
        if (const uint32_t e = allocActor60(0)) { // exe 0x5e1300: the death explosion
            StaticActor& explosion = staticActorAt(e);
            auto&& params = ExplosionParams(actorHeaderAt(e));
            explosion.flags = kActorLive;
            explosion.type = typeByte(GlobalStaticType::Explosion);
            explosion.variant = 0;
            explosion.loopFlags |= kActorSweepGroup2;
            explosion.pos.x.whole = actor.pos.x.whole;
            explosion.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) - 0x100u);
            explosion.pos.z.whole = actor.pos.z.whole;
            params.spread = 0x80;
            params.flashes = uint16_t(w.ticks);
        }
        gridSet(actor, w);
        [[fallthrough]];
    }
    case DeathPhase::Fade: {
        if (uint16_t(w.ticks) & 1) actor.tint = kTintDeathBlink;
        uint16_t v = uint16_t(w.ticks);
        if (v == 0x14) w.flags |= kWalkerDying;
        v = uint16_t(v - 1u);
        w.ticks = int16_t(v);
        if (v != 0) {
            gridSet(actor, w);
            return;
        }
        countDefeat(); // exe 0x5e53e0
        spawnDrops(actor.pos, animatedStats(actor.type).dropTable); // exe 0x5f1270
        if (const uint32_t n = allocActor20()) { // exe 0x5e1160: the wreck (pool 0x20 type 0x2d sub 0)
            AnimatedActor& wreck = animatedActorAt(n);
            wreck.flags = kActorLiveDrawn;
            wreck.type = kWreckType;
            wreck.subtype = 0;
            wreck.variant = 0;
            wreck.params[0] = uint8_t(actor.variant + 1u);
            wreck.pos = actor.pos;
            wreck.rotation = actor.rotation;
        }
        freeActor(actor); // exe 0x5e1490
        return;
    }
    default: return;
    }
}
// exe 0x4ad070
void walkerRun(AnimatedActor& actor, WalkerFields& w) {
    actor.tint = kTintNeutral;
    w.lastCell = int16_t(cellWord(w));
    if (!(w.flags & kWalkerNoDamage) && damage(actor, w) < 0) {
        actor.enterState(uint8_t(WalkerState::Death));
        gridSet(actor, w);
        walkerDeath(actor, w);
        return;
    }
    switch (WalkerSub(actor.subState)) {
    case WalkerSub::Think: thinkSub(actor, w); break;
    case WalkerSub::Walk: pathWalk(actor, w, false); break;
    case WalkerSub::BoxAhead: boxAhead(actor, w); break;
    case WalkerSub::Turn: turnSub(actor, w); break;
    case WalkerSub::Run: pathWalk(actor, w, true); break;
    case WalkerSub::Stop: stopSub(actor, w); break;
    case WalkerSub::Hurt: hurtSub(actor, w); break;
    case WalkerSub::ThrowBox: throwBox(actor, w); break;
    case WalkerSub::Route: routeWalk(actor, w); break;
    case WalkerSub::Charge: charge(actor, w); break;
    case WalkerSub::ToCentre: toCentre(actor, w); break;
    case WalkerSub::Busy: busySub(actor, w); break;
    case WalkerSub::Gunfire: gunfire(actor, w); break;
    default: notPortedAt(kStoryPort, "0x4accc0 sub-state (past the table 0x894130)", actor.subState);
    }
    gridSet(actor, w);
}
// exe 0x4accc0
void boxWalker(AnimatedActor& actor) {
    WalkerFields& w = classState<WalkerFields>(actor);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    gridClear(actor, w);
    if (const uint32_t t = w.target) {
        const Actor& target = actorLayoutAt(t);
        search().targetX = cellOfX(target.pos.x.whole);
        search().targetZ = cellOfZ(target.pos.z.whole);
    }
    switch (WalkerState(actor.state)) {
    case WalkerState::Init: walkerInit(actor, w); break;
    case WalkerState::Run: walkerRun(actor, w); break;
    case WalkerState::Death: walkerDeath(actor, w); break;
    default: notPortedAt(kStoryPort, "0x4accc0 state (past the table 0x894108)", actor.state);
    }
    switch (WalkerAction(w.action)) {
    case WalkerAction::None: break; // exe 0x52ef00
    case WalkerAction::Idle: idleAction(actor, w); break;
    case WalkerAction::Walk: walkAction(actor, w); break;
    case WalkerAction::Stand: standAction(actor, w); break;
    case WalkerAction::Swing: swingAction(actor, w); break;
    case WalkerAction::Bomb: bombAction(actor, w); break;
    default: notPortedAt(kStoryPort, "0x4accc0 action (past the table 0x894118)", w.action);
    }
    if (!(actor.flags & kActorLive)) return;
    if (!(w.flags & kWalkerHoldAim)) {
        if (actor.anim == actor.prevAnim) animStepPose(actor); // exe 0x5e1e80
        else animSetPose(actor, actor.anim, 0);               // exe 0x5e1c30
        // the head's yaw and the guns' angles added to the decoded frame (part 1's yaw, parts 3 / 4's pitch and roll)
        DecodedFrame& frame = decodedFrame();
        frame.entries[2].y += uint16_t(w.headYaw);
        frame.entries[4].x += uint16_t(w.gunPitch);
        frame.entries[5].x += uint16_t(w.gunPitch);
        frame.entries[4].z += uint16_t(w.spreadLeft);
        frame.entries[5].z += uint16_t(w.spreadRight);
        animPose(actor); // exe 0x5e2d90
    }
    const WalkerStats stats = walkerStats(actor);
    w.hit.result = 0;
    w.hit.previousX = actor.prevPos.x.whole;
    w.hit.previousZ = actor.prevPos.z.whole;
    if (!(w.flags & kWalkerNoHitEntry)) {
        const ShortVec4& body = bonePoint(actor, 1, kStoryPort); // exe 0x5e1810
        const int16_t pos[3] = {body.x, body.y, body.z};
        const uint32_t ownerShape = hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | kHitCompareHeights |
                                    hitHeightLevel(stats.hitHeight) | hitRadius(stats.hitRadiusBytes) |
                                    ((w.flags & kWalkerDying) ? kHitNoLockOn : 0u);
        hitListSubmit(pos, w.hit, ownerShape, uint32_t(stats.attack) | kHitAttackSolid); // exe 0x5e4120
    }
    if (w.flags & kWalkerCarrying) { // the carried box between the hands (bones 3 / 4)
        Actor& box = actorLayoutAt(w.target);
        box.rotation.y = actor.rotation.y;
        const int16_t grip[3] = {0, 0x140, 0x40};
        int16_t p3[3], p4[3];
        bonePointOffset(actor, 3, grip, p3);
        bonePointOffset(actor, 4, grip, p4);
        box.pos.x.whole = int16_t((int(p4[0]) + p3[0]) >> 1);
        box.pos.y.whole = int16_t((int(p4[1]) + p3[1]) >> 1);
        box.pos.z.whole = int16_t((int(p4[2]) + p3[2]) >> 1);
    }
    // exe 0x5f20f0(a, 0x180, 0x100, 0): the shadow (render output)
}

// ---- 0x5c8420: the rolling bomb (states [0x8d13a4]) ----------------------------------------------------------------
enum class BombState : uint8_t { Init = 0, Fly = 1, Blast = 2, Gone = 3 };
enum class BombPhase : uint8_t { Arc = 0, Roll = 1 };
const DataTable<CollisionBoxRecord> bombBox{TableId::GlydeBoxWalkersBombBox};
constexpr uint32_t kBombAttackKinds = 0xff0000; // any attack kind in its result: it blows

// exe 0x5c8440: model variant 0, at the thrower, lifted (0, -0xc00, -0x600) along the yaw
void bombInit(ProjectileActor& bomb, BombFields& fields) {
    const uint32_t thrower = fields.thrower;
    if (!bindActorModel(bomb, 0)) { // exe 0x5e0ba0
        freeActor(bomb);           // exe 0x5e1500
        return;
    }
    bomb.state += 1u;
    bomb.pos = actorLayoutAt(thrower).pos;
    integrateVelocity(bomb, 0, -0xc00, -0x600);
    bomb.airborne = 1;
    bomb.onPlatform = 0;
    bomb.platform = nullptr;
    bomb.collisionBox.pointAt(bombBox.stateAddress());
    bomb.actorHit = 0;
    fields.clearBlockBytes(); // exe 0x40dc40(+0xc0, +0xd8)
    fields.hit.previousX = bomb.pos.x.whole;
    fields.hit.previousZ = bomb.pos.z.whole;
}
// exe 0x5c84f0: the arc (gravity 0x40), spinning, until it lands; then it rolls on with a hit entry (sound 0x1b8 every
// 10 ticks). Any hit or a wall -> the blast; a floor / ceiling: on the ground
void bombFly(const PlayerEnv& env, ProjectileActor& bomb, BombFields& fields) {
    bomb.savePreviousPose();
    switch (BombPhase(bomb.subState)) {
    case BombPhase::Arc: // exe 0x5c85a0
        if (bomb.step == 0) {
            bomb.step = 1;
            bomb.velocity.y = 0;
        }
        bomb.velocity.y += 0x40u;
        bomb.rotation.x += 0x100u;
        integrateVelocity(bomb, 0, bomb.velocity.y, -0x320);
        if (bomb.airborne == 0) bomb.subState += 1u;
        break;
    case BombPhase::Roll: { // exe 0x5c85f0
        const int16_t pos[3] = {bomb.pos.x.whole, bomb.pos.y.whole, bomb.pos.z.whole};
        bomb.rotation.x += 0x100u;
        hitListSubmit(pos, fields.hit, hitOwner(bomb.slot) | hitGroup(kHitGroupLiftable) | kHitContactMotion | hitRadius(0x30),
                      kHitAttackHarmless | kHitAttackStrong | 0x10u); // exe 0x5e4120
        integrateVelocity(bomb, 0, 0, -0x320);
        const uint16_t v = fields.rollSoundTicks;
        fields.rollSoundTicks = uint16_t(v - 1u);
        if (v == 0) {
            fields.rollSoundTicks = 9;
            soundPlayAtPos(0x1b8, bomb.pos);
        }
        break;
    }
    default: notPortedAt(kStoryPort, "0x5c8420 sub-state (table 0x8d13b4)", bomb.subState);
    }
    if (fields.hit.result & kBombAttackKinds) {
        bomb.enterState(uint8_t(BombState::Blast));
    } else if (int32_t(actorTestMove(env, bomb)) < 0) { // exe 0x40f570
        const uint32_t r = actorPushOut(env, bomb);       // exe 0x410960
        if (r & kWallOnSides) bomb.enterState(uint8_t(BombState::Blast));
        else if (r & kWallOnY) bomb.airborne = 0;
    }
    fields.hit.result = 0;
    fields.hit.previousX = bomb.prevPos.x.whole;
    fields.hit.previousZ = bomb.prevPos.z.whole;
    // exe 0x5f20f0(a, 0x50, 0x100, 0): the shadow (render output)
}
// exe 0x5c8690: the blast (the global static type 7)
void bombBlast(ProjectileActor& bomb) {
    bomb.state += 1u;
    const uint32_t e = allocActor60(0); // exe 0x5e1300
    if (e == 0) return;
    StaticActor& blast = staticActorAt(e);
    auto&& params = BlastParams(actorHeaderAt(e));
    blast.flags = kActorLive;
    blast.loopFlags |= kActorSweepGroup2;
    blast.type = typeByte(GlobalStaticType::Blast);
    blast.variant = 0;
    blast.pos.x.whole = bomb.pos.x.whole;
    blast.pos.y.whole = int16_t(uint16_t(bomb.pos.y.whole) - 0x14u);
    blast.pos.z.whole = bomb.pos.z.whole;
    params.size = 0x50;
    params.attack = 5;
}
// exe 0x5c8420
void walkerBomb(const PlayerEnv& env, ProjectileActor& bomb) {
    BombFields& fields = classState<BombFields>(bomb);
    switch (BombState(bomb.state)) {
    case BombState::Init: bombInit(bomb, fields); return;
    case BombState::Fly: bombFly(env, bomb, fields); return;
    case BombState::Blast: bombBlast(bomb); return;
    case BombState::Gone: freeActor(bomb); return; // exe 0x5d0bc0 -> 0x5e1500
    default: notPortedAt(kStoryPort, "0x5c8420 state (table 0x8d13a4)", bomb.state);
    }
}

} // namespace

void boxWalkerUpdate(const PlayerEnv&, uint32_t actor) { boxWalker(animatedActorAt(actor)); }
void walkerBombUpdate(const PlayerEnv& env, uint32_t actor) { walkerBomb(env, projectileActorAt(actor)); }

} // namespace od::port
