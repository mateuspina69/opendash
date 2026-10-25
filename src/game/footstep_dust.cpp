// The footstep dust (see footstep_dust.hpp).
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/footstep_dust.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/draw_commands.hpp"
#include "game/game_state.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"

namespace od::port {

namespace {

inline constexpr const char* kDustArea = "footstep dust";

constexpr int16_t kDustPuffLife = 0x30;
constexpr uint8_t kDustPuffAge = 0x14;
constexpr int kDustDriftPlayer = 2, kDustDriftActor = 8; // the drift's length along the walker's side
constexpr int kSideTurn = 0x400;                         // the walker's side: its yaw + a quarter turn
constexpr int16_t kFootHeight = 0x90;                    // an actor's foot: its position's height - this
constexpr uint32_t kFootBone = 3;                        // ... at its bone 3's x / z
// the puffs' sprite: semi-transparent, colour 0x101080, tpage 0x2e, CLUT 0x7c91, 0x30-texel frames of the u table (by
// the puff's growth >> 3; the frames from 4 on in the lower row)
constexpr uint32_t kDustColour = 0x101080;
constexpr uint16_t kDustTpage = 0x2e, kDustClut = 0x7c91;
constexpr uint32_t kDustFrameSize = 0x2f, kDustLowerRow = 0x30;
const DataTable<ScalarTable<uint8_t, 8>> dustFrameU{TableId::FootstepDustFrameU};
// the points' jitter a frame (the RNG's low bits & mask, centred by the offset): the player's, an actor's
constexpr uint32_t kJitterMaskPlayer = 0xf, kJitterMaskActor = 0x1f;
constexpr uint32_t kJitterCentrePlayer = 7, kJitterCentreActor = 0xf;

// the walker's fields the dust reads: its stop word (the walker's classes set it when it stops making dust)
struct DustWalkerFields {
    explicit DustWalkerFields(const Actor& actor) : DustWalkerFields(const_cast<Actor&>(actor)) {}
    explicit DustWalkerFields(Actor& actor) : stopWord(actor.velocity.spare) {}
    FieldAlias<uint16_t, int16_t> stopWord;
};

void dustRun(const PlayerEnv& env, StaticActor& actor, FootstepDustFields& dust);

// exe 0x5acbe0 (state Start): the puffs' points cleared, then the run state at once
void dustStart(const PlayerEnv& env, StaticActor& actor, FootstepDustFields& dust) {
    actor.state = uint8_t(FootstepDustState::Run);
    dust.made = 0;
    dust.stopped = 0;
    for (uint32_t k = 0; k < kDustPuffs; ++k) {
        auto&& puff = dust.puffs[k];
        puff.first.x = 0;
        puff.first.y = 0;
        puff.first.z = 0;
        puff.second.x = 0;
        puff.second.y = 0;
        puff.second.z = 0;
    }
    dustRun(env, actor, dust);
}

// the point a new puff starts at (the variant's: the walker's foot, or the spawn's point)
void puffStart(const PlayerEnv& env, const StaticActor& actor, const Actor& walker, bool isPlayer, int16_t p[3]) {
    if (actor.variant == kDustAtFoot) {
        if (isPlayer) {
            const int16_t g = int16_t(actorGround(env, player())); // exe 0x40f190
            p[0] = player().pos.x.whole;
            p[1] = g;
            p[2] = player().pos.z.whole;
        } else {
            const ShortVec4& foot = bonePoint(animatedActorAt(walker.at), kFootBone, kDustArea); // exe 0x5e1810
            p[0] = foot.x;
            p[1] = int16_t(uint16_t(walker.pos.y.whole) - uint16_t(kFootHeight));
            p[2] = foot.z;
        }
    } else if (isPlayer) {
        p[0] = player().pos.x.whole;
        p[1] = 0;
        p[2] = player().pos.z.whole;
    } else {
        p[0] = actor.pos.x.whole;
        p[1] = actor.pos.y.whole;
        p[2] = actor.pos.z.whole;
    }
}

// exe 0x5acc30 (state Run): every other frame a new puff (two points at the walker's foot, the drift along its side)
// until kDustPuffs or the walker stops; each puff ages kDustPuffAge frames as two billboards (the frame by its growth,
// the size growing) drifting apart with RNG jitter; the last puff's end frees the actor
void dustRun(const PlayerEnv& env, StaticActor& actor, FootstepDustFields& dust) {
    const bool isPlayer = dust.parent.get() == &player();
    Actor& walker = *dust.parent;
    if (isPlayer) {
        if (!(player().status & kStatusBurnFloor)) dust.stopped = 1;
    } else if (walker.flags == 0 || DustWalkerFields(walker).stopWord != 0) {
        dust.stopped = 1;
    }
    if (dust.stopped == 0 && !(gameState().frame & 1) && dust.made < int8_t(kDustPuffs)) {
        auto&& puff = dust.puffs[uint32_t(int32_t(dust.made))];
        puff.life = kDustPuffLife;
        int16_t vx = 0, vz = 0;
        angleVector(int(int16_t(uint16_t(uint16_t(walker.rotation.y) + kSideTurn))),
                    isPlayer ? kDustDriftPlayer : kDustDriftActor, vx, vz); // exe 0x5e40c0
        puff.driftX = vx;
        puff.driftZ = vz;
        int16_t p[3];
        puffStart(env, actor, walker, isPlayer, p);
        puff.first.x = p[0];
        puff.first.y = p[1];
        puff.first.z = p[2];
        puff.age = 0;
        puff.second.x = p[0];
        puff.second.y = p[1];
        puff.second.z = p[2];
        puff.age = 0;
        dust.made = int8_t(dust.made + 1);
    }
    if (dust.made == 0) {
        actor.state = uint8_t(FootstepDustState::Free);
        return;
    }
    for (int8_t i = 0; i < dust.made; ++i) {
        auto&& puff = dust.puffs[uint32_t(int32_t(i))];
        const uint16_t age = puff.age;
        if (uint8_t(age) == kDustPuffAge) {
            if (i == dust.made - 1) {
                actor.state = uint8_t(FootstepDustState::Free);
                return;
            }
            continue;
        }
        puff.age = uint16_t(age + 1u);
        puff.life = int16_t(uint16_t(puff.life) - 1u);
        for (uint32_t j = 0; j < 2; ++j) {
            const int32_t grow = 0x40 - int32_t(puff.life);
            const int8_t step = int8_t(grow >> 3);
            const uint32_t u = dustFrameU[size_t(int32_t(step))];
            const uint32_t vo = step < 4 ? 0 : kDustLowerRow;
            auto&& point = j == 0 ? puff.first : puff.second;
            const uint32_t size =
                uint32_t((isPlayer ? (grow * 5 + 0x50) << 4 : (grow + 0x10) << 7) >> 6) & 0xffff;
            dc::Billboard b;
            b.colour = dc::billboardColour(kDustColour);
            b.xy = dc::packXY(point.x, point.y);
            b.z = dc::signedZWord(point.z);
            b.clut = kDustClut;
            b.tpage = kDustTpage;
            b.uv[0] = dc::texel(u, vo);
            b.uv[1] = dc::texel(u + kDustFrameSize, vo);
            b.uv[2] = dc::texel(u, vo + kDustFrameSize);
            b.uv[3] = dc::texel(u + kDustFrameSize, vo + kDustFrameSize);
            b.sizeRotation = dc::sizeRotation(size, 0);
            dc::emit(b, actor.at);
            const uint32_t r = rngStep();
            const uint32_t mask = isPlayer ? kJitterMaskPlayer : kJitterMaskActor;
            const uint32_t c = isPlayer ? kJitterCentrePlayer : kJitterCentreActor;
            if (j == 0) {
                point.x = int16_t(uint16_t(point.x) + c - uint16_t(puff.driftX) - (r & mask));
                point.z = int16_t(uint16_t(point.z) + c - ((r >> 5) & mask) - uint16_t(puff.driftZ));
            } else {
                point.x = int16_t(uint16_t(point.x) + uint16_t(puff.driftX) + (r & mask) - c);
                point.z = int16_t(uint16_t(point.z) + ((r >> 5) & mask) + uint16_t(puff.driftZ) - c);
            }
        }
    }
}

} // namespace

void footstepDustUpdate(const PlayerEnv& env, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    FootstepDustFields& dust = classState<FootstepDustFields>(actor);
    switch (FootstepDustState(actor.state)) {
    case FootstepDustState::Start: dustStart(env, actor, dust); return;
    case FootstepDustState::Run: dustRun(env, actor, dust); return;
    case FootstepDustState::Free: freeActor(actor); return; // exe 0x5afdd0 -> 0x5e1540
    default: notPortedAt(kDustArea, "exe 0x5acbc0 state", actor.state);
    }
}

namespace {
// exe 0x5ad170's spawn: the dust slot of the walker at `parent`, nullptr when the pool is full
StaticActor* spawnFootstepDust(uint32_t parent, uint8_t variant) {
    const uint32_t at = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (at == 0) return nullptr;
    StaticActor& dust = staticActorAt(at);
    dust.flags = kActorLive;
    dust.loopFlags |= kActorSweepGroup2;
    dust.type = kFootstepDustType;
    dust.variant = variant;
    return &dust;
}
} // namespace

void footstepDustAtFoot(ActorHeader& parent) {
    StaticActor* dust = spawnFootstepDust(parent.at, kDustAtFoot);
    if (dust == nullptr) return;
    classState<FootstepDustFields>(*dust).parent = &actorLayoutOf(parent);
}

void footstepDustAt(const ActorHeader& parent, uint8_t variant, const int16_t point[3]) {
    StaticActor* dust = spawnFootstepDust(parent.at, variant);
    if (dust == nullptr) return;
    if (variant != kDustAtFoot) {
        dust->pos.x.whole = point[0];
        dust->pos.y.whole = point[1];
        dust->pos.z.whole = point[2];
    }
    classState<FootstepDustFields>(*dust).parent = &actorLayoutOf(parent);
}

} // namespace od::port
