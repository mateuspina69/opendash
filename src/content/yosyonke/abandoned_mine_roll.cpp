// Roll at the Abandoned Mine's entrance, class 0x4e6110 (ST0F pool 0x20 type 0x60; see abandoned_mine_roll.hpp): a guided NPC. Her
// kind 0 stands (the talk), else she walks the path [kind == 1 ? 0x8961f0 : 0x896220]; her animation, the head-look
// target (& 0x60: 0x40 the script actor of its low bits, 0x20 none, else the player) and the talk parameter, her
// message are her spawn bytes. States (the exe's table 0x896248): 0 exe 0x4e6250, 1 exe 0x4e6310 -> [kind == 0 ?
// 0x896254 : 0x89625c]: (exe 0x4e6350 wait / exe 0x4e6440 turn to the path), exe 0x4e63b0 talk, exe 0x4e6510 walk. Her
// shadow (exe 0x5f20f0) is render output and left out.
#include "content/yosyonke/abandoned_mine_roll.hpp"
#include "content/yosyonke/abandoned_mine_roll_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/cutscene_actor.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"

namespace od::port {

using namespace abandoned_mine_roll_state;

namespace {

constexpr const char* kRuinRollPort = "town story port"; // the area of its port-gap errors

constexpr uint8_t kTalkingBit = 0x01;
constexpr uint8_t kLookMask = 0x60, kLookActor = 0x40, kLookNone = 0x20, kLookActorMask = 0x1f;
enum class RuinRollState : uint8_t { Init = 0, Main = 1, Free = 2 }; // [0x896248]
enum class RuinRollSub : uint8_t { Wait = 0, Talk = 1, Walk = 2 }; // Wait: the walker turns to the path's heading
enum class TalkStep : uint8_t { Start = 0, Talking = 1, Ending = 2 };

// a point of a path (exe data): its x / z, its flag (>= 0 a point; kPathStop / kPathEnd), the yaw she turns to first
struct PathPoint : DataRecord {
    int16_t x = 0;
    int16_t flag = 0;
    int16_t z = 0;
    int16_t yaw = 0;
    static PathPoint load(const TableReader& r) {
        PathPoint rec;
        rec.x = r.s16(0);
        rec.flag = r.s16(2);
        rec.z = r.s16(4);
        rec.yaw = r.s16(6);
        return rec;
    }
};
constexpr int16_t kPathStop = -1, kPathEnd = -2;
const DataTable<RecordTable<PathPoint, 0x10, 8>> firstPath{TableId::AbandonedMineRollFirstPath};
const DataTable<RecordTable<PathPoint, 0x10, 8>> secondPath{TableId::AbandonedMineRollSecondPath};
const DataTable<CollisionBoxRecord> ruinRollCollisionBox{TableId::AbandonedMineRollRuinRollCollisionBox};
const DataTable<InteractionBoxRecord> ruinRollTalkBox{TableId::AbandonedMineRollRuinRollTalkBox};
constexpr uint8_t kTalkIdleAnim = 4, kWalkAnim = 2;

const DataTable<RecordTable<PathPoint, 0x10, 8>>& pathOf(RuinRollFields& f) { return f.kind == 1 ? firstPath : secondPath; }
void setSub(AnimatedActor& a, RuinRollSub s) {
    a.subState = uint8_t(s);
    a.restartStep();
}
// the player's talk with her starts her message (her look parameter: the event's flags)
void talkCheck(AnimatedActor& a, RuinRollFields& f) {
    if ((f.talking & kTalkingBit) || !player().talksTo(a) || eventState().active != 0) return;
    const uint8_t param = uint8_t(((f.lookParam & 0x97u) | 0x10u) >> 3);
    eventStartAtActor(a, param, 0, f.message);
    setSub(a, RuinRollSub::Talk);
}
void ruinRollSub(AnimatedActor& a, RuinRollFields& f) {
    const bool walker = f.kind != 0;
    const RuinRollSub sub = RuinRollSub(a.subState);
    if (sub == RuinRollSub::Talk) { // exe 0x4e63b0
        const uint8_t st = a.step;
        if (TalkStep(st) == TalkStep::Ending) {
            if (a.animEnded()) {
                f.talking &= uint8_t(~kTalkingBit);
                setSub(a, RuinRollSub::Wait);
            }
            return;
        }
        if (st > uint8_t(TalkStep::Ending)) return;
        if (TalkStep(st) == TalkStep::Start) {
            f.talking |= kTalkingBit;
            f.animParam = 0;
            eventState().speakerAnimation = kNoSpeakerAnimation;
            f.headPitch = 0;
            a.enterStep(uint8_t(TalkStep::Talking));
        }
        if (eventState().speakerAnimation != kNoSpeakerAnimation) {
            f.animParam = eventState().speakerAnimation;
            eventState().speakerAnimation = kNoSpeakerAnimation;
        } else if (a.animKey.flags == kKeyHold) {
            f.animParam = kTalkIdleAnim;
        }
        if (eventState().active == 0) {
            f.animParam = kTalkIdleAnim;
            a.enterStep(uint8_t(TalkStep::Ending));
        }
        return;
    }
    if (!walker) {
        if (sub != RuinRollSub::Wait) notPortedAt(kRuinRollPort, "0x4e6110 sub-state", a.subState);
        talkCheck(a, f); // exe 0x4e6350
        return;
    }
    if (sub == RuinRollSub::Wait) { // exe 0x4e6440: the turn toward the path's heading
        const uint8_t st = a.step;
        if (st == 0) {
            f.animParam = 0;
            a.velocity.z = 0;
            a.step = 1;
        }
        if (st <= 1) {
            const PathPoint e = pathOf(f)[f.pathIndex];
            if (e.flag >= 0) {
                a.subState = uint8_t(RuinRollSub::Walk);
                a.step = 0;
            } else {
                int d = turnYawTo(a.rotation.y, uint32_t(int32_t(e.yaw)), 0x80);
                if (d < 0) d = -d;
                if (d < 0x38) ++a.step;
            }
        }
        talkCheck(a, f);
        return;
    }
    if (sub != RuinRollSub::Walk) notPortedAt(kRuinRollPort, "0x4e6110 sub-state", a.subState);
    // exe 0x4e6510: the walk along the path
    const DataTable<RecordTable<PathPoint, 0x10, 8>>& path = pathOf(f);
    uint8_t idx = f.pathIndex;
    if (path[idx].flag == kPathStop) return;
    const int16_t me[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
    int dx = int16_t(me[0] - path[idx].x), dz = int16_t(me[2] - path[idx].z);
    if (dx < 0) dx = -dx;
    if (dz < 0) dz = -dz;
    if (dx < 0x20 && dz < 0x20) {
        idx = uint8_t(idx + 1);
        f.pathIndex = idx;
        const int16_t nextFlag = path[idx].flag;
        if (nextFlag < 0) {
            if (nextFlag == kPathEnd) {
                a.enterState(2);
            } else {
                a.subState = uint8_t(RuinRollSub::Wait);
                a.step = 0;
            }
        }
    } else {
        a.velocity.z = 0x260;
    }
    const PathPoint e = path[f.pathIndex];
    const int16_t t[3] = {e.x, e.flag, e.z};
    const int ang = atanAngle(float(int(t[0]) - int(me[0])), float(int(t[2]) - int(me[2]))) & kAngleMask;
    const int16_t h = int16_t(uint16_t((uint32_t(ang) << 4) - 0x7ff1u));
    turnYawTo(a.rotation.y, uint32_t(int32_t(int16_t(h >> 4))), 0x38);
    const int16_t sp = a.velocity.z;
    f.animParam = sp > 0 ? kWalkAnim : 0;
    integrateVelocity(a, 0, 0, -int(sp));
}
// a head lane relaxing toward 0 by 0x40 (two stores when it crosses 0, as the exe)
void relaxHead(int16_t& lane) {
    const int16_t v = lane;
    if (v < 0) {
        lane = int16_t(uint16_t(v + 0x40));
        if (int16_t(v + 0x40) > 0) lane = 0;
    } else {
        lane = int16_t(uint16_t(v - 0x40));
        if (int16_t(v - 0x40) < 0) lane = 0;
    }
}
// exe 0x4e6620(actor, target, look): the head turn (its yaw / pitch lanes) toward the target's 16.16 position, 0x40 a
// tick (0x80 while she talks), else relaxed; the lanes into the head part's decoded angles
void ruinRollHead(AnimatedActor& a, RuinRollFields& f, const Fixed16Vec3* target) {
    const int step = (f.talking & kTalkingBit) ? 0x80 : 0x40;
    if (!target) {
        relaxHead(f.headYaw);
        relaxHead(f.headPitch);
    } else {
        const Fixed16Vec3& t = *target;
        const uint32_t ang = uint32_t(atanAngle(float(int32_t(uint32_t(a.pos.x) - uint32_t(t.x))),
                                                float(int32_t(uint32_t(a.pos.z) - uint32_t(t.z))))) &
                             kAngleMask;
        const uint16_t hd = uint16_t(f.headYaw);
        int d = int((uint32_t(uint16_t(a.rotation.y)) + hd) & kAngleMask) - int(ang);
        if (uint32_t(d + kHalfTurn) > 0x1000u) d = int((uint32_t(-d) ^ 0xfffu) + 1u);
        const int half = step >> 1;
        if (d < -half) {
            f.headYaw = int16_t(uint16_t(hd + step));
            if (int16_t(hd + step) > 0x300) f.headYaw = 0x300;
        } else if (d > half) {
            f.headYaw = int16_t(uint16_t(hd - step));
            if (int16_t(hd - step) < -0x300) f.headYaw = -0x300;
        }
        const int16_t p = f.headPitch;
        const int e = ((int(a.pos.y.whole) - int(t.y.whole)) >> 1) + p;
        if (e < -0x20) {
            f.headPitch = int16_t(uint16_t(p + 0x40));
            if (int16_t(p + 0x40) > 0x200) f.headPitch = 0x200;
        } else if (e > 0x20) {
            f.headPitch = int16_t(uint16_t(p - 0x40));
            if (int16_t(p - 0x40) < -0x200) f.headPitch = -0x200;
        }
    }
    LaneVec4 head = decodedFrame().entries[2]; // the head part's angles
    head.x = int16_t(uint16_t(uint16_t(f.headPitch) + uint16_t(head.x)));
    head.y = int16_t(uint16_t(uint16_t(head.y) + uint16_t(f.headYaw)));
}
// exe 0x4e6110
void ruinRoll(const PlayerEnv& env, AnimatedActor& a) {
    RuinRollFields& f = classState<RuinRollFields>(a);
    a.savePreviousPose();
    a.prevAnim = a.anim;
    f.frameCopy = uint8_t(a.animKey.frame);
    switch (RuinRollState(a.state)) {
    case RuinRollState::Init: // exe 0x4e6250
        if (!bindActorModel(a, 0)) {
            freeActor(a);
            return;
        }
        a.clearClassBlock();
        a.collisionBox.pointAt(ruinRollCollisionBox.stateAddress());
        a.interactionBox.pointAt(ruinRollTalkBox.stateAddress());
        a.interaction = kInteractTalk;
        a.anim = f.animParam;
        a.prevAnim = kNoAnimShown;
        a.airborne = 0;
        a.onPlatform = 0;
        a.maxHitPoints = 1;
        a.hitPoints = 1;
        a.velocity.setXY(0, 0);
        a.velocity.setZSpare(0, 0);
        a.spin.setXY(0, 0);
        a.spin.setZSpare(0, 0);
        a.scale.z = 0x200;
        a.scale.y = 0x200;
        a.scale.x = 0x200;
        f.tracks.init(0, 0);
        f.clearTalkWord();
        a.enterState(uint8_t(RuinRollState::Main));
        break;
    case RuinRollState::Main: ruinRollSub(a, f); break;
    case RuinRollState::Free: freeActor(a); break; // exe 0x468fa0 -> 0x5e1490 (the walk's path end)
    default: notPortedAt(kRuinRollPort, "0x4e6110 state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    if (actorTestMove(env, a) != 0) actorPushOut(env, a);
    a.pos.y.whole = int16_t(actorGround(env, a));
    if (a.anim != f.animParam) a.anim = f.animParam;
    textureTracksTick(a, f.tracks);
    if (a.anim != a.prevAnim) animSet(a, a.anim, f.animKey);
    else animStep(a);
    const uint8_t look = f.lookParam & kLookMask;
    if (look == kLookActor) {
        const uint32_t t = slotIdentityWord(scriptState().actors[f.lookParam & kLookActorMask]);
        if (t) ruinRollHead(a, f, &actorLayoutAt(t).pos);
        else ruinRollHead(a, f, nullptr);
    } else if (look == kLookNone) {
        ruinRollHead(a, f, nullptr);
    } else {
        ruinRollHead(a, f, &player().pos);
    }
    animPose(a);
    actorCollisionSubmitSolid(a, SolidType::StandOn, kNoBone, 0);
    // exe 0x5f20f0: the shadow (render output)
}

} // namespace

void abandonedMineRollUpdate(const PlayerEnv& env, uint32_t actor) { ruinRoll(env, animatedActorAt(actor)); }

} // namespace od::port
