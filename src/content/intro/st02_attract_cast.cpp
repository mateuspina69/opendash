// The attract intro's cast in ST02 (see st02_attract_cast.hpp). The pool-0x20 classes share a state 0 (the model
// bound, none: freed; the class block cleared, the box, animation 0, the motion cleared; then their own bytes and state
// 1) and an animation tail: a new animation starts at the class's start key; else, while its hold bit is set, the
// animation restarts at the frame it showed (the frame copied each tick); else it steps. Their variants key on the
// cast's copy of the timeline; the ticks are the timeline, the animation numbers the classes' model-bank animations.
// The shadow list entries (exe 0x5f20f0(actor, 0x30, 0x160, 0)) are render output; their ground probe is not
// run (an approximation).
#include "content/intro/st02_attract_cast.hpp"
#include "content/intro/st02_attract_cast_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/cutscene_actor.hpp"
#include "game/field_alias.hpp"
#include "game/game_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st02_attract_cast_state;

namespace {

// +8 of every class of the ST02 cast: bound (state 0), then its variant runs
enum class St02CastState : uint8_t { Bind = 0, Runs = 1 };

// the cast's copy of the timeline (script 3 runs exe 0x537c50)

constexpr uint8_t kHoldFrame = 0x01; // the animation restarts at the frame it shows

// the common state 0: false once freed (no model in the room's bank)
bool st02CastBind(AnimatedActor& a, uint32_t box) {
    if (!bindActorModel(a, 0)) { // exe 0x5e0ba0
        freeActor(a);            // exe 0x5e1490
        return false;
    }
    a.clearClassBlock();
    a.collisionBox.pointAt(box);
    a.anim = 0;
    a.prevAnim = kNoAnimShown;
    a.airborne = 0;
    a.velocity.x = 0;
    a.velocity.y = 0;
    a.velocity.z = 0;
    a.acceleration.x = 0;
    a.acceleration.y = 0;
    a.acceleration.z = 0;
    a.spin.assign(0, 0, 0);
    return true;
}
// the hold block cleared (the state 0's own bytes)
void clearHold(HoldBlock& h) {
    h.flags = 0;
    h.frame = 0;
    h.startKey = 0;
}
// the animation tail (exe 0x5e1c30 / 0x5e1e80)
void st02AnimTail(AnimatedActor& a, const HoldBlock& h) {
    const uint8_t anim = a.anim;
    if (anim != a.prevAnim) animSetPose(a, anim, h.startKey);
    else if (h.flags & kHoldFrame) animSetPose(a, anim, int(h.frame));
    else animStepPose(a);
}
// the variants' hold bit: cleared each tick, set where the variant holds its frame
void holdFrame(HoldBlock& h, bool on, uint8_t cleared) { h.flags = on ? uint8_t(cleared | kHoldFrame) : cleared; }
// the variants' motion: backwards by the forward speed / and sideways by the side speed
void moveBack(AnimatedActor& a) {
    if (a.velocity.z != 0) integrateVelocity(a, 0, 0, -int(a.velocity.z));
}
// ... and by the side speed too
void moveBackSide(AnimatedActor& a) {
    if (a.velocity.x != 0 || a.velocity.z != 0) integrateVelocity(a, -int(a.velocity.x), 0, -int(a.velocity.z));
}
// the class's variant (the exe tables by the variant byte, signed: the named routines of engine/routines.csv); a
// variant past the table: none (the class's port gap)
template <size_t N> RoutineId variantOf(const DataTable<RoutineTable<N>>& table, ActorHeader& a) {
    const int32_t v = int32_t(AttractCastVariant(a).index);
    return v >= 0 && size_t(v) < N ? table[size_t(v)].id() : RoutineId::None;
}
// the face tracks' selections (exe 0x5e33d0 / 0x5e33f0)
void eyes(KeyframeTracks& t, uint8_t seq) { t.first.select(seq, 0); }
// (the mouth track's)
void mouth(KeyframeTracks& t, uint8_t seq) { t.second.select(seq, 0); }

// ---- the prop (exe 0x48a0f0, type 0x27, variants 0x8923e4: 0x48a250)
// ------------------------------------------------
// backwards 0x40 a tick from steps 0 / 1, turning (on the frame clock) while far from the centre; step 2 tick 0 puts it
// back at the start
const DataTable<RoutineTable<1>> propVariants{TableId::St02AttractCastPropVariants};
constexpr int16_t kPropScale = 0x800, kPropSpeed = 0x40, kPropTurnZone = 0x191;
constexpr int32_t kPropRestartZ = 0x7260000;

// exe 0x48a0f0: the state 0, the variant, the animation tail
void st02Prop(AnimatedActor& a) {
    HoldBlock& h = classState<HoldOnlyFields>(a).hold;
    castSavePose(a);
    h.frame = a.animKey.frame;
    if (St02CastState(a.state) == St02CastState::Bind) {
        if (!st02CastBind(a, stateAddress(TableId::St02AttractCastPropBox))) return;
        a.flags |= kActorScenery;
        clearHold(h);
        a.scale.x = kPropScale;
        a.scale.y = kPropScale;
        a.scale.z = kPropScale;
        a.enterState(uint8_t(St02CastState::Runs));
    }
    switch (variantOf(propVariants, a)) {
    case RoutineId::St02CastPropDrifts: {
        const uint8_t sub = scriptState().part<CastTimelineStep>().stepSub;
        const bool first = scriptState().part<CastTimelineTick>().stepTick == 0;
        bool turns = true;
        if (sub == 0 || sub == 1) {
            if (first) a.velocity.z = kPropSpeed; // (step 0 writes 0 first)
        } else {
            if (sub == 2 && first) {
                a.rotation.y = 0;
                a.pos.x = 0;
                a.pos.z = kPropRestartZ;
                a.velocity.z = kPropSpeed;
            }
            turns = false;
        }
        if (turns) {
            const int16_t z = a.pos.z.whole;
            int clock = -1;
            const uint16_t frame = gameState().frame;
            if (z >= kPropTurnZone) clock = uint8_t(frame) & 1;
            else if (z <= -kPropTurnZone) clock = uint8_t(frame) & 3;
            if (clock == 0) ++a.rotation.y;
        }
        moveBack(a);
        break;
    }
    default: notPortedAt(kStoryPort, "0x48a0f0 variant", AttractCastVariant(a).index);
    }
    if (!(a.flags & kActorLive)) return;
    st02AnimTail(a, h);
}

// ---- the drifting static (exe 0x59d000, static type 0x1a, variants 0x8ce88c: 0x59d0a0)
// ------------------------------
// still at step 0, backwards 8 a tick from steps 1 / 2
const DataTable<RoutineTable<1>> driftingVariants{TableId::St02AttractCastDriftingVariants};
constexpr int16_t kDriftingScale = 0x100, kDriftingSpeed = 8;

// exe 0x59d000: the state 0, the variant
void st02DriftingStatic(StaticActor& a) {
    if (St02CastState(a.state) == St02CastState::Bind) {
        if (!bindActorModel(a, a.variant)) { // exe 0x5e0bc0 (the model of the variant)
            freeActor(a);                    // exe 0x5e1540
            return;
        }
        a.clearClassBlock();
        a.collisionBox.pointAt(TableId::St02AttractCastDriftingBox);
        a.airborne = 0;
        a.rotation.z = 0;
        a.rotation.y = 0;
        a.rotation.x = 0;
        a.velocity.z = 0;
        a.velocity.y = 0;
        a.velocity.x = 0;
        a.scale.z = kDriftingScale;
        a.scale.y = kDriftingScale;
        a.scale.x = kDriftingScale;
        a.enterState(uint8_t(St02CastState::Runs));
    }
    switch (variantOf(driftingVariants, a)) {
    case RoutineId::St02CastDrifts: {
        const uint8_t sub = scriptState().part<CastTimelineStep>().stepSub;
        const bool first = scriptState().part<CastTimelineTick>().stepTick == 0;
        if (sub == 0) {
            if (first) a.velocity.z = 0;
        } else if (sub == 1 || sub == 2) {
            if (first) a.velocity.z = kDriftingSpeed;
        }
        if (a.velocity.z != 0) integrateVelocity(a, 0, 0, -int(a.velocity.z));
        break;
    }
    default: notPortedAt(kStoryPort, "0x59d000 variant", AttractCastVariant(a).index);
    }
}

// ---- Roll (exe 0x475c80, type 0x16, variants 0x89078c: 0x475e00 / 0x476610)
// -----------------------------------------
// her face on the texture tracks (sequences 0x890830 / 0x890900), on the ground; the hold block after the tracks
const DataTable<RoutineTable<2>> rollVariants{TableId::St02AttractCastRollVariants};

void rollFirst(AnimatedActor& a, KeyframeTracks& t, HoldBlock& h) { // exe 0x475e00
    const uint8_t cleared = uint8_t(h.flags & ~kHoldFrame);
    h.flags = cleared;
    const int32_t k = scriptState().part<CastTimelineTick>().stepTick;
    auto anim = [&](uint8_t n) { a.anim = n; };
    auto speed = [&](int16_t v) { a.velocity.z = v; };
    switch (scriptState().part<CastTimelineStep>().stepSub) {
    case 1:
        if (k == 1) {
            anim(1);
            speed(0x50);
        } else if (k == 0x47) {
            anim(2);
            speed(0x20);
        } else if (k == 0x5d) {
            speed(0);
        }
        break;
    case 2:
        switch (k) {
        case 0: eyes(t, 0); mouth(t, 0); break;
        case 0xf: case 0x2c: eyes(t, 1); break;
        case 0x28: anim(3); break;
        case 0x40: case 0x5e: mouth(t, 1); break;
        case 0x54: case 0x87: mouth(t, 0); break;
        case 0x90: eyes(t, 3); anim(4); break;
        default: break;
        }
        break;
    case 4:
        switch (k) {
        case 5: anim(5); break;
        case 10: eyes(t, 4); break;
        case 0x28: case 0x65: mouth(t, 3); break;
        case 0x51: case 0xb1: mouth(t, 0); break;
        case 0x8d: anim(6); break;
        default: break;
        }
        break;
    case 5: break;
    case 7:
        switch (k) {
        case 0x182: eyes(t, 6); anim(8); break;
        case 0x195: case 0xef: case 0x153: mouth(t, 0); break;
        case 0x1df: eyes(t, 3); mouth(t, 2); anim(9); break;
        case 0x16f: case 0x112: mouth(t, 3); break;
        case 200: eyes(t, 5); mouth(t, 3); anim(7); break;
        default: break;
        }
        break;
    case 8:
        switch (k) {
        case 0x5a: speed(-0x48); break;
        case 5: anim(10); speed(0); break;
        case 0xc: textureTrackStop(a, t, FaceTrack::Mouth, 2); break; // exe 0x5e3660
        case 0x12: case 0x36: mouth(t, 1); break;
        case 0x28: eyes(t, 7); mouth(t, 1); break;
        case 0x2f: case 0x113: case 0x144: mouth(t, 0); break;
        case 0x4e: mouth(t, 0); anim(0xb); break;
        case 0x67:
            anim(0xc);
            a.rotation.y = int16_t((uint32_t(uint16_t(a.rotation.y)) - uint32_t(kHalfTurn)) & kAngleMask);
            speed(0x4c);
            break;
        case 0xaf: anim(0xd); speed(0x1d); break;
        case 199: speed(0); break;
        case 0xff: case 0x11c: mouth(t, 3); break;
        default: break;
        }
        break;
    case 9:
        switch (k) {
        case 5: eyes(t, 8); mouth(t, 1); anim(0xe); break;
        case 0x41: case 0x7a: mouth(t, 0); break;
        case 0x4b: mouth(t, 1); break;
        case 0x7e: eyes(t, 10); break;
        case 0x85: eyes(t, 0xd); mouth(t, 4); break;
        case 0x96: anim(0xf); speed(0x20); break;
        case 0xa0: eyes(t, 1); break;
        case 0xa5: speed(0x4c); break;
        case 0xb3: anim(0xc); break;
        default: break;
        }
        break;
    default: holdFrame(h, true, cleared); break;
    }
    moveBack(a);
}

void rollSecond(AnimatedActor& a, KeyframeTracks& t, HoldBlock& h) { // exe 0x476610
    const uint8_t cleared = uint8_t(h.flags & ~kHoldFrame);
    h.flags = cleared;
    const int32_t k = scriptState().part<CastTimelineTick>().stepTick;
    auto anim = [&](uint8_t n) { a.anim = n; };
    switch (scriptState().part<CastTimelineStep>().stepSub) {
    case 1:
        switch (k) {
        case 0: anim(0x10); a.velocity.z = 0x4e; break;
        case 0x50: eyes(t, 0); mouth(t, 3); break;
        case 0x59: case 0x7b: case 0xd9: mouth(t, 0); break;
        case 0x62: case 0x89: mouth(t, 3); break;
        default: break;
        }
        break;
    case 2:
        if (k == 0) anim(0x11);
        h.flags |= kHoldFrame;
        break;
    case 3:
        switch (k) {
        case 0x86: case 0xbd: mouth(t, 2); break;
        case 0: eyes(t, 0); break;
        case 0x46: eyes(t, 0xe); break;
        case 0x50: anim(0x11); mouth(t, 5); break;
        case 0x90: mouth(t, 5); break;
        case 0x14a: anim(0x12); eyes(t, 9); mouth(t, 4); break;
        default: break;
        }
        if (scriptState().part<CastTimelineTick>().stepTick >= 0 && scriptState().part<CastTimelineTick>().stepTick < 0x50) h.flags |= kHoldFrame;
        break;
    case 4:
        if (k == 10) {
            anim(0x13);
            a.velocity.z = 0x20;
        } else if (k == 0x1f) {
            a.velocity.z = 0;
        }
        break;
    case 5:
        switch (k) {
        case 0xbb: case 0x115: case 0x13e: case 0x5f: case 0x8b: mouth(t, 0); break;
        case 0xca: mouth(t, 1); break;
        case 0x11b: mouth(t, 1); anim(0x14); break;
        case 0xa0: case 0x50: case 0x6d: eyes(t, 0); mouth(t, 1); break;
        case 0xf: textureTrackStop(a, t, FaceTrack::Eyes, 1); break; // exe 0x5e3630
        case 0x19: eyes(t, 0x10); break;
        default: break;
        }
        break;
    case 0xc:
        switch (k) {
        case 0xf0: eyes(t, 3); mouth(t, 1); break;
        case 0xfa: anim(0x16); break;
        case 0x102: case 0x5e: eyes(t, 1); mouth(t, 0); break;
        case 0x116: case 0x20: case 0x6f: case 0xa8: mouth(t, 1); break;
        case 0x141: case 0xd6: case 0x15: case 0x9f: mouth(t, 0); break;
        case 0x146: anim(0x1b); break;
        case 5: anim(0x15); eyes(t, 0); mouth(t, 1); break;
        default: break;
        }
        break;
    case 0xd:
        switch (k) {
        case 0x9c: eyes(t, 10); break;
        case 0xad: case 5: case 0x25: case 0x83: mouth(t, 3); break;
        case 0xaf: a.velocity.x = 0x22; break;
        case 0xe1: case 0x9b: case 0x19: case 0x6c: mouth(t, 0); break;
        case 0xe6: anim(0x18); a.velocity.x = 0xe; break;
        case 0x107: a.velocity.x = 0; break;
        case 0x122: eyes(t, 8); mouth(t, 1); break;
        case 0x87: anim(0x17); break;
        case 0x99: a.velocity.x = 0x12; break;
        default: break;
        }
        break;
    case 0xe:
        switch (k) {
        case 2: case 0x1e: case 0x40: case 0x75: mouth(t, 4); break;
        case 10: mouth(t, 6); anim(0x19); break;
        case 0x29: case 0x4e: mouth(t, 6); break;
        case 0x61: anim(0x1a); break;
        default: break;
        }
        break;
    default: holdFrame(h, true, cleared); break;
    }
    moveBackSide(a);
}

// exe 0x475c80: the state 0 (on the ground, her face tracks), the variant, the tracks, the animation tail
void st02Roll(const PlayerEnv& env, AnimatedActor& a) {
    FaceTracksFields& f = classState<FaceTracksFields>(a);
    HoldBlock& h = classState<FaceTracksFields>(a).hold;
    castSavePose(a);
    h.frame = a.animKey.frame;
    if (St02CastState(a.state) == St02CastState::Bind) {
        if (!st02CastBind(a, stateAddress(TableId::St02AttractCastRollBox))) return;
        clearHold(h);
        a.pos.y.whole = int16_t(actorGround(env, a)); // exe 0x40f190
        f.tracks.init(stateAddress(TableId::St02AttractCastRollEyes), stateAddress(TableId::St02AttractCastRollMouth));
        eyes(f.tracks, 0);
        mouth(f.tracks, 0);
        a.enterState(uint8_t(St02CastState::Runs));
    }
    switch (variantOf(rollVariants, a)) {
    case RoutineId::St02CastRollFirst: rollFirst(a, f.tracks, h); break;
    case RoutineId::St02CastRollSecond: rollSecond(a, f.tracks, h); break;
    default: notPortedAt(kStoryPort, "0x475c80 variant", AttractCastVariant(a).index);
    }
    if (!(a.flags & kActorLive)) return;
    textureTracksTick(a, f.tracks); // exe 0x5e34f0
    st02AnimTail(a, h);
}

// ---- Barrel (exe 0x47bf40, type 0x1a, variants 0x8910cc: 0x47c0b0 / 0x47c590)
// ---------------------------------------
// his two heads (parts 1 / 11) swapped by one part track (sequences 0x8910ec: value 0 the first, 1 the second); the
// hold block after the track
const DataTable<RoutineTable<2>> barrelVariants{TableId::St02AttractCastBarrelVariants};
constexpr size_t kBarrelHead = 1, kBarrelTalkHead = 11;

void barrelFirst(AnimatedActor& a, KeyframeTrack& t, HoldBlock& h) { // exe 0x47c0b0
    const uint8_t cleared = uint8_t(h.flags & ~kHoldFrame);
    h.flags = cleared;
    const int32_t k = scriptState().part<CastTimelineTick>().stepTick;
    auto anim = [&](uint8_t n) { a.anim = n; };
    auto head = [&](uint8_t seq) { t.select(seq, 0); };
    switch (scriptState().part<CastTimelineStep>().stepSub) {
    case 0: case 1: break;
    case 2:
        if (k == 0xa5) anim(1);
        break;
    case 5:
        switch (k) {
        case 0xf: anim(2); head(2); break;
        case 0x26: case 0x48: case 0x94: case 0xc5: head(0); break;
        case 0x34: case 0x66: case 0xa0: head(2); break;
        default: break;
        }
        break;
    case 7:
        switch (k) {
        case 0x1e5: anim(3); head(2); break;
        case 0x1e8: case 0x208: case 0xa8: case 0x1d: case 0x60: head(0); break;
        case 0x1fa: case 0xf: case 0x3b: case 0x77: head(2); break;
        default: break;
        }
        break;
    case 8:
        switch (k) {
        case 0x203: case 600: case 0x280: case 0x179: case 0x1ae: case 0x1cc: head(2); break;
        case 0x224: case 0x272: case 0x2b8: case 0x1f9: case 0x184: case 0x1c7: head(0); break;
        case 0x29: anim(4); break;
        case 0x151: anim(5); break;
        default: break;
        }
        break;
    case 10:
        if (k == 5) anim(6);
        break;
    case 0xb:
        if (k == 5) anim(7);
        break;
    case 0xc:
        switch (k) {
        case 5: anim(8); break;
        case 0x14: a.velocity.z = -0x15; break;
        case 0x35: a.velocity.z = -0x24; break;
        case 0x3c:
            anim(9);
            a.velocity.z = 0x24;
            a.rotation.y = int16_t((uint32_t(uint16_t(a.rotation.y)) - uint32_t(kHalfTurn)) & kAngleMask);
            break;
        default: break;
        }
        break;
    default: holdFrame(h, true, cleared); break;
    }
    moveBack(a);
}

void barrelSecond(AnimatedActor& a, KeyframeTrack& t, HoldBlock& h) { // exe 0x47c590
    const uint8_t cleared = uint8_t(h.flags & ~kHoldFrame);
    h.flags = cleared;
    const int32_t k = scriptState().part<CastTimelineTick>().stepTick;
    switch (scriptState().part<CastTimelineStep>().stepSub) {
    case 2:
        if (k == 0) a.anim = 10;
        h.flags |= kHoldFrame;
        break;
    case 3:
        switch (k) {
        case 0xff: case 0x3c: case 0x14a: t.select(0, 0); break;
        case 5: a.anim = 10; t.select(2, 0); break;
        case 0xd7: case 0x118: t.select(2, 0); break;
        default: break;
        }
        if (scriptState().part<CastTimelineTick>().stepTick >= 0 && scriptState().part<CastTimelineTick>().stepTick < 5) h.flags |= kHoldFrame;
        break;
    case 4:
        if (k == 0x1e) {
            a.anim = 0xb;
        } else if (k == 0x2c) {
            a.velocity.x = -0x1b;
            a.velocity.z = 0x10;
        } else if (k == 0x60) {
            a.velocity.x = 0;
            a.velocity.z = 0;
        }
        break;
    case 0xc:
        if (k == 0) a.anim = 0xc;
        break;
    default: holdFrame(h, true, cleared); break;
    }
    moveBackSide(a);
}

// exe 0x47c6e0: the part track's tick (its key's value: 0 the first head shown, 1 the second)
void barrelHeads(AnimatedActor& a, KeyframeTrack& t) {
    keyframeTrackTick(t, [&](const Keyframe& key) {
        if (key.value == 0) {
            a.parts[kBarrelTalkHead].flags |= kPartHidden;
            a.parts[kBarrelHead].flags &= uint8_t(~kPartHidden);
        } else if (key.value == 1) {
            a.parts[kBarrelHead].flags |= kPartHidden;
            a.parts[kBarrelTalkHead].flags &= uint8_t(~kPartHidden);
        }
    });
}

// exe 0x47bf40: the state 0 (on the ground, the heads' track), the variant, the heads, the animation tail
void st02Barrel(const PlayerEnv& env, AnimatedActor& a) {
    PartTrackFields& f = classState<PartTrackFields>(a);
    HoldBlock& h = classState<PartTrackFields>(a).hold;
    castSavePose(a);
    h.frame = a.animKey.frame;
    if (St02CastState(a.state) == St02CastState::Bind) {
        if (!st02CastBind(a, stateAddress(TableId::St02AttractCastBarrelBox))) return;
        clearHold(h);
        a.pos.y.whole = int16_t(actorGround(env, a));
        f.track.table.pointAt(TableId::St02AttractCastBarrelHeads);
        f.track.clearCursorWord();
        a.enterState(uint8_t(St02CastState::Runs));
    }
    switch (variantOf(barrelVariants, a)) {
    case RoutineId::St02CastBarrelFirst: barrelFirst(a, f.track, h); break;
    case RoutineId::St02CastBarrelSecond: barrelSecond(a, f.track, h); break;
    default: notPortedAt(kStoryPort, "0x47bf40 variant", AttractCastVariant(a).index);
    }
    if (!(a.flags & kActorLive)) return;
    barrelHeads(a, f.track);
    st02AnimTail(a, h);
}

// ---- the placed static (exe 0x59ada0, static type 0x14, variants 0x8ce4ec: 0x59ae40)
// --------------------------------
// variant 1 takes its pitch from its spawn bytes 10 / 11; placed at step 7 tick 0, scaled up at step 0xd
struct PlacedStaticFields {
    explicit PlacedStaticFields(const StaticActor& actor) : PlacedStaticFields(const_cast<StaticActor&>(actor)) {}
    explicit PlacedStaticFields(StaticActor& actor) : pitch(actor.params, 2) {}
    BytesField<int16_t, 4> pitch;
};
const DataTable<RoutineTable<1>> placedVariants{TableId::St02AttractCastPlacedVariants};
constexpr uint8_t kPitchedModel = 1;
constexpr int16_t kPlacedScale = 0x400;

// exe 0x59ada0: the state 0, the variant
void st02PlacedStatic(StaticActor& a) {
    if (St02CastState(a.state) == St02CastState::Bind) {
        if (!bindActorModel(a, a.variant)) {
            freeActor(a);
            return;
        }
        a.clearClassBlock();
        a.collisionBox.pointAt(TableId::St02AttractCastPlacedBox);
        a.airborne = 0;
        a.rotation.z = 0;
        a.rotation.x = 0;
        a.velocity.z = 0;
        a.velocity.y = 0;
        a.velocity.x = 0;
        if (a.variant == kPitchedModel) a.rotation.x = PlacedStaticFields(a).pitch;
        a.enterState(uint8_t(St02CastState::Runs));
    }
    switch (variantOf(placedVariants, a)) {
    case RoutineId::St02CastPlaced: {
        if (scriptState().part<CastTimelineStep>().stepSub == 7) {
            if (scriptState().part<CastTimelineTick>().stepTick == 0) {
                a.pos.x.whole = 0x17;
                a.pos.y.whole = -0x51;
                a.pos.z.whole = 0x18a;
                a.rotation.x = 0xdc0;
                a.rotation.y = 0xf20;
            }
        } else if (scriptState().part<CastTimelineStep>().stepSub == 0xd) {
            a.scale.x = kPlacedScale;
            a.scale.y = kPlacedScale;
            a.scale.z = kPlacedScale;
        }
        break;
    }
    default: notPortedAt(kStoryPort, "0x59ada0 variant", AttractCastVariant(a).index);
    }
}

// ---- the watcher (exe 0x47f800, type 0x1d, variants 0x8915dc: 0x47f950 / 0x47f990 / 0x47f9f0 / 0x47fa50)
// ------------
// no tracks: the hold block at the class block's start; on the ground
const DataTable<RoutineTable<4>> watcherVariants{TableId::St02AttractCastWatcherVariants};
constexpr int16_t kWatcherSpeed = 0x4e;
constexpr uint8_t kOnceToDo = 0, kOnceDone = 1; // variant 3's sub-state: its animation asked once

// variants 0x47f950 / 0x47f990 / 0x47f9f0: at tick 0 of step `sub` the animation (walking back with `walks`); the
// other steps hold the frame
void watcherAtStep(AnimatedActor& a, HoldBlock& h, uint8_t sub, uint8_t anim, bool walks) {
    const uint8_t cleared = uint8_t(h.flags & ~kHoldFrame);
    h.flags = cleared;
    if (scriptState().part<CastTimelineStep>().stepSub != sub) {
        holdFrame(h, true, cleared);
    } else if (scriptState().part<CastTimelineTick>().stepTick == 0) {
        a.anim = anim;
        if (walks) a.velocity.z = kWatcherSpeed;
    }
    if (walks) moveBack(a);
}

// exe 0x47f800: the state 0 (on the ground), the variant, the animation tail
void st02Watcher(const PlayerEnv& env, AnimatedActor& a) {
    HoldBlock& h = classState<HoldOnlyFields>(a).hold;
    castSavePose(a);
    h.frame = a.animKey.frame;
    if (St02CastState(a.state) == St02CastState::Bind) {
        if (!st02CastBind(a, stateAddress(TableId::St02AttractCastWatcherBox))) return;
        clearHold(h);
        a.pos.y.whole = int16_t(actorGround(env, a)); // exe 0x40f190
        a.enterState(uint8_t(St02CastState::Runs));
    }
    switch (variantOf(watcherVariants, a)) {
    case RoutineId::St02CastWatcherLooks: { // exe 0x47f950: step 0 tick 0 -> animation 1 (no motion)
        const uint8_t cleared = uint8_t(h.flags & ~kHoldFrame);
        h.flags = cleared;
        if (scriptState().part<CastTimelineStep>().stepSub != 0) {
            holdFrame(h, true, cleared);
            break;
        }
        if (scriptState().part<CastTimelineTick>().stepTick == 0) a.anim = 1;
        break;
    }
    case RoutineId::St02CastWatcherWalks: watcherAtStep(a, h, 0, 2, true); break;      // exe 0x47f990
    case RoutineId::St02CastWatcherWalksLater: watcherAtStep(a, h, 1, 3, true); break; // exe 0x47f9f0
    case RoutineId::St02CastWatcherOnce:                                              // exe 0x47fa50: animation 4 once
        h.flags &= uint8_t(~kHoldFrame);
        if (a.subState == kOnceToDo) {
            a.anim = 4;
            a.subState = kOnceDone;
        }
        break;
    default: notPortedAt(kStoryPort, "0x47f800 variant", AttractCastVariant(a).index);
    }
    if (!(a.flags & kActorLive)) return;
    st02AnimTail(a, h);
}

// ---- the talking cast without motion (exe 0x480580 type 0x1e, 0x481da0 type 0x21, 0x48aef0 type 0x28, 0x4b5c40
// type 0x3c): the face on the texture tracks, the hold block after them, on the ground in the tail (the first three)
// ----------
struct TalkerClass {
    const DataTable<RoutineTable<1>>& variants;
    TableId box, eyes, mouth; // the box, the face tracks' tables
    RoutineId variant;        // the one variant's routine
    const char* name;
};

// the talking cast's state 0 past the bind: the hold block cleared, the face tracks on their tables, state 1
void talkerInit(AnimatedActor& a, FaceTracksFields& f, HoldBlock& h, const TalkerClass& c) {
    clearHold(h);
    f.tracks.init(stateAddress(c.eyes), stateAddress(c.mouth)); // exe 0x5e33a0
    eyes(f.tracks, 0);
    mouth(f.tracks, 0);
    a.enterState(uint8_t(St02CastState::Runs));
}

// exe 0x480700 (0x480580's variant)
const DataTable<RoutineTable<1>> talkerVariants{TableId::St02AttractCastTalkerVariants};
const TalkerClass kTalker{talkerVariants, TableId::St02AttractCastTalkerBox, TableId::St02AttractCastTalkerEyes, TableId::St02AttractCastTalkerMouth, RoutineId::St02CastTalkerLines,
                          "0x480580 variant"};
// exe 0x480700: 0x480580's lines on steps 7 .. 10
void talkerLines(AnimatedActor& a, KeyframeTracks& t, HoldBlock& h) {
    h.flags &= uint8_t(~kHoldFrame);
    const int32_t k = scriptState().part<CastTimelineTick>().stepTick;
    switch (scriptState().part<CastTimelineStep>().stepSub) {
    case 7:
        if (k == 5) a.anim = 1;
        break;
    case 8:
        switch (k) {
        case 0: a.pos.z.whole = -0x66; break;
        case 0x18: a.anim = 6; break;
        case 0x2c: a.anim = 2; eyes(t, 5); mouth(t, 1); break;
        case 0x6b: case 0xb8: case 0x166: case 0x198: mouth(t, 0); break;
        case 0x75: mouth(t, 1); break;
        case 0x14f: a.anim = 3; eyes(t, 2); mouth(t, 2); break;
        case 0x170: a.anim = 4; mouth(t, 2); break;
        default: break;
        }
        break;
    case 9: // tick 0: z; the frame held for the whole step
        if (k == 0) a.pos.z.whole = -0x5a;
        h.flags |= kHoldFrame;
        break;
    case 10:
        switch (k) {
        case 0: a.pos.z.whole = -0x64; break;
        case 10: eyes(t, 3); mouth(t, 0); break;
        case 0x78: a.anim = 5; eyes(t, 4); mouth(t, 1); break;
        case 0x9e: case 0xcb: mouth(t, 0); break;
        case 0xae: mouth(t, 1); break;
        default: break;
        }
        break;
    default: h.flags |= kHoldFrame; break;
    }
}

// exe 0x481f20 (0x481da0's variant)
const DataTable<RoutineTable<1>> faceVariants{TableId::St02AttractCastFaceVariants};
const TalkerClass kFace{faceVariants, TableId::St02AttractCastFaceBox, TableId::St02AttractCastFaceEyes, TableId::St02AttractCastFaceMouth, RoutineId::St02CastFaceLines, "0x481da0 variant"};
// exe 0x481f20: 0x481da0's lines on steps 7 .. 10
void faceLines(AnimatedActor& a, KeyframeTracks& t, HoldBlock& h) {
    const uint8_t cleared = uint8_t(h.flags & ~kHoldFrame);
    h.flags = cleared;
    const int32_t k = scriptState().part<CastTimelineTick>().stepTick;
    switch (scriptState().part<CastTimelineStep>().stepSub) {
    case 7:
        if (k == 0xb) a.anim = 1;
        break;
    case 8:
        switch (k) {
        case 0x1e: a.anim = 8; break;
        case 0xcd: a.anim = 2; eyes(t, 1); mouth(t, 1); break;
        case 0xdd: case 0x13c: mouth(t, 5); break;
        case 0xef: mouth(t, 1); break;
        case 0x1a4: a.anim = 3; break;
        case 0x1cd: a.anim = 9; break;
        default: break;
        }
        break;
    case 9:
        if (k == 0x7a) a.anim = 4;
        break;
    case 10:
        switch (k) {
        case 5: a.anim = 5; eyes(t, 2); mouth(t, 3); break;
        case 0xd: case 0x28: case 0x69: mouth(t, 2); break;
        case 0x13: case 0x3a: mouth(t, 3); break;
        case 0x82: a.anim = 6; eyes(t, 4); mouth(t, 4); break;
        case 0xff: a.anim = 7; eyes(t, 3); mouth(t, 0); break;
        default: break;
        }
        break;
    default: holdFrame(h, true, cleared); break;
    }
}

// exe 0x48b070 (0x48aef0's variant): placed on steps 9 / 10 tick 0
const DataTable<RoutineTable<1>> speakerVariants{TableId::St02AttractCastSpeakerVariants};
const TalkerClass kSpeaker{speakerVariants, TableId::St02AttractCastSpeakerBox, TableId::St02AttractCastSpeakerEyes, TableId::St02AttractCastSpeakerMouth, RoutineId::St02CastSpeakerLines,
                           "0x48aef0 variant"};
// exe 0x48b070: 0x48aef0's lines on steps 7 .. 10
void speakerLines(AnimatedActor& a, KeyframeTracks& t, HoldBlock& h) {
    const uint8_t cleared = uint8_t(h.flags & ~kHoldFrame);
    h.flags = cleared;
    const int32_t k = scriptState().part<CastTimelineTick>().stepTick;
    switch (scriptState().part<CastTimelineStep>().stepSub) {
    case 7:
        if (k == 8) a.anim = 1;
        break;
    case 8:
        if (k == 0) a.pos.z.whole = -0x78;
        break;
    case 9:
        switch (k) {
        case 0:
            a.pos.x.whole = 0x3e;
            a.pos.z.whole = -0x78;
            a.rotation.y = 0x76c;
            eyes(t, 1);
            break;
        case 10: a.anim = 2; mouth(t, 2); break;
        case 0x1e: case 0x5a: eyes(t, 2); break;
        case 0x7a: eyes(t, 1); mouth(t, 0); break;
        default: break;
        }
        break;
    case 10:
        switch (k) {
        case 0:
            a.pos.x.whole = 0x39;
            a.pos.z.whole = -0x64;
            break;
        case 8: a.anim = 3; eyes(t, 3); mouth(t, 0); break;
        case 0xe1: a.anim = 4; eyes(t, 3); mouth(t, 2); break;
        case 0xea: eyes(t, 5); break;
        case 0xf2: mouth(t, 0); break;
        default: break;
        }
        break;
    default: holdFrame(h, true, cleared); break;
    }
}

// exe 0x480580 / 0x481da0 / 0x48aef0: the variant, then on the ground, the tracks, the animation
void talkerClass(const PlayerEnv& env, AnimatedActor& a, const TalkerClass& c,
                 void (*lines)(AnimatedActor&, KeyframeTracks&, HoldBlock&)) {
    FaceTracksFields& f = classState<FaceTracksFields>(a);
    HoldBlock& h = classState<FaceTracksFields>(a).hold;
    castSavePose(a);
    h.frame = a.animKey.frame;
    if (St02CastState(a.state) == St02CastState::Bind) {
        if (!st02CastBind(a, stateAddress(c.box))) return;
        talkerInit(a, f, h, c);
    }
    if (variantOf(c.variants, a) == c.variant) lines(a, f.tracks, h);
    else notPortedAt(kStoryPort, c.name, AttractCastVariant(a).index);
    if (!(a.flags & kActorLive)) return;
    a.pos.y.whole = int16_t(actorGround(env, a)); // exe 0x40f190 (every tick, in the tail)
    textureTracksTick(a, f.tracks);                 // exe 0x5e34f0
    st02AnimTail(a, h);
}

// ---- the screen (exe 0x4b5c40, type 0x3c, variants 0x894664: 0x4b5db0)
// ----------------------------------------------
// one texture track (sequences 0x894674; the second track has no table: idle), on the ground at its state 0; step 9
// tick 0xf0: the track's sequence 1; the other steps hold the frame
const DataTable<RoutineTable<1>> screenVariants{TableId::St02AttractCastScreenVariants};

// exe 0x4b5c40: the state 0, the variant, the track, the animation tail
void st02Screen(const PlayerEnv& env, AnimatedActor& a) {
    FaceTracksFields& f = classState<FaceTracksFields>(a);
    HoldBlock& h = classState<FaceTracksFields>(a).hold;
    castSavePose(a);
    h.frame = a.animKey.frame;
    if (St02CastState(a.state) == St02CastState::Bind) {
        if (!st02CastBind(a, stateAddress(TableId::St02AttractCastScreenBox))) return;
        clearHold(h);
        a.pos.y.whole = int16_t(actorGround(env, a)); // exe 0x40f190 (once, at its state 0)
        f.tracks.init(stateAddress(TableId::St02AttractCastScreenCells), 0);              // exe 0x5e33a0
        eyes(f.tracks, 0);
        a.enterState(uint8_t(St02CastState::Runs));
    }
    switch (variantOf(screenVariants, a)) {
    case RoutineId::St02CastScreenShows: {
        const uint8_t cleared = uint8_t(h.flags & ~kHoldFrame);
        h.flags = cleared;
        if (scriptState().part<CastTimelineStep>().stepSub != 9) {
            holdFrame(h, true, cleared);
            break;
        }
        if (scriptState().part<CastTimelineTick>().stepTick == 0xf0) eyes(f.tracks, 1);
        break;
    }
    default: notPortedAt(kStoryPort, "0x4b5c40 variant", AttractCastVariant(a).index);
    }
    if (!(a.flags & kActorLive)) return;
    textureTracksTick(a, f.tracks); // exe 0x5e34f0
    st02AnimTail(a, h);
}

// ---- the blinker (exe 0x4848c0, type 0x23, variants 0x892084: 0x484a30)
// ----------------------------------------------
// one part track (sequences 0x8920a8: value 0 shows part 1 and hides part 10, value 1 the reverse); step 2 only (any
// other step holds the frame); no motion
const DataTable<RoutineTable<1>> blinkerVariants{TableId::St02AttractCastBlinkerVariants};
constexpr size_t kBlinkerOpen = 1, kBlinkerShut = 10;

// exe 0x484b80: the part track's tick
void blinkerParts(AnimatedActor& a, KeyframeTrack& t) {
    keyframeTrackTick(t, [&](const Keyframe& key) {
        if (key.value == 0) {
            a.parts[kBlinkerShut].flags |= kPartHidden;
            a.parts[kBlinkerOpen].flags &= uint8_t(~kPartHidden);
        } else if (key.value == 1) {
            a.parts[kBlinkerOpen].flags |= kPartHidden;
            a.parts[kBlinkerShut].flags &= uint8_t(~kPartHidden);
        }
    });
}

// exe 0x4848c0: the state 0, the variant, the part track, the animation tail
void st02Blinker(const PlayerEnv& env, AnimatedActor& a) {
    PartTrackFields& f = classState<PartTrackFields>(a);
    HoldBlock& h = classState<PartTrackFields>(a).hold;
    castSavePose(a);
    h.frame = a.animKey.frame;
    if (St02CastState(a.state) == St02CastState::Bind) {
        if (!st02CastBind(a, stateAddress(TableId::St02AttractCastBlinkerBox))) return; // exe 0x5e0ba0 / 0x5e1490 / 0x40dc40
        clearHold(h);
        a.pos.y.whole = int16_t(actorGround(env, a)); // exe 0x40f190
        f.track.table.pointAt(TableId::St02AttractCastBlinkerParts);
        f.track.clearCursorWord();
        a.enterState(uint8_t(St02CastState::Runs));
    }
    switch (variantOf(blinkerVariants, a)) {
    case RoutineId::St02CastBlinkerBlinks: {
        const uint8_t cleared = uint8_t(h.flags & ~kHoldFrame);
        h.flags = cleared;
        if (scriptState().part<CastTimelineStep>().stepSub != 2) {
            holdFrame(h, true, cleared);
            break;
        }
        switch (scriptState().part<CastTimelineTick>().stepTick) { // exe jump table 0x484ab8
        case 5: case 0x1b: case 0x4d: f.track.select(2, 0); break; // exe 0x484b60
        case 10: a.anim = 1; break;
        case 0x10: case 0x43: case 0x8a: f.track.select(0, 0); break;
        case 0x28: a.anim = 2; break;
        case 0x5a: a.anim = 3; break;
        default: break;
        }
        break;
    }
    default: notPortedAt(kStoryPort, "0x4848c0 variant", AttractCastVariant(a).index);
    }
    if (!(a.flags & kActorLive)) return;
    blinkerParts(a, f.track);
    st02AnimTail(a, h);
}

// ---- the stander (exe 0x488f60, type 0x25, variants 0x892394: 0x4890b0)
// ----------------------------------------------
// no tracks; on the ground every tick; step 2 tick 0: animation 0, any other step holds the frame
const DataTable<RoutineTable<1>> standerVariants{TableId::St02AttractCastStanderVariants};

// exe 0x488f60: the state 0, the variant, the ground, the animation tail
void st02Stander(const PlayerEnv& env, AnimatedActor& a) {
    HoldBlock& h = classState<HoldOnlyFields>(a).hold;
    castSavePose(a);
    h.frame = a.animKey.frame;
    if (St02CastState(a.state) == St02CastState::Bind) {
        if (!st02CastBind(a, stateAddress(TableId::St02AttractCastStanderBox))) return;
        clearHold(h);
        a.enterState(uint8_t(St02CastState::Runs));
    }
    switch (variantOf(standerVariants, a)) {
    case RoutineId::St02CastStanderStands: {
        const uint8_t cleared = uint8_t(h.flags & ~kHoldFrame);
        h.flags = cleared;
        if (scriptState().part<CastTimelineStep>().stepSub != 2) {
            holdFrame(h, true, cleared);
            break;
        }
        if (scriptState().part<CastTimelineTick>().stepTick == 0) a.anim = 0;
        break;
    }
    default: notPortedAt(kStoryPort, "0x488f60 variant", AttractCastVariant(a).index);
    }
    if (!(a.flags & kActorLive)) return;
    a.pos.y.whole = int16_t(actorGround(env, a)); // exe 0x40f190 (every tick)
    st02AnimTail(a, h);
}

// ---- the still model and the scenery (exe 0x5ad6f0 static type 0x34, exe 0x59b630 static type 0x16): their state 0
// only (the model of the variant, none: freed; the class block cleared, the box, still); the scenery's spawn bytes
// (kind, index, param) are not read by this class (its ST03 sibling reads them)

// the statics' state 0: the model of the variant (none: freed, false), the class block cleared, the box, on the ground
bool st02StaticBind(StaticActor& a, uint32_t box) {
    if (!bindActorModel(a, a.variant)) { // exe 0x5e0bc0
        freeActor(a);                    // exe 0x5e1540
        return false;
    }
    a.clearClassBlock(); // exe 0x40dc40
    a.collisionBox.pointAt(box);
    a.airborne = 0;
    return true;
}

// exe 0x5ad6f0
void st02StillModel(StaticActor& a) {
    if (St02CastState(a.state) != St02CastState::Bind) return;
    if (!st02StaticBind(a, stateAddress(TableId::St02AttractCastStillModelBox))) return;
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.velocity.z = 0;
    a.velocity.y = 0;
    a.velocity.x = 0;
    a.enterState(uint8_t(St02CastState::Runs));
}

// exe 0x59b630
void st02Scenery(StaticActor& a) {
    if (St02CastState(a.state) != St02CastState::Bind) return;
    if (!st02StaticBind(a, stateAddress(TableId::St02AttractCastSceneryBox))) return;
    a.velocity.z = 0;
    a.velocity.y = 0;
    a.velocity.x = 0;
    a.enterState(uint8_t(St02CastState::Runs));
}

} // namespace

// the classes' updates (st02_attract_cast.hpp)
void st02PropUpdate(const PlayerEnv&, uint32_t actor) { st02Prop(animatedActorAt(actor)); }
void st02DriftingStaticUpdate(const PlayerEnv&, uint32_t actor) { st02DriftingStatic(staticActorAt(actor)); }
void st02RollUpdate(const PlayerEnv& env, uint32_t actor) { st02Roll(env, animatedActorAt(actor)); }
void st02BarrelUpdate(const PlayerEnv& env, uint32_t actor) { st02Barrel(env, animatedActorAt(actor)); }
void st02PlacedStaticUpdate(const PlayerEnv&, uint32_t actor) { st02PlacedStatic(staticActorAt(actor)); }
void st02WatcherUpdate(const PlayerEnv& env, uint32_t actor) { st02Watcher(env, animatedActorAt(actor)); }
void st02TalkerUpdate(const PlayerEnv& env, uint32_t actor) {
    talkerClass(env, animatedActorAt(actor), kTalker, &talkerLines);
}
void st02FaceUpdate(const PlayerEnv& env, uint32_t actor) { talkerClass(env, animatedActorAt(actor), kFace, &faceLines); }
void st02SpeakerUpdate(const PlayerEnv& env, uint32_t actor) {
    talkerClass(env, animatedActorAt(actor), kSpeaker, &speakerLines);
}
void st02ScreenUpdate(const PlayerEnv& env, uint32_t actor) { st02Screen(env, animatedActorAt(actor)); }
void st02BlinkerUpdate(const PlayerEnv& env, uint32_t actor) { st02Blinker(env, animatedActorAt(actor)); }
void st02StanderUpdate(const PlayerEnv& env, uint32_t actor) { st02Stander(env, animatedActorAt(actor)); }
void st02StillModelUpdate(const PlayerEnv&, uint32_t actor) { st02StillModel(staticActorAt(actor)); }
void st02SceneryUpdate(const PlayerEnv&, uint32_t actor) { st02Scenery(staticActorAt(actor)); }

} // namespace od::port
