// The display figures of ST00 (pool 0x20, stage class type 2, exe 0x449cc0; see st00_figures.hpp). The bind
// (exe 0x449d30) loads one model by variant (its own model type, then type 2 again) and enters the variant's state:
//   1 Watching   the head (part 1) turned to the player, blinking every 16 ticks (both texture words)
//   2 Looping    its animation played on
//   3 Random     a random animation 1..6 at each end (animation 4 frame 7: one of three parts shown)
//   4 Target     a hit box: hit by the player -> animation 1
//   5 Scrolling  its texture word's cell scrolled (64 steps)
//   6 Face       a blinking face (texture cells by its clock) whose animation pad 2's right / left cycle (0 .. 0x11)
// All but variant 3 stay on the ground. The animation numbers are the figures' model-bank animations. The shadow under
// them (exe 0x5f20f0(actor, 0x60, 0x160, 0)) is render output; its ground probe is not run (an approximation).
#include "content/intro/st00_figures.hpp"
#include "content/intro/st00_figures_state.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/cutscene_actor.hpp"
#include "game/field_alias.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"

namespace od::port {

using namespace st00_figures_state;

namespace {

inline constexpr const char* kFiguresArea = "ST00 figures";

// +8: the states (exe table 0x88e3b0); the bind enters the variant's (variant + 1)
enum class FigureState : uint8_t { Bind = 0, Watching = 1, Looping = 2, Random = 3, Target = 4, Scrolling = 5, Face = 6 };
// +6: the variants (the model type each binds)
enum class FigureVariant : uint8_t { Watching = 0, Looping = 1, Random = 2, Target = 3, Scrolling = 4, Face = 5 };
constexpr uint8_t kLastVariant = uint8_t(FigureVariant::Face);
constexpr uint8_t kVariantModelTypes[kLastVariant + 1] = {9, 0xb, 0x60, 0x4f, 0x52, 0x5d};
constexpr uint8_t kFigureType = 2; // the class's own type, back once the model is bound

constexpr uint32_t kHeadBone = 1;
constexpr size_t kHeadEntry = 1 + kHeadBone; // the head's angles in the decode buffer (entry 0: the root offset)
constexpr uint32_t kHeadAboveFeet = 0xff800000; // the player's point the head looks at: 0x80 above its position
constexpr int16_t kHeadTurnLimit = 0x200;
constexpr uint8_t kBlinkPeriodMask = 0xf;
constexpr int kBlinkCells = 16;
constexpr uint32_t kTargetShape = hitGroup(kHitGroupBodies) | hitRadius(0xc0);
constexpr uint32_t kTargetAttack = kHitAttackDeflects | 1;
// variant 4's: the scroll step
struct ScrollingFigureFields {
    explicit ScrollingFigureFields(const AnimatedActor& actor) : ScrollingFigureFields(const_cast<AnimatedActor&>(actor)) {}
    explicit ScrollingFigureFields(AnimatedActor& actor) : scrollStep(actor.params[0]) {}
    uint8_t& scrollStep;
};
constexpr uint32_t kTexturePageKeep = 0x3fff0000; // the texture word's page bits (exe 0x5e3360(word, step, 0, 0))
constexpr int8_t kScrollSteps = 0x40;
// variant 5's: the face's clock
struct FaceFigureFields {
    explicit FaceFigureFields(const AnimatedActor& actor) : FaceFigureFields(const_cast<AnimatedActor&>(actor)) {}
    explicit FaceFigureFields(AnimatedActor& actor) : faceClock(actor.params, 0) {}
    BytesField<uint16_t, 4> faceClock;
};
constexpr uint16_t kFaceHalfShut = 0x96, kFaceShut = 0x97, kFaceClosed = 0x98, kFaceOpening = 0x9b,
                   kFaceHalfOpen = 0x9c, kFaceOpen = 0x9d;
constexpr uint8_t kFaceLastAnim = 0x11;
// the parts variant 2's animation 4 shows one of (its frame 7), the others hidden
constexpr size_t kRandomParts[3] = {3, 11, 12};
constexpr uint8_t kRandomPoseAnim = 4;
constexpr int8_t kRandomPoseFrame = 7;
constexpr uint32_t kRandomAnims = 6;

// the figures have no sub-state machine: the bind clears the byte
constexpr uint8_t kNoSubState = 0;

void setScale(AnimatedActor& a, int16_t s) { // z, y, x (the exe's order)
    a.scale.z = s;
    a.scale.y = s;
    a.scale.x = s;
}
// the states' tail: a new animation started at key 0, else stepped (exe 0x5e1c30 / 0x5e1e80)
void figurePose(AnimatedActor& a) {
    if (a.anim != a.prevAnim) animSetPose(a, a.anim, 0);
    else animStepPose(a);
}

// exe 0x449fa0: the head turned to the player (clamped to a quarter of a half turn either way), the animation stepped
// with the head's angles added, the blink
void figureWatching(AnimatedActor& a) {
    WatchingFigureFields& f = classState<WatchingFigureFields>(a);
    const ShortVec4& bone = bonePoint(a, kHeadBone, kFiguresArea); // exe 0x5e1810
    const Player& p = player();
    const int32_t eye[3] = {p.pos.x, int32_t(uint32_t(p.pos.y) + kHeadAboveFeet), p.pos.z};
    const int32_t head[3] = {int32_t(uint32_t(int32_t(bone.x)) << 16), int32_t(uint32_t(int32_t(bone.y)) << 16),
                             int32_t(uint32_t(int32_t(bone.z)) << 16)};
    // exe 0x5e3ca0 on the two 16.16 points: their integer parts
    const int16_t eyeWhole[3] = {int16_t(uint32_t(eye[0]) >> 16), int16_t(uint32_t(eye[1]) >> 16),
                                 int16_t(uint32_t(eye[2]) >> 16)};
    const int16_t headWhole[3] = {bone.x, bone.y, bone.z};
    f.headYaw = int16_t(angleOf(eye[0], eye[2], head[0], head[2])); // exe 0x5e3bc0
    f.headPitch = int16_t(uint32_t(kHalfTurn) - uint32_t(pitchOf(eyeWhole, headWhole)));
    if (f.headYaw >= kHalfTurn) f.headYaw = int16_t(uint32_t(uint16_t(f.headYaw)) - uint32_t(kFullTurn));
    if (f.headYaw < -kHeadTurnLimit) f.headYaw = int16_t(-kHeadTurnLimit);
    if (f.headYaw >= kHeadTurnLimit) f.headYaw = kHeadTurnLimit;
    if (a.anim != a.prevAnim) animSet(a, a.anim, 0); // exe 0x5e1c50
    else animStep(a);                                // exe 0x5e1ea0
    auto&& headAngles = decodedFrame().entries[kHeadEntry];
    headAngles.x = int16_t(uint16_t(headAngles.x) + uint16_t(f.headPitch));
    headAngles.y = int16_t(uint16_t(f.headYaw) + uint16_t(headAngles.y));
    animPose(a); // exe 0x5e2d90
    const uint8_t clock = uint8_t(f.blinkClock + 1);
    f.blinkClock = clock;
    if (clock & kBlinkPeriodMask) return;
    const int8_t cell = int8_t(int8_t(uint8_t(f.blinkCell + 1)) % kBlinkCells);
    f.blinkCell = uint8_t(cell);
    setTexturePageCell(a.texturePage, uint32_t(int32_t(cell)));       // exe 0x5e3280
    setTexturePageCell(a.secondTexturePage, uint32_t(int32_t(cell))); // exe 0x5e32a0
}

// exe 0x44a140: a random animation at each end; animation 4's frame 7 shows one of three parts
void figureRandom(AnimatedActor& a) {
    if (a.animEnded()) a.anim = uint8_t(rngStep() % kRandomAnims + 1);
    if (a.anim == kRandomPoseAnim && a.animKey.frame == kRandomPoseFrame) {
        const uint32_t shown = rngStep() % 3;
        for (uint32_t k = 0; k < 3; ++k) {
            if (k == shown) a.parts[kRandomParts[k]].flags &= uint8_t(~kPartHidden);
            else a.parts[kRandomParts[k]].flags |= kPartHidden;
        }
    }
    figurePose(a);
}

// exe 0x44a260: the hit box (animation 0 at each end, 1 when the player hit it)
void figureTarget(AnimatedActor& a) {
    TargetFigureFields& f = classState<TargetFigureFields>(a);
    a.prevPos = a.pos;
    if (a.animEnded()) a.anim = 0;
    if (f.hitBox.result & kHitAttackByPlayer) a.anim = 1;
    figurePose(a);
    f.hitBox.result = 0;
    f.hitBox.previousX = a.prevPos.x.whole;
    f.hitBox.previousZ = a.prevPos.z.whole;
    const int16_t pos[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
    hitListSubmit(pos, f.hitBox, hitOwner(a.slot) | kTargetShape, kTargetAttack); // exe 0x5e4120
}

// exe 0x44a330: the texture word's cell scrolled one step a tick
void figureScrolling(AnimatedActor& a) {
    ScrollingFigureFields f(a);
    a.texturePage = (a.texturePage & kTexturePageKeep) | f.scrollStep;
    const uint8_t step = uint8_t(f.scrollStep + 1);
    f.scrollStep = int8_t(step) >= kScrollSteps ? uint8_t(0) : step;
    figurePose(a);
}

// exe 0x44a390: the face's blink on its clock; pad 2's right / left cycle the animation
void figureFace(AnimatedActor& a) {
    FaceFigureFields f(a);
    ++f.faceClock;
    const uint16_t t = f.faceClock;
    if (t == kFaceHalfShut || t == kFaceHalfOpen) setTexturePageCell(a.texturePage, 1);
    if (t == kFaceShut || t == kFaceOpening) setTexturePageCell(a.texturePage, 2);
    if (f.faceClock == kFaceClosed) setTexturePageCell(a.texturePage, 3);
    if (f.faceClock == kFaceOpen) {
        setTexturePageCell(a.texturePage, 0);
        f.faceClock = 0;
    }
    const uint8_t pressed = uint8_t(pad1().pressed);
    if (pressed & kPadRight) a.anim = a.anim < kFaceLastAnim ? uint8_t(a.anim + 1) : uint8_t(0);
    else if (pressed & kPadLeft) a.anim = a.anim > 0 ? uint8_t(a.anim - 1) : kFaceLastAnim;
    figurePose(a);
}

// exe 0x449d30: the variant's model (bound under its own type), its scale and first animation, its state
void figureBind(AnimatedActor& a) {
    const uint8_t v = a.variant;
    if (v <= kLastVariant) {
        if (v == uint8_t(FigureVariant::Random) || v == uint8_t(FigureVariant::Target)) a.flags |= kActorScenery;
        a.type = kVariantModelTypes[v];
        if (!bindActorModel(a, 0)) return; // exe 0x5e0ba0 (the type stays the model's)
        a.type = kFigureType;
        switch (FigureVariant(v)) {
        case FigureVariant::Watching:
            setScale(a, 0x400);
            animSetPose(a, 0, 0);
            break;
        case FigureVariant::Looping:
            setScale(a, 0x400);
            animSetPose(a, 0xe, 0);
            setTexturePageCell(a.texturePage, 3); // exe 0x5e3280
            break;
        case FigureVariant::Random:
            a.parts[kRandomParts[0]].flags &= uint8_t(~kPartHidden);
            setScale(a, 0x800);
            a.cullWidth = 0xfff;
            a.cullHeight = 0xfff;
            a.parts[kRandomParts[1]].flags |= kPartHidden;
            a.parts[kRandomParts[2]].flags |= kPartHidden;
            animSetPose(a, 1, 0);
            break;
        case FigureVariant::Target:
            setScale(a, 0x200);
            animSetPose(a, 0, 0);
            break;
        case FigureVariant::Scrolling:
            setScale(a, 0x200);
            a.semiTransparency = 0x20; // exe 0x5e1a00(actor, 1, 0)
            animSetPose(a, 0, 0);
            break;
        case FigureVariant::Face:
            setScale(a, 0x400);
            a.anim = 0xf;
            animSetPose(a, 0xf, 0);
            break;
        }
        a.state = uint8_t(a.state + v + 1);
    }
    a.subState = kNoSubState;
    for (size_t i = 0; i < a.params.size(); ++i) a.params[i] = 0;
}

} // namespace

// exe 0x449cc0: the previous pose, the ground (all but variant 3), the state (exe table 0x88e3b0)
void st00FigureUpdate(const PlayerEnv& env, uint32_t address) {
    AnimatedActor& a = animatedActorAt(address);
    castSavePose(a);
    if (a.variant != uint8_t(FigureVariant::Target)) a.pos.y.whole = int16_t(actorGround(env, a)); // exe 0x40f190
    switch (FigureState(a.state)) {
    case FigureState::Bind: figureBind(a); break;
    case FigureState::Watching: figureWatching(a); break;
    case FigureState::Looping: figurePose(a); break; // exe 0x44a110
    case FigureState::Random: figureRandom(a); break;
    case FigureState::Target: figureTarget(a); break;
    case FigureState::Scrolling: figureScrolling(a); break;
    case FigureState::Face: figureFace(a); break;
    default: notPortedAt(kFiguresArea, "state (table 0x88e3b0)", a.state);
    }
}

} // namespace od::port
