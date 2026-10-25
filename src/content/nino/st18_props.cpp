// ST18 (Nino) scenery classes of rooms 0 / 1 (room spawn lists [0x8e37d0 + sub * 4]: sub 0 = 0x8e3688, sub 1 =
// 0x8e36f0) and the effects of the stage's per-class tables (pool 0x60 [*0xa6bb8c] = 0x8e32e4, e0 = 0x8e3460, a0 =
// 0x8e3404). One routine per exe routine; the names are hypotheses from the observed behaviour.
//
//   0x58ffa0  pool 0x60 type 1 sub 0 (ST18 sub 0, records 0x8e36b0 / 0x8e36c4: variants 0 / 1): a still solid model
//             (the stage's "mover" family of ST1A without motion); variant != 0 also has a shadow
//   0x417250  pool e0 type 1 (ST18 subs 0 / 1, record 0x8e369c, variant 0): the sun -- projected, bent toward the
//             screen edges, and drawn as two lens-flare chains of screen-space discs (draw records 0xf4)
//   0x5abb70  pool 0x60 type 0x2d subs 1 / 3 (record 0x8a682c, after script 0x37): a still model -- bound (variant 0)
//             once, its previous pose kept every tick
#include "content/nino/st18_props.hpp"
#include "content/nino/st18_props_state.hpp"

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/camera_state.hpp"
#include "game/draw_commands.hpp"
#include "game/fixed_point.hpp"
#include "game/vectors.hpp"
#include "game/game_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st18_props_state;

namespace {

// the signed division by 2^sh rounding to zero (the exe's cdq / and / add / sar)
int32_t divideRoundingToZero(int32_t v, int sh) { return (v + int32_t((v >> 31) & ((1 << sh) - 1))) >> sh; }

// ---- 0x58ffa0: pool 0x60 type 1 sub 0, the still solid model --------------------------------------------------------
// [0x8cdc9c]
enum class StillState : uint8_t { Init = 0, Stand = 1 };

const DataTable<CollisionBoxRecord> stillBox{TableId::St18PropsStillBox};
const DataTable<ScalarTable<uint8_t, 4>> stillModelVariants{TableId::St18PropsStillModelVariants}; // the model variant of each variant
constexpr int16_t kStillHitPoints = 0x40;
constexpr int16_t kSlowSpin = 0x80; // variant 3's turn speed

// exe 0x58ffe0: the model of the variant, the motion and contact cleared, the class block cleared; variants 2 / 4 still,
// 3 turning slowly
void stillInit(StaticActor& actor) {
    if (!bindActorModel(actor, stillModelVariants[actor.variant])) { // exe 0x5e0bc0
        freeActor(actor);
        return;
    }
    actor.maxHitPoints = kStillHitPoints;
    actor.hitPoints = kStillHitPoints;
    actor.velocity.setXY(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.collisionBox.pointAt(stillBox.stateAddress());
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.wallHit = 0;
    actor.actorHit = 0;
    actor.lastMover = nullptr;
    actor.clearClassBlock(); // exe 0x40dc40
    actor.enterState(uint8_t(StillState::Stand));
    const uint8_t v = actor.variant;
    if (v == 2 || v == 4) actor.spin.y = 0;
    else if (v == 3) actor.spin.y = kSlowSpin;
    actor.wasOnPlatform = actor.onPlatform;
}

// exe 0x5900c0: a stand-on solid (variant != 0: the shadow list entry exe 0x5f20f0(a, 0x18, 0x100, 0), render output)
void stillSolid(StaticActor& actor) {
    actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0); // exe 0x414050(a, 1, -1, 0)
}

// exe 0x58ffa0
void still(StaticActor& actor) {
    actor.savePreviousPose();
    actor.wasAirborne = actor.airborne;
    switch (StillState(actor.state)) {
    case StillState::Init: stillInit(actor); return;
    case StillState::Stand: // exe 0x5900a0: by the variant [0x8cdcb4]
        switch (actor.variant) {
        case 0:
        case 1: stillSolid(actor); return;
        case 2:
        case 3: return; // exe 0x52ef00
        default: notPortedAt(kStoryPort, "static 0x58ffa0 variant", actor.variant);
        }
    default: notPortedAt(kStoryPort, "static 0x58ffa0 state", actor.state);
    }
}

// ---- 0x417250: pool e0 type 1, the sun ------------------------------------------------------------------------------
// [0x889f78] (2 / 3 = 0x417510 / 0x417540, a ring drawn into the OT, are not reached from the spawn: state 0 only
// steps to 1)
enum class SunState : uint8_t { Init = 0, Draw = 1 };


// One disc of a flare chain (0x10 bytes; a chain ends at a factor of kChainEnd): its offset factor along the angle, its
// size, its texture word and its two colours
struct FlareDisc : DataRecord {
    static constexpr uint32_t kBytes = 0x10;
    int16_t factor = 0; // +0x0
    uint16_t size = 0; // +0x2
    uint32_t texture = 0; // +0x4
    uint32_t colour = 0; // +0x8
    uint32_t colour2 = 0; // +0xc
    template <class R> static FlareDisc load(const R& r) {
        FlareDisc v;
        v.factor = r.template read<int16_t>(0x0);
        v.size = r.template read<uint16_t>(0x2);
        v.texture = r.template read<uint32_t>(0x4);
        v.colour = r.template read<uint32_t>(0x8);
        v.colour2 = r.template read<uint32_t>(0xc);
        return v;
    }
};
constexpr int16_t kChainEnd = 0x7fff;
// [0]: the shared chain, [1 + variant]: the variant's (exe data 0x889f70: the chains' addresses, as the sun stores them:
// the address is the record's identity)
const DataTable<ScalarTable<uint32_t, 8>> flareChains{TableId::St18FlareChains};
constexpr uint32_t kOddFrameDim = 0x80808; // the colours darker on odd frames
constexpr int16_t kScreenCentreX = 0xa0, kScreenCentreY = 0xf0;

// exe 0x4173a0(screen point, chain, dist, angle): one 0xf4 record (a disc in screen space) per disc of the chain, along
// the angle at offset (dist - 0x200) * factor; on odd frames the colours darker
void flareChain(uint32_t owner, const int16_t sxy[2], uint32_t chain, int16_t dist, uint32_t angle) {
    const uint32_t dim = (gameState().frame & 1) ? kOddFrameDim : 0u;
    uint32_t e = flareChains[chain];
    int32_t k = dataRecord<FlareDisc>(e, FlareDisc::kBytes).factor;
    if (k == kChainEnd) return;
    const int32_t r = int32_t(dist) - 0x200;
    for (;;) {
        const FlareDisc disc = dataRecord<FlareDisc>(e, FlareDisc::kBytes);
        const int16_t a16 = int16_t(uint16_t(angle));
        if (a16 < 0x400 || a16 > 0xc00) k = -k;
        const int32_t d = divideRoundingToZero(int32_t(uint32_t(r) * uint32_t(k)), 10);
        const int16_t x =
            wrap16(divideRoundingToZero(int32_t(uint32_t(int32_t(fixedSin(int(angle)))) * uint32_t(d)), 10) + sxy[0]);
        const int16_t y =
            wrap16(divideRoundingToZero(int32_t(uint32_t(int32_t(fixedCos(int(angle)))) * uint32_t(d)), 11) + sxy[1]);
        int32_t c1 = int32_t(disc.colour - dim);
        if (c1 < 0) c1 = 0;
        const uint32_t v = disc.colour2;
        int32_t c2 = int32_t(v - dim);
        if (c2 < 0) c2 = 0;
        const uint32_t w0 = ((uint16_t(cameraState().viewAngles.y) >> 4) & 0x3fu) << 16 | 0x4000u;
        dc::emit({w0, uint32_t(c1) | 0xf4000000u, uint32_t(uint16_t(x)) | uint32_t(uint16_t(y)) << 16,
                  (v & 0xff000000u) | uint32_t(c2), uint32_t(disc.size) << 16 | 1u, disc.texture},
                 owner);
        e += 0x10;
        k = dataRecord<FlareDisc>(e, FlareDisc::kBytes).factor;
        if (k == kChainEnd) return;
    }
}

// exe 0x417270: the variant's flare chain
void sunInit(ControllerActor& actor, SunFields& sun) {
    sun.flareChain = flareChains[actor.variant];
    actor.state += 1u;
}

// exe 0x417290: the sun projected (bent toward the screen edges), its two flare chains drawn while the camera looks up
// and it is near the screen
void sunDraw(ControllerActor& actor, SunFields& sun) {
    const ShortVec3& eye = cameraTransform().eyePoint;
    const int16_t pos[3] = {wrap16(sun.point.x + eye.x), sun.point.y, wrap16(sun.point.z + eye.z)};
    int16_t s[2] = {0, 0};
    projectOnScreen(pos, s); // exe 0x5f2dd0 (the result is not tested)
    s[0] = wrap16(divideRoundingToZero((int32_t(s[0]) - kScreenCentreX) * 25, 4) + kScreenCentreX);
    int32_t ex = int32_t(s[0]) - kScreenCentreX;
    if (ex < 0) ex = kScreenCentreX - int32_t(s[0]);
    const uint32_t bend = (uint32_t(int32_t(s[1]) - kScreenCentreY) * uint32_t(ex)) * uint32_t(ex);
    s[1] = wrap16(int32_t(s[1]) + wrap16(int32_t(bend * 7u) >> 0x15));
    const CameraState& camera = cameraState();
    const int16_t dist = signedAngle(uint16_t(camera.viewAngles.x)); // the view's pitch (12 bits, signed)
    if (dist < 0) return;
    const uint32_t ang = uint16_t(camera.viewAngles.y) & 0xfffu;
    if (uint16_t(s[0] + 0x50) > 0x1e0 || uint16_t(s[1] + 0x50) > 0x190) return;
    flareChain(actor.at, s, 0, dist, ang);
    flareChain(actor.at, s, uint32_t(actor.variant) + 1, dist, ang);
}

// exe 0x417250
void sun(ControllerActor& actor) {
    auto&& sun = classState<SunFields>(actor);
    switch (SunState(actor.state)) {
    case SunState::Init: sunInit(actor, sun); return;
    case SunState::Draw: sunDraw(actor, sun); return;
    default: notPortedAt(kStoryPort, "e0 0x417250 state", actor.state);
    }
}

// ---- 0x5abb70: pool 0x60 type 0x2d subs 1 / 3, the square's still model ----------------------------------------------
enum class SquarePropState : uint8_t { Init = 0, Stand = 1 };

// exe 0x5abb70: the previous pose kept; at its init the model (variant 0), one hit point, no motion, no box
void squareProp(StaticActor& actor) {
    actor.tint = kTintNeutral;
    actor.prevPos = actor.pos;
    actor.wasAirborne = actor.airborne;
    actor.prevRotation = actor.rotation;
    actor.wasOnPlatform = actor.onPlatform;
    if (SquarePropState(actor.state) != SquarePropState::Init) return;
    if (!bindActorModel(actor, 0)) {
        freeActor(actor);
        return;
    }
    actor.maxHitPoints = 1;
    actor.hitPoints = 1;
    actor.velocity.clearWords();
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.collisionBox = nullptr;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.clearClassBlock(); // exe 0x40dc40
    actor.enterState(uint8_t(SquarePropState::Stand));
}

} // namespace

void st18StillModelUpdate(const PlayerEnv&, uint32_t address) { still(staticActorAt(address)); }
void st18SunUpdate(const PlayerEnv&, uint32_t address) { sun(controllerActorAt(address)); }
void st18SquarePropUpdate(const PlayerEnv&, uint32_t address) { squareProp(staticActorAt(address)); }

} // namespace od::port
