// The crumbling platform of the pirate base's gate rooms (ST41 / ST42 static type 0x1f sub 1, exe 0x5a4c60;
// hypothesis names). Its mode (its first spawn parameter): 0 the platform, 1 a falling piece, 2 / 3 bouncing debris.
// States [0x8ced2c]: 0 start 0x5a4cb0, 1 run 0x5a4d50, 2 bounce 0x5a4eb0, 3 freed 0x5afdd0.
// The platform (variant 0), once the gate is open (flag 0x155): the player standing on it wears it down -- its model
// cracked at 0x28 (sound 0x2e7), at 0 it breaks (sound 0x2e8) into 3 pieces, which fall to the ground.
#include "content/saul_kada/st41_crumbling_platform.hpp"

#include "engine/data_source.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/field_alias.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

enum class CrumbleState : uint8_t { Start = 0, Run = 1, Bounce = 2, Free = 3 };
enum class CrumbleMode : uint8_t { Platform = 0, Piece = 1, Debris = 2, LastDebris = 3 };
enum class PlatformPhase : uint8_t { Start = 0, Stand = 1, Broken = 2 };
// Its spawn parameters: its mode, a piece's model (its index in the piece models), stood on this tick
struct CrumbleParams {
    explicit CrumbleParams(const StaticActor& actor) : CrumbleParams(const_cast<StaticActor&>(actor)) {}
    explicit CrumbleParams(StaticActor& actor) : mode(actor.params[0]), pieceModel(actor.params[1]), stoodOn(actor.params[3]) {}
    uint8_t& mode;
    FieldAlias<int8_t, uint8_t> pieceModel;
    uint8_t& stoodOn;
};
// Its own bytes: the ground height (a piece; the acceleration's y lane), its timer (the acceleration's spare lane)
struct CrumbleFields {
    explicit CrumbleFields(const StaticActor& actor) : CrumbleFields(const_cast<StaticActor&>(actor)) {}
    explicit CrumbleFields(StaticActor& actor) : groundY(actor.acceleration.y), timer(actor.acceleration.spare) {}
    int16_t& groundY;
    FieldAlias<uint16_t, int16_t> timer;
};
constexpr uint32_t kFlagGateOpen = 0x155;
const DataTable<ScalarTable<uint8_t, 4>> platformModels{TableId::St41CrumblingPlatformModels}; // by variant
const DataTable<ScalarTable<uint8_t, 8>> pieceModels{TableId::St41CrumblingPlatformPieceModels}; // by the piece's model index (1: the cracked platform)
// the pieces' offsets from the platform along its yaw (s16 x / z)
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
const DataTable<RecordTable<PieceOffset, 3, 4>> pieceOffsets{TableId::St41CrumblingPlatformPieceOffsets};
const DataTable<ScalarTable<uint16_t, 4>> brokenTimers{TableId::St41CrumblingPlatformBrokenTimers}; // by variant
constexpr uint8_t kCrackedModel = 1, kFirstPieceModel = 2;
constexpr uint16_t kCrackSound = 0x2e7, kBreakSound = 0x2e8;
constexpr uint16_t kCrackAt = 0x28;
constexpr uint32_t kRiderMatch = 0x1fffff; // the address bits the player's platform word is compared on

// exe 0x5a4f20: 3 pieces (mode 1, models 2..4) at their offsets along its yaw, 0x1e lower, their rotation jittered
void breakApart(const StaticActor& actor) {
    for (size_t k = 0; k < 3; ++k) {
        const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (!slot) continue;
        StaticActor& piece = staticActorAt(slot);
        CrumbleParams params(staticActorAt(slot));
        piece.flags = kActorLive;
        piece.type = actor.type;
        piece.subtype = actor.subtype;
        piece.loopFlags |= kActorSweepGroup2;
        piece.variant = actor.variant;
        params.mode = uint8_t(CrumbleMode::Piece);
        params.pieceModel = int8_t(kFirstPieceModel + k);
        const PieceOffset offset = pieceOffsets[k];
        int32_t point[3] = {actor.pos.x, actor.pos.y, actor.pos.z}; // the exe's stack copy
        int32_t d[3];
        rotateByYaw(actor.rotation.y, offset.x, 0, offset.z, d); // exe 0x5e36f0
        for (int i = 0; i < 3; ++i) point[i] = int32_t(uint32_t(point[i]) + uint32_t(d[i]));
        point[1] = int32_t(uint32_t(point[1]) + (0x1eu << 16));
        piece.pos.x = point[0];
        piece.pos.y = point[1];
        piece.pos.z = point[2];
        piece.rotation = actor.rotation;
        piece.rotation.x = int16_t(uint16_t(uint16_t(piece.rotation.x) + uint16_t((crtRand() & 0xff) - 0x80u)));
        piece.rotation.y = int16_t(uint16_t(uint16_t(piece.rotation.y) + uint16_t((crtRand() & 0xff) - 0x80u)));
        piece.rotation.z = int16_t(uint16_t(uint16_t(piece.rotation.z) + uint16_t((crtRand() & 0xff) - 0x80u)));
    }
}
// exe 0x5a4cb0: its model (the platform's by variant, a piece's by its index; the exe stays here until it binds)
void start(const PlayerEnv& env, StaticActor& actor, CrumbleFields& crumble) {
    const CrumbleParams params(actor);
    actor.enterState(uint8_t(CrumbleState::Run));
    actor.clearClassBlock(); // exe 0x40dc40
    if (CrumbleMode(params.mode) == CrumbleMode::Platform) {
        if (!bindActorModel(actor, platformModels[actor.variant])) return; // exe 0x5e0bc0
        actor.collisionBox.pointAt(TableId::St41CrumblingPlatformBox);
    } else {
        if (!bindActorModel(actor, pieceModels[size_t(int32_t(params.pieceModel.get()))])) return;
        if (CrumbleMode(params.mode) == CrumbleMode::Piece) {
            actor.collisionBox.pointAt(TableId::St41CrumblingPlatformPieceBox);
            actor.velocity.z = 0;
            actor.velocity.y = 0;
            actor.velocity.x = 0;
            crumble.groundY = int16_t(uint16_t(actorGround(env, actor))); // exe 0x40f190
        }
    }
    actor.airborne = 0;
    actor.flags |= kActorDrawn;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
}
// the player standing on it (his platform its slot)
bool playerOnIt(const StaticActor& actor) {
    const Player& p = player();
    return p.onPlatform != 0 && ((slotIdentityWord(p.platform) ^ slotIdentityWord(actor)) & kRiderMatch) == 0;
}
// exe 0x5a4d50
void run(StaticActor& actor, CrumbleFields& crumble) {
    CrumbleParams params(actor);
    if (CrumbleMode(params.mode) != CrumbleMode::Platform) {
        if (CrumbleMode(params.mode) == CrumbleMode::LastDebris) {
            ++actor.state;
            integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z);
            return;
        }
        actor.velocity.y += 0x30u; // falls until 0x20 above the ground
        if (int32_t(crumble.groundY) <= int32_t(actor.pos.y.whole) + 0x20) ++actor.state;
        integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z);
        return;
    }
    switch (PlatformPhase(actor.subState)) {
    case PlatformPhase::Start:
        ++actor.subState;
        actor.flags |= kActorDrawn;
        crumble.timer = 0x50;
        params.stoodOn = 1;
        return;
    case PlatformPhase::Stand:
        params.stoodOn = 1;
        // variant 0, after the gate, the player standing on it: worn down
        if (actor.variant == 0 && storyFlag(kFlagGateOpen) && playerOnIt(actor)) {
            crumble.timer -= 2u;
            const uint32_t left = crumble.timer & 0xfffeu;
            if (left == kCrackAt) { // cracked
                bindActorModel(actor, pieceModels[kCrackedModel]); // exe 0x5e0bc0
                soundPlayAtPos(kCrackSound, actor.pos);
            } else if (left == 0) { // broken
                actor.flags ^= kActorDrawn;
                ++actor.subState;
                actor.step = 0;
                crumble.timer = brokenTimers[actor.variant];
                actorDetachRiders(actor); // exe 0x414e70
                breakApart(actor);
                soundPlayAtPos(kBreakSound, actor.pos);
                return;
            }
        }
        actorCollisionSubmitSolid(actor, SolidType::StandOn, 0, 0); // exe 0x414050
        return;
    default: return;
    }
}
// exe 0x5a4eb0: the debris bounce (half its speed back up) and blink 0xc ticks; the others freed
void bounce(StaticActor& actor, CrumbleFields& crumble) {
    const int8_t mode = int8_t(CrumbleParams(actor).mode);
    if (mode < 2 || mode > 3) {
        actor.enterState(uint8_t(CrumbleState::Free));
        return;
    }
    if (actor.subState == 0) {
        crumble.timer = 0xc;
        actor.subState = 1;
        actor.airborne = 1;
        actor.velocity.y = int16_t(uint16_t((-int32_t(actor.velocity.y)) >> 1));
    }
    actor.velocity.y += 0x30u;
    actor.flags ^= kActorDrawn;
    crumble.timer -= 1u;
    const int16_t vy = actor.velocity.y;
    if (crumble.timer == 0) actor.enterState(uint8_t(CrumbleState::Free));
    integrateVelocity(actor, actor.velocity.x, vy, actor.velocity.z);
}
void crumblingPlatform(const PlayerEnv& env, StaticActor& actor) { // exe 0x5a4c60
    CrumbleFields crumble(actor);
    CrumbleParams(actor).stoodOn = 0;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (CrumbleState(actor.state)) {
    case CrumbleState::Start: start(env, actor, crumble); return;
    case CrumbleState::Run: run(actor, crumble); return;
    case CrumbleState::Bounce: bounce(actor, crumble); return;
    case CrumbleState::Free: freeActor(actor); return; // 0x5afdd0 -> exe 0x5e1540
    default: notPortedAt(kStoryPort, "0x5a4c60 state (table 0x8ced2c)", actor.state);
    }
}

} // namespace

void crumblingPlatformUpdate(const PlayerEnv& env, uint32_t actor) { crumblingPlatform(env, staticActorAt(actor)); }

} // namespace od::port
