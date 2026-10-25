// ST16's script 0x33 (the player caught by the box carrier), one routine per exe routine. Names are behaviour based.
// Render output (the shadow list exe 0x5f20f0, the white screen exe 0x5f30e0) is left out as in the other cast ports.
//
// Script 0x33 (handler 0x528020, states [0x8a61b0] = 0x528040 / 0x52f420 / 0x5281f0; camera 0x8a6190, steps 0x8a61a0 =
// 0x528260): ST16 (the room case / content/nino/ch2_rooms at story 2, flag 0x26, no event). The first live box
// carrier (pool 0x20 type 0x49, content/nino/field_enemies) gripping the player is the script's; the camera fixed
// behind the player (0x5800 back along the angle from the point (0, -0x840, 0)), the player scripted (anim 0x81). The
// step 0x528260: the grip's animation (0x80 + the carrier's grip pose); once the carrier lets go the throw (anim 0x22,
// vy -0x200 rising 0x40 a tick along the yaw) to the next step. The wrap-up: the player back at its start x / z,
// knocked down (exe 0x572850(player, 1)), the damage of the carrier's strong attack (its stat row's), the
// block ended, flag 0x26 cleared.
#include "content/nino/st16_script33.hpp"
#include "content/nino/st16_script33_state.hpp"

#include "content/nino/field_enemies.hpp"

#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/camera_state.hpp"
#include "game/hit_points.hpp"
#include "game/player/player_damage.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

using namespace od::port::st16_script33_state;

namespace od::port {

namespace {

constexpr uint8_t kScriptCatch = 0x33;
constexpr RoutineId kGripStep = RoutineId::St16Script33GripStep; // the step's routine
constexpr uint32_t kFlagCatchScene = 0x26;


// the set-up's sub-states (+5)
enum class CatchSetUp : uint8_t { Find = 0, TakeOver = 1 };
// the grip step's phases (+6)
enum class GripPhase : uint8_t { Grip = 0, Held = 1, Thrown = 2 };

// the player's animations
constexpr uint8_t kAnimGripBase = 0x80; // + the carrier's grip pose
constexpr uint8_t kAnimCaught = 0x81, kAnimThrown = 0x22;
constexpr int16_t kThrowRise = -0x200, kThrowGravity = 0x40;
// the camera: the point it looks from (0, -0x840, 0) and its distance back, the offset from the player
constexpr int32_t kCameraFromY = int32_t(0xf7c00000u);
constexpr int kCameraBack = -0x5800;
constexpr int32_t kCameraOffsetY = int32_t(0xff800000u);

// exe 0x528260: the grip, held until the carrier lets go, then the throw until the player falls back
void gripStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    CatchScriptFields& script = scriptState().part<CatchScriptFields>();
    switch (routine.id()) {
    case kGripStep: {
        auto&& carrier = classState<BoxCarrierGrip>(*script.carrier);
        Player& p = player();
        switch (GripPhase(s.stepPhase)) {
        case GripPhase::Grip:
            playerScriptAnimId(uint8_t(carrier.gripPose + kAnimGripBase), 0, 1);
            s.stepPhase += 1u;
            [[fallthrough]];
        case GripPhase::Held:
            if (carrier.carry & kCarrierHoldsPlayer) return;
            p.velocity.y = kThrowRise;
            playerScriptAnimId(kAnimThrown, 0, 1);
            s.stepPhase += 1u;
            return;
        case GripPhase::Thrown: {
            moveAlongYaw(p.pos, p.rotation.y, 0, p.velocity.y, 0); // exe 0x5e36f0
            const int16_t v = int16_t(uint16_t(p.velocity.y) + uint32_t(kThrowGravity));
            p.velocity.y = v;
            if (v <= 0) return;
            scriptSequenceSkip(1);
            return;
        }
        default: return;
        }
    }
    default: notPortedAt(kStoryPort, "script 0x33 step", routine.pointerValue());
    }
}

// exe 0x528040: the carrier found, the camera fixed behind the player, the replay pad, the player taken over
void catchSetUp() {
    ScriptState& s = scriptState();
    CatchScriptFields& script = scriptState().part<CatchScriptFields>();
    Player& p = player();
    if (CatchSetUp(s.subState) == CatchSetUp::Find) {
        uint32_t i = 0;
        for (uint32_t mask = actorSystem().animatedLive; mask != 0; mask <<= 1, ++i) {
            AnimatedActor& actor = animatedPool()[i];
            if (int32_t(mask) < 0 && actor.type == kBoxCarrierType && classState<BoxCarrierGrip>(actor).gripPose != 0) {
                script.carrier = &actor;
                break;
            }
        }
        scriptSequenceStart(stateAddress(TableId::St16Script33CatchCamera), stateAddress(TableId::St16Script33CatchSteps));
        ScriptCamera& camera = scriptCamera();
        camera.lookAt = 1;
        camera.pointSpeed[0] = 0;
        camera.pointSpeed[1] = 0;
        camera.easing = 0;
        camera.pointSpeed[2] = 0;
        camera.angleSpeed[0] = 0;
        camera.angleSpeed[1] = 0;
        camera.angleSpeed[2] = 0;
        camera.lookAtSpeed[0] = 0;
        camera.lookAtSpeed[1] = 0;
        camera.lookAtSpeed[2] = 0;
        // the point (0, -0x840, 0) moved 0x5800 back along its angle to the player, then by the player's x / z (exe
        // 0x5e3b00's distance is compared with 0x580, both branches take the same path)
        const int angle = angleOf(0, 0, p.pos.x, p.pos.z); // exe 0x5e3bc0
        int32_t back[3];
        rotateByYaw(angle, 0, 0, kCameraBack, back); // exe 0x5e36f0
        camera.lookAtPoint[0] = int32_t(uint32_t(back[0]) + uint32_t(p.pos.x));
        camera.lookAtPoint[1] = int32_t(uint32_t(kCameraFromY) + uint32_t(back[1]));
        camera.lookAtPoint[2] = int32_t(uint32_t(back[2]) + uint32_t(p.pos.z));
        camera.setAnchor(ScriptCameraAnchor::Player);
        camera.offset[0] = 0;
        camera.offset[1] = kCameraOffsetY;
        camera.offset[2] = 0;
        script.startX = p.pos.x.whole;
        script.startZ = p.pos.z.whole;
        scriptReplayPadOn();
        s.subState = uint8_t(CatchSetUp::TakeOver);
        s.flags |= kScriptUnskippable;
    } else if (CatchSetUp(s.subState) != CatchSetUp::TakeOver) {
        return;
    }
    if (playerToScriptIfFree() == 0) {
        s.enterState(CutsceneState::Steps);
        playerScriptAnimId(kAnimCaught, 0, 1);
    }
}

// exe 0x5281f0: the player back at its start, knocked down and hurt, the block ended
void catchWrapUp() {
    CatchScriptFields& script = scriptState().part<CatchScriptFields>();
    Player& p = player();
    playerBackToPlay(); // exe 0x5723c0
    scriptReplayPadOff();
    p.pos.x.whole = script.startX;
    p.pos.z.whole = script.startZ;
    p.subState = uint8_t(PlayerSub::KnockDown);
    p.enterStep(uint8_t(KnockDownStep::Airborne));
    knockDownThrow(p, 1); // exe 0x572850(player, 1): knocked down without the throw animation
    hitPointsChange(p, -int(animatedStats(kBoxCarrierType).strongAttack)); // exe 0x5e5320
    scriptEndBlock();
    clearStoryFlag(kFlagCatchScene);
}

// script 0x33's states
void catchScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: catchSetUp(); return;
    case CutsceneState::Steps: scriptStepsStateNoFaces(&gripStep); return; // exe 0x52f420
    case CutsceneState::WrapUp: catchWrapUp(); return;
    default: notPortedAt(kStoryPort, "script 0x33 state", s.state);
    }
}

} // namespace

bool st16Script33Ported(uint8_t id) { return storyScriptHandler(id).id() == RoutineId::St16CatchScript; }

bool st16Script33Tick(const PlayerEnv&, uint8_t id) {
    if (storyScriptHandler(id).id() != RoutineId::St16CatchScript) return false;
    catchScript();
    return true;
}

} // namespace od::port
