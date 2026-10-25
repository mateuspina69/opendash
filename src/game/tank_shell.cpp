// The tank shell, class 0x5c6cb0 (see tank_shell.hpp). States [0x8d12fc]: 0 0x5c6d40 (set up: the speed, the previous
// position one step behind, the glow when visible), 1 0x5c6e00 (the flight); state 3 0x5c7060 is never entered by its
// spawners. Its glow (a ring record) and its billboard are draw commands.
#include "game/actor_state.hpp"
#include "game/tank_shell_state.hpp"
#include "game/tank_shell.hpp"

#include "engine/data_source.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/draw_commands.hpp"
#include "game/fixed_point.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"
#include "game/visibility.hpp"

namespace od::port {

using namespace tank_shell_state;

namespace {

constexpr const char* kTankShellPort = "tank shell port"; // the area of its port-gap errors

enum class TankShellState : uint8_t { Setup = 0, Fly = 1 };
// the flight's end: none, an actor / a hit, its ticks out, the player's shield
enum class ShellEnd : int { Flying = 0, Hit = 1, Spent = 2, Shielded = 3 };

const DataTable<CollisionBoxRecord> tankShellBox{TableId::TankShellBox};
constexpr uint16_t kSoundShot = 0x118;
constexpr uint32_t kShellColour = 0x808080;

uint32_t sizeClass(const ProjectileActor& a) { return a.variant & 3u; }

ShellEnd fly(ProjectileActor& a, TankShellFields& f) { // exe 0x5c6e00
    a.savePreviousPose();
    if (a.actorHit != 0 || (f.hitBox.result & (kHitAttackBump | kHitAttackSolid))) return ShellEnd::Hit;
    f.ticks = int16_t(uint16_t(f.ticks) - 1u);
    if (f.ticks == 0) return ShellEnd::Spent;
    moveAlongRotation(a.pos, a.rotation, 0, 0, a.velocity.z); // exe 0x5e3810
    const uint32_t radius = sizeClass(a) * 8 + 0x20;
    if (playerShieldBlocks(a, radius)) return ShellEnd::Shielded; // exe 0x5737b0
    if (pointVisibleOrNear(a.pos, 0) == 0) {
        // the billboard: its size by the size class, turning with its ticks (the exe also stores 0x7fc0 / 0x7fdf at
        // +0x44 / +0x4c, past the record: not reproduced)
        dc::Billboard b;
        b.colour = dc::billboardColour(kShellColour);
        b.xy = dc::packXY(a.pos.x.whole, a.pos.y.whole);
        b.z = dc::signedZWord(a.pos.z.whole);
        b.clut = 0x7dd0;
        b.tpage = 0x2f;
        b.uv[0] = 0x60c0;
        b.uv[1] = 0x60df;
        b.uv[2] = 0x7fc0;
        b.uv[3] = 0x7fdf;
        b.sizeRotation = dc::sizeRotation(sizeClass(a) * 0x10 + 0x18, (uint32_t(uint16_t(f.ticks)) & 0xfu) << 8);
        dc::emit(b, a.at);
    }
    const int16_t pos[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
    f.hitBox.result = 0;
    f.hitBox.previousX = a.prevPos.x.whole;
    f.hitBox.previousZ = a.prevPos.z.whole;
    // the shape: the radius / 4 in hit group 6 (the exe's (radius | 0x3000000) >> 2)
    hitListSubmit(pos, f.hitBox, hitOwner(a.slot) | ((radius | 0x3000000u) >> 2),
                  uint32_t(f.attack) | kHitAttackStrong | kHitAttackByPlayer);
    a.airborne = 1;
    a.onPlatform = 0;
    a.platform = nullptr;
    return ShellEnd::Flying;
}

void tankShell(const PlayerEnv& env, ProjectileActor& a) { // exe 0x5c6cb0
    TankShellFields& f = classState<TankShellFields>(a);
    ShellEnd r = ShellEnd::Flying;
    switch (TankShellState(a.state)) { // [0x8d12fc]
    case TankShellState::Setup: { // exe 0x5c6d40
        a.runFlags |= kActorWarpKept;
        a.setDrawn(false);
        f.hitBox.result = 0;
        const int16_t speed = wrap16((-7 - int((a.variant >> 2) & 3)) * 0x100);
        a.velocity.z = speed;
        a.velocity.setXY(0, 0);
        a.prevPos = a.pos;
        moveAlongRotation(a.prevPos, a.rotation, 0, 0, -2 * int(speed)); // exe 0x5e3810
        a.collisionBox.pointAt(tankShellBox.stateAddress());
        a.actorHit = 0;
        soundPlayAtPos(kSoundShot, a.pos);
        a.state = uint8_t(a.state + 1u);
        if (pointVisibleOrNear(a.pos, 0) == 0) { // exe 0x5c6fc0: the glow
            dc::Ring g;
            g.inner = dc::ringColour(0x8080);
            g.xy = dc::packXY(a.pos.x.whole, a.pos.y.whole);
            g.zStep = dc::zWord(a.pos.z.whole) | dc::ringStep(2);
            g.radii = dc::ringRadii(0, sizeClass(a) * 0x20 + 0x30);
            dc::emit(g, a.at);
        }
        break;
    }
    case TankShellState::Fly: r = fly(a, f); break;
    default: notPortedAt(kTankShellPort, "class 0x5c6cb0 state", a.state);
    }
    if (r == ShellEnd::Flying) {
        const uint32_t t = actorTestMove(env, a); // exe 0x40f570
        if (int32_t(t) < 0) {
            queueBreakableWallHit(t, f.attack);
            actorPushOut(env, a); // exe 0x410960
            freeActor(a);         // exe 0x5d0bc0
        }
        return;
    }
    freeActor(a); // exe 0x5d0bc0 (a hit, its ticks out) / 0x5e1500 (the shield)
}

} // namespace

void tankShellUpdate(const PlayerEnv& env, uint32_t actor) { tankShell(env, projectileActorAt(actor)); }

} // namespace od::port
