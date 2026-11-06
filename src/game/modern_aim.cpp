// OpenDash's free aim (see modern_aim.hpp).
#include "game/modern_aim.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/hit_list.hpp"
#include "game/lock_on_targets.hpp"
#include "game/modern_play.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/player/remote_player.hpp"

namespace od::port {

namespace {

struct AimTarget {
    float a[3], b[3]; // the ends of a capsule (a sphere: the same point)
    float ra, rb;
};

constexpr uint32_t kCapsuleSamples = 8;
constexpr float kShotMargin = 32.0f;    // the buster's smallest hit radius: a ray this close to a body still hits it
constexpr float kMinAimDistance = 64.0f; // a nearer aim point gives no direction: the muzzle may already be past it
constexpr int kMaxYawOff = 0x400, kMaxPitch = 0x300; // a shot's angles off the body's facing / the level
constexpr float kCrossGap = 2.0f, kCrossLength = 3.0f, kCrossWidth = 0.75f, kCrossShadow = 0.5f;
constexpr uint32_t kCrossWhite = 0xffffff, kCrossRed = 0x3030ff, kCrossDark = 0x000000;

AimRay g_ray;
std::array<AimTarget, kHitListCapacity> g_targets;
uint32_t g_targetCount = 0;

// where the ray ends: on the nearest candidate it meets (true) or at its end
bool aimPoint(float out[3]) {
    const AimRay& ray = g_ray;
    float best = ray.length;
    bool found = false;
    for (uint32_t i = 0; i < g_targetCount; ++i) {
        const AimTarget& t = g_targets[i];
        for (uint32_t s = 0; s <= kCapsuleSamples; ++s) {
            const float k = float(s) / float(kCapsuleSamples), r = t.ra + (t.rb - t.ra) * k;
            float c[3], v[3];
            for (int q = 0; q < 3; ++q) {
                c[q] = t.a[q] + (t.b[q] - t.a[q]) * k;
                v[q] = c[q] - ray.origin[q];
            }
            const float along = v[0] * ray.dir[0] + v[1] * ray.dir[1] + v[2] * ray.dir[2];
            if (along <= 0.0f || along >= best) continue;
            float off[3];
            for (int q = 0; q < 3; ++q) off[q] = ray.origin[q] + ray.dir[q] * along - c[q];
            const float d = std::sqrt(off[0] * off[0] + off[1] * off[1] + off[2] * off[2]);
            if (d > r + kShotMargin) continue;
            best = along;
            found = true;
            // inside the body: the point on the ray (a large body keeps the exact spot); in the margin: on its surface
            const float keep = d > r ? r / d : 1.0f;
            for (int q = 0; q < 3; ++q) out[q] = c[q] + off[q] * keep;
        }
    }
    if (!found)
        for (int q = 0; q < 3; ++q) out[q] = ray.origin[q] + ray.dir[q] * ray.length;
    return found;
}

bool freeAimOn() { return g_ray.on && modernPlay() && player().lockedActor == nullptr; }

// another machine's player (a session): the angles their own shot left with, from their crosshair
struct ShotAim {
    bool set = false;
    int16_t pitch = 0, yaw = 0;
};
std::array<ShotAim, kPlayerSlots> g_shotAim{};

} // namespace

void setPlayerShotAim(size_t player, bool set, int16_t pitch, int16_t yaw) {
    if (player < g_shotAim.size()) g_shotAim[player] = {set, pitch, yaw};
}

void modernAimSetRay(const AimRay& ray) { g_ray = ray; }

void modernAimCollectTargets(uint32_t count) {
    const ActorSystemState& sys = actorSystem();
    const uint32_t groups = kHitGroupsAll >> kHitGroupShift;
    g_targetCount = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const HitEntry& e = sys.hitList[i];
        const bool capsule = (uint16_t(e.shape()) & kHitCapsuleMask) != 0;
        const HitEntry& end = capsule ? sys.hitList[i + 1] : e;
        const uint32_t owner = e.owner;
        if ((hitGroupPairs((owner >> kHitGroupShift) & groups) & kHitGroupLockOn) && !(owner & kHitNoLockOn)) {
            AimTarget& t = g_targets[g_targetCount++];
            t.a[0] = e.pos.x, t.a[1] = e.pos.y, t.a[2] = e.pos.z;
            t.b[0] = end.pos.x, t.b[1] = end.pos.y, t.b[2] = end.pos.z;
            t.ra = float((uint16_t(e.shape()) & kHitRadiusMask) << kHitRadiusScale);
            t.rb = float((uint16_t(end.shape()) & kHitRadiusMask) << kHitRadiusScale);
        }
        if (capsule) ++i;
    }
}

bool modernAimAngles(const int32_t from[3], int16_t& pitch, int16_t& yaw) {
    // a session's other player: their crosshair is on their machine, the angles their shot left with came with it
    if (const size_t acting = playerId(actingPlayer()); playerIsRemote(acting)) {
        const ShotAim& a = g_shotAim[acting];
        if (!a.set) return false;
        pitch = a.pitch;
        yaw = a.yaw;
        return true;
    }
    if (!freeAimOn()) return false;
    float at[3];
    aimPoint(at);
    float dist = 0.0f;
    int32_t to[3];
    for (int q = 0; q < 3; ++q) {
        to[q] = int32_t(std::clamp(at[q], -32767.0f, 32767.0f) * 65536.0f); // the far end may leave the 16.16 range
        const float d = at[q] - float(from[q]) / 65536.0f;
        dist += d * d;
    }
    if (dist < kMinAimDistance * kMinAimDistance) return false;
    const int body = player().rotation.y;
    const int off = signedAngle(angleOf(from[0], from[2], to[0], to[2]) - kHalfTurn - body);
    yaw = signedAngle(body + std::clamp(off, -kMaxYawOff, kMaxYawOff));
    pitch = int16_t(std::clamp(int(signedAngle(pitchOf(from, to) - kHalfTurn)), -kMaxPitch, kMaxPitch));
    return true;
}

std::vector<hud::Prim> modernAimDraw() {
    std::vector<hud::Prim> out;
    const Player& p = player();
    if (!freeAimOn() || p.playerState() != PlayerState::Playing || p.activeBuster == kTownBuster) return out;
    float at[3];
    const uint32_t colour = aimPoint(at) ? kCrossRed : kCrossWhite;
    const float cx = float(kAimCentreX), cy = float(kAimCentreY), w = kCrossWidth * 0.5f;
    auto bar = [&](float x0, float y0, float x1, float y1, float grow, uint32_t c) {
        hud::Prim q;
        q.kind = hud::Prim::Flat;
        q.colour = c;
        q.x[0] = q.x[2] = cx + x0 - grow;
        q.x[1] = q.x[3] = cx + x1 + grow;
        q.y[0] = q.y[1] = cy + y0 - grow;
        q.y[2] = q.y[3] = cy + y1 + grow;
        out.push_back(q);
    };
    const float g = kCrossGap, e = kCrossGap + kCrossLength;
    for (int pass = 0; pass < 2; ++pass) { // a dark edge under the four ticks, for bright backgrounds
        const float grow = pass == 0 ? kCrossShadow : 0.0f;
        const uint32_t c = pass == 0 ? kCrossDark : colour;
        bar(-e, -w, -g, w, grow, c);
        bar(g, -w, e, w, grow, c);
        bar(-w, -e, w, -g, grow, c);
        bar(-w, g, w, e, grow, c);
    }
    return out;
}

} // namespace od::port
