// The room scripts of ST4B / ST4C: the handlers of the room-script tick's stage cases ST4B [0x8cd58c + sub * 4] and
// ST4C [0x8cd5a8 + sub * 4], and the cutscene script 0x59 (exe 0x533b20, ST4B sub 0). One case per exe function
// (addresses in the comments).
#include "content/late_game/st4b_st4c_rooms.hpp"
#include "content/late_game/st4b_st4c_rooms_state.hpp"

#include "engine/routine_ids.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/camera.hpp"
#include "game/display.hpp"
#include "game/event_state.hpp"
#include "game/explosions.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/room_grid.hpp"
#include "game/room_scripts.hpp"
#include "game/save_image.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/spark_emitter.hpp"
#include "game/spawn.hpp"
#include "game/stage_files.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

using namespace od::port::st4b_st4c_rooms_state;

namespace od::port {

namespace {

constexpr uint32_t kDoorFlag = 0x700;
bool doorScript() { // flag 0x700 -> script 0 (exe 0x511270(0))
    if (!storyFlag(kDoorFlag)) return false;
    scriptRun(0);
    return true;
}

// ---- script 0x59 (exe 0x533b20, states 0x8ab27c, camera 0x8aa6e0, steps 0x8aaf80, face tracks 0x8ab0e0 / 0x8ab194) -

constexpr uint8_t kStage4b = 0x4b;
constexpr uint8_t kScriptPropType = 1; // the prop the opening turns (a global static type)

Script59Fields& fields() {
    return scriptState().part<Script59Fields>();
}
// exe 0x5354e0(sub): the request to ST4B sub `sub`, position / yaw 0, fades 0xff
void subRequest(uint8_t sub) { requestTransition(0xff, kStage4b, sub, 0, 0, 0, 0, 0xff, 0xff); }
void skip() { scriptSequenceSkip(1); } // exe 0x5115c0(1)
void walk(int z) { moveAlongYaw(player().pos, player().rotation.y, 0, 0, z); } // exe 0x5e36f0
void anim(uint8_t a) { playerScriptAnimId(a, 0, 1); }                           // exe 0x5723d0(a, 0, 1)
ActorHeader& castActor(uint32_t i) { return (*scriptState().actors[i]); }
bool fadeBusy() { return fadeState().busy != 0; }
void unskippable(bool on) {
    if (on) scriptState().flags |= kScriptUnskippable;
    else scriptState().flags &= uint8_t(~kScriptUnskippable);
}
void playerXZ(uint32_t x, uint32_t z) {
    player().pos.x = int32_t(x);
    player().pos.z = int32_t(z);
}

// A point of the scene's tables: its s16 x / y / z and a fourth word (its low byte a row)
struct ScenePoint : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    int16_t x = 0; // +0x0
    int16_t y = 0; // +0x2
    int16_t z = 0; // +0x4
    int16_t extra = 0; // +0x6
    uint8_t extraByte = 0; // +0x6
    template <class R> static ScenePoint load(const R& r) {
        ScenePoint v;
        v.x = r.template read<int16_t>(0x0);
        v.y = r.template read<int16_t>(0x2);
        v.z = r.template read<int16_t>(0x4);
        v.extra = r.template read<int16_t>(0x6);
        v.extraByte = r.template read<uint8_t>(0x6);
        return v;
    }
};
constexpr uint8_t kSceneEffectType = 0x20; // the stage's effect class (content/yosyonke/st0a_fx exe 0x43c110)
constexpr uint8_t kSceneBillboard = 0, kSceneTrail = 1, kSceneBurst = 4;
// The scene effect's trail bytes (its variant 1): its row, its base / 4, along y, its shrink
struct SceneTrailParams {
    explicit SceneTrailParams(EffectActor& actor)
        : row(actor.params[0]), baseQuarter(actor.params[1]), vertical(actor.params[2]), shrink(actor.params[3]) {}
    uint8_t& row;
    uint8_t& baseQuarter;
    uint8_t& vertical;
    uint8_t& shrink;
};
EffectActor* sceneEffect(uint8_t variant) { // a scene effect (pool 0xa0, the stage's class table: exe 0x5e13a0)
    const uint32_t slot = allocActorA0(kActorStageClass);
    if (!slot) return nullptr;
    EffectActor& e = effectActorAt(slot);
    e.flags = kActorLive;
    e.type = kSceneEffectType;
    e.variant = variant;
    return &e;
}
// exe 0x535530(p): a billboard at the point, its size the point's fourth word
// the scene's point k (exe data: 8 bytes each, the trails / billboards / smoke of the collapse and the escape)
uint32_t scenePoint(uint32_t k) { return stateAddress(TableId::St4bSt4cRoomsScenePoints) + k * 8; }
void sceneBillboard(uint32_t point) {
    EffectActor* e = sceneEffect(kSceneBillboard);
    if (!e) return;
    const ScenePoint p = dataRecord<ScenePoint>(point, ScenePoint::kBytes);
    e->pos.x = p.x;
    e->pos.y = p.y;
    e->pos.z = p.z;
    e->setRecordYaw(p.extra);
}
// exe 0x535570 / 0x5355c0(p): a trail at the point (yaw 0x800 / 0x400), base 8 / 4, its row the point's byte, along
// y or not
void sceneTrail(uint32_t point, uint16_t yaw, uint8_t vertical) {
    EffectActor* e = sceneEffect(kSceneTrail);
    if (!e) return;
    const ScenePoint p = dataRecord<ScenePoint>(point, ScenePoint::kBytes);
    SceneTrailParams params(*e);
    e->pos.x = p.x;
    e->pos.y = p.y;
    e->pos.z = p.z;
    e->setRecordYaw(int16_t(yaw));
    params.baseQuarter = 8;
    params.row = p.extraByte;
    params.vertical = vertical;
    params.shrink = 0;
}
// exe 0x535610(x, y, z): a burst at the point
void sceneBurst(int16_t x, int16_t y, int16_t z) {
    EffectActor* e = sceneEffect(kSceneBurst);
    if (!e) return;
    e->pos.x = x;
    e->pos.y = y;
    e->pos.z = z;
}
// exe 0x535640(x, y, z): two hurt sparks (global static type 0xd) at the point, untilted
void debrisSparks(int16_t x, int16_t y, int16_t z) {
    for (int k = 0; k < 2; ++k) {
        const uint32_t slot = allocActor60(0); // exe 0x5e1300
        if (!slot) continue;
        StaticActor& a = staticActorAt(slot);
        HurtSparksFields& sparks = classState<HurtSparksFields>(staticActorAt(slot));
        a.flags = kActorLive;
        a.type = typeByte(GlobalStaticType::HurtSparks);
        a.variant = 0;
        a.pos.x.whole = x;
        a.pos.y.whole = y;
        a.pos.z.whole = z;
        sparks.pitch = 0;
        sparks.yaw = 0;
    }
}
// exe 0x535690(p): a smoke emitter (the stage's spark emitter, variant 0x18) at the point, yaw 0x800, drift (0, 0x40),
// lifetime -1
void sceneSmoke(uint32_t point) {
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!slot) return;
    const ScenePoint p = dataRecord<ScenePoint>(point, ScenePoint::kBytes);
    StaticActor& a = staticActorAt(slot);
    SparkEmitterParams& params = classState<SparkEmitterParams>(staticActorAt(slot));
    a.flags = kActorLive;
    a.type = kSparkEmitterStageType;
    a.variant = 0x18;
    a.pos.x.whole = p.x;
    a.pos.y.whole = p.y;
    a.pos.z.whole = p.z;
    a.rotation.y = 0x800;
    params.driftX = 0;
    params.driftZ = 0x40;
    params.lifetime = -1;
}
// exe 0x5356e0: a controller (pool 0xe0) type 0xe variant 1
void sceneController() {
    const uint32_t slot = allocActorE0(); // exe 0x5e1440
    if (!slot) return;
    ControllerActor& c = controllerActorAt(slot);
    const uint8_t loop = c.loopFlags;
    c.flags = kActorLive;
    c.type = 0xe;
    c.variant = 1;
    c.loopFlags = uint8_t(loop | kActorLateLoop);
}
// one face row: the tick it fires at (0xffff: the end), the sequence and the key
struct FaceRow : DataRecord {
    static constexpr uint32_t kBytes = 0x4;
    uint16_t tick = 0; // +0x0
    uint8_t sequence = 0; // +0x2
    uint8_t key = 0; // +0x3
    template <class R> static FaceRow load(const R& r) {
        FaceRow v;
        v.tick = r.template read<uint16_t>(0x0);
        v.sequence = r.template read<uint8_t>(0x2);
        v.key = r.template read<uint8_t>(0x3);
        return v;
    }
};
// exe 0x535700(block, t0, t1): the player's face keys by the cast timeline's tick: a row reached (its tick <= the
// timeline's) selects track 0 / 1 of the face tracks, the row index advances
void faceKeys(uint32_t table0, uint32_t table1) {
    Script59Fields& f = fields();
    ScriptState& s = scriptState();
    const FaceRow row[2] = {dataRecord<FaceRow>(table0 + uint32_t(f.faceRows[0]) * 4, FaceRow::kBytes), dataRecord<FaceRow>(table1 + uint32_t(f.faceRows[1]) * 4, FaceRow::kBytes)};
    for (uint32_t i = 0; i < 2; ++i) {
        const FaceRow& r = row[i];
        const uint16_t t = r.tick;
        if (t == 0xffff || int32_t(t) > scriptState().part<CastTimelineTick>().stepTick) continue;
        if (i) s.faceTracks.second.select(r.sequence, r.key); // exe 0x5e33f0
        else s.faceTracks.first.select(r.sequence, r.key);    // exe 0x5e33d0
        f.faceRows[i] += 1u;
    }
}
// the bank swap shared by the steps 0x5344a0 / 0x534c70 and the end: the loader idle and the draw buffer 1 -> the
// type-1 buffer flipped and walked (exe 0x5f3200); true when done
bool bufferSwap() {
    if ((fileLoads().loading & kFileLoading) || display().drawBuffer != 1) return false;
    stage().bankBuffer ^= 1u;
    stageBankWalk();
    return true;
}

void script59Step(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    Script59Fields& f = fields();
    Player& p = player();
    const uint32_t t = uint32_t(s.stepTick);
    switch (routine.id()) {
    case RoutineId::St4bScript59Step1: // the walk in (the prop turning)
        switch (s.subState) {
        case 0:
            if (f.propSpawned) f.prop->rotation.y -= 0x10u;
            if (t == 0x44) s.subState = 1;
            return;
        case 1:
            walk(-0x50);
            if (uint32_t(s.stepTick) == 0x83) {
                anim(0x82);
                s.subState = 2;
            }
            return;
        case 2:
            walk(-0x50);
            if (uint32_t(s.stepTick) == 0x8b) s.subState = 3;
            return;
        case 3:
            if (t == 0x13e) {
                skip();
                s.subState = 0;
            }
            return;
        default: return;
        }
    case RoutineId::St4bScript59Step2: // the walk in again, the poses, then sub 1
        switch (s.subState) {
        case 0:
            anim(0x81);
            p.pos.x = int32_t(0xff600000u);
            p.pos.z = 0x800000;
            p.rotation.y = 0xc00;
            f.clearFaceRows();
            s.subState = 1;
            break;
        case 1:
            walk(-0x50);
            if (uint32_t(s.stepTick) == 0x2f) {
                anim(0x82);
                s.subState = 2;
            }
            break;
        case 2:
            walk(-0x50);
            if (uint32_t(s.stepTick) == 0x37) s.subState = 3;
            break;
        case 3:
            if (t == 0x1b6) {
                anim(0x83);
                s.subState = 4;
            }
            break;
        case 4:
            if (t == 0x440) {
                anim(0x84);
                s.subState = 5;
            }
            break;
        case 5:
            if (t == 0x45e) {
                skip();
                subRequest(1);
                s.subState = 0;
            }
            break;
        default: break;
        }
        faceKeys(stateAddress(TableId::St4bSt4cRoomsStep2FaceRows), stateAddress(TableId::St4bSt4cRoomsStep2FaceRowsSecond));
        return;
    case RoutineId::St4bScript59Step3: // the cast's actors 3..5 released at 0x50, then sub 0
        switch (s.subState) {
        case 0:
            playerXZ(0xf0000000u, 0xf0000000u);
            s.subState = 1;
            return;
        case 1:
            if (t == 0x50) {
                castActor(3).stepCounter = 1;
                castActor(4).stepCounter = 1;
                castActor(5).stepCounter = 1;
                s.subState = 2;
            }
            return;
        case 2:
            if (t == 0x78) {
                skip();
                subRequest(0);
                s.subState = 0;
            }
            return;
        default: return;
        }
    case RoutineId::St4bScript59Step4:
        switch (s.subState) {
        case 0:
            playerXZ(0x730000, 0x800000);
            s.subState = 1;
            return;
        case 1:
            if (t == 0x97) {
                skip();
                subRequest(2);
                s.subState = 0;
            }
            return;
        default: return;
        }
    case RoutineId::St4bScript59Step5:
        if (s.subState == 0 && t == 0x410) {
            skip();
            subRequest(0);
            s.subState = 0;
        }
        return;
    case RoutineId::St4bScript59Step6: // the quake: the shake dies down by 0x40 every 4 ticks
        switch (s.subState) {
        case 0:
            anim(0x85);
            p.animPaused = 1;
            playerXZ(0xa70000, 0x840000);
            f.shake = 0x400;
            s.subState = 1;
            return;
        case 1: {
            if (t == 0x14) {
                p.animPaused = 0;
                s.faceTracks.first.select(0, 0);
                s.faceTracks.second.select(3, 0);
            }
            const uint32_t shake = f.shake;
            if (shake > 0x40) {
                if (uint8_t(s.stepTick) & 3) return;
                cameraShake(true, shake, 0); // exe 0x40ea80
                f.shake -= 0x40u;
                return;
            }
            cameraShake(true, 0, 0);
            s.subState = 2;
            return;
        }
        case 2:
            if (t == 0x78) {
                skip();
                s.subState = 0;
            }
            return;
        default: return;
        }
    case RoutineId::St4bScript59Step7:
        if (t == 1) cameraShake(true, 0x100, 0);
        return;
    case RoutineId::St4bScript59Step8:
        switch (s.subState) {
        case 0:
            cameraShake(true, 0x100, 0);
            s.subState = 1;
            return;
        case 1:
            if (t == 0x78) {
                cameraShake(true, 0, 0);
                skip();
                s.subState = 0;
            }
            return;
        default: return;
        }
    case RoutineId::St4bScript59Step9:
        if (s.subState == 0 && t == 0x3c) {
            skip();
            p.setDrawn(false);
            unskippable(true);
            subRequest(3);
            s.subState = 0;
        }
        return;
    case RoutineId::St4bScript59Step10:
        // the collapse: trails, billboards and smoke, then the blast, a DAT load and sub 4
        switch (s.subState) {
        case 0:
            for (uint32_t k = 13; k < 17; ++k) sceneTrail(scenePoint(k), 0x800, 0);
            for (uint32_t k = 0; k < 11; ++k) sceneBillboard(scenePoint(k));
            for (uint32_t k = 0; k < 6; ++k) sceneSmoke(scenePoint(k));
            s.subState += 1u;
            return;
        case 1:
            switch (t) {
            case 0x3c: {
                cameraShake(true, 0x800, 0);
                if (const uint32_t slot = allocActor60(0)) {
                    StaticActor& b = staticActorAt(slot);
                    BlastParams params(actorHeaderAt(slot));
                    b.flags = kActorLive;
                    b.type = typeByte(GlobalStaticType::Blast);
                    b.variant = 0x10;
                    b.pos.x.whole = 0x440;
                    b.pos.y.whole = 0xee0;
                    b.pos.z.whole = int16_t(0xf3c0);
                    params.size = 0x200;
                    params.attack = 0;
                }
                if (const uint32_t slot = allocActor60(0)) {
                    StaticActor& x = staticActorAt(slot);
                    ExplosionParams params(actorHeaderAt(slot));
                    x.flags = kActorLive;
                    x.type = typeByte(GlobalStaticType::Explosion);
                    x.variant = 0;
                    x.pos.x.whole = 0x600;
                    x.pos.y.whole = 0xee0;
                    x.pos.z.whole = int16_t(0xf1c0);
                    params.spread = 0x100;
                    params.flashes = 0x3c;
                }
                sceneSmoke(scenePoint(11));
                sceneBillboard(scenePoint(11));
                sceneBillboard(scenePoint(12));
                return;
            }
            case 0x5a:
                cameraShake(true, 0, 0);
                sceneTrail(scenePoint(17), 0x800, 0);
                return;
            case 0x64: sceneTrail(scenePoint(18), 0x800, 0); return;
            case 0x78: sceneTrail(scenePoint(19), 0x800, 0); return;
            case 0x96:
                scriptActorSpawnRecord(stateAddress(TableId::St4bSt4cRoomsCollapseRecord));
                s.subState += 1u;
                return;
            default: return;
            }
        case 2:
            if (t == 0xf0) {
                scriptFade(0x11, 0);
                voiceClipFadeOut(8);
                s.subState += 1u;
            }
            return;
        case 3:
            if (voiceClipIdle() && !fadeBusy()) {
                actorsSweep(0xffe, 0x80);
                datLoadRequest(0x12d); // exe 0x40efe0
                s.subState += 1u;
            }
            return;
        case 4:
            if (bufferSwap()) {
                voiceClipPlay(2);
                s.subState += 1u;
            }
            return;
        case 5:
            if (voiceClipPlaying()) {
                subRequest(4);
                skip();
                s.subState = 0;
            }
            return;
        default: return;
        }
    case RoutineId::St4bScript59Step11:
        switch (s.subState) {
        case 0:
            scriptFade(1, 0);
            s.subState = 1;
            return;
        case 1:
            if (!fadeBusy()) {
                s.subState = 2;
                unskippable(false);
            }
            return;
        case 2:
            if (t == 0x37a) {
                scriptFade(0x14, 0);
                s.subState = 3;
                unskippable(true);
            }
            return;
        case 3:
            if (!fadeBusy()) {
                skip();
                subRequest(2);
                s.subState = 0;
            }
            return;
        default: return;
        }
    case RoutineId::St4bScript59Step12: // the cast's actor 2 counted down at five ticks
        if (t == 0x44 || t == 0x4c || t == 0x54 || t == 0x5c || t == 0x64) castActor(2).params[3] -= 1u;
        switch (s.subState) {
        case 0:
            anim(0x81);
            p.setDrawn(true);
            p.animPaused = 1;
            playerXZ(0, 0);
            scriptFade(4, 0);
            s.subState = 1;
            return;
        case 1:
            if (!fadeBusy()) {
                s.subState = 2;
                unskippable(false);
            }
            return;
        case 2:
            if (t == 0xea) {
                skip();
                s.subState = 0;
            }
            return;
        default: return;
        }
    case RoutineId::St4bScript59Step13:
        switch (s.subState) {
        case 0:
            p.animPaused = 0;
            f.clearFaceRows();
            s.subState = 1;
            break;
        case 1:
            if (t == 0x5a) {
                anim(0x82);
                s.subState = 2;
            }
            break;
        case 2:
            if (t == 0x145) {
                skip();
                s.subState = 0;
            }
            break;
        default: break;
        }
        faceKeys(stateAddress(TableId::St4bSt4cRoomsStep13FaceRows), stateAddress(TableId::St4bSt4cRoomsStep13FaceRowsSecond));
        return;
    case RoutineId::St4bScript59Step14:
        switch (s.subState) {
        case 0:
            f.clearFaceRows();
            s.subState = 1;
            break;
        case 1:
            if (t == 0x14) {
                anim(0x83);
                s.subState = 2;
            }
            break;
        case 2:
            if (t == 0x46) {
                anim(0x84);
                playerXZ(0xff920000u, 0x980000);
                s.subState = 3;
            }
            break;
        case 3:
            walk(-0x50);
            if (uint32_t(s.stepTick) == 0x6e) s.subState = 4;
            break;
        case 4:
            if (t == 0x314) {
                anim(0x85);
                s.subState = 5;
            }
            break;
        case 5:
            if (t == 0x378) {
                skip();
                s.subState = 0;
            }
            break;
        default: break;
        }
        faceKeys(stateAddress(TableId::St4bSt4cRoomsStep14FaceRows), stateAddress(TableId::St4bSt4cRoomsStep14FaceRowsSecond));
        return;
    case RoutineId::St4bScript59Step15:
        if (s.subState == 0 && t == 0x118) {
            skip();
            s.subState = 0;
        }
        return;
    case RoutineId::St4bScript59Step16:
        switch (s.subState) {
        case 0:
            f.clearFaceRows();
            s.subState = 1;
            break;
        case 1:
            switch (t) {
            case 0xa: anim(0x86); break;
            case 0x1c5: cameraShake(true, 0x100, 0); break;
            case 0x201:
                cameraShake(true, 0, 0);
                soundPlayFlatForced(0x51); // exe 0x5d5260
                break;
            case 0x21f: anim(0x87); break;
            // past `cmp eax, 0x21f; jg 0x534b2b` the register still holds the tick: sub 0x2fc / sub 0x58 are the
            // cases 0x2fc / 0x354 (not 0x21f + them -- the scene ran 18 s long and the next shot came late)
            case 0x2fc: anim(0x88); break;
            case 0x354:
                skip();
                s.subState = 0;
                break;
            default: break;
            }
            break;
        default: break;
        }
        faceKeys(stateAddress(TableId::St4bSt4cRoomsStep16FaceRows), stateAddress(TableId::St4bSt4cRoomsStep16FaceRowsSecond));
        return;
    case RoutineId::St4bScript59Step17:
        if (s.subState == 0 && t == 0x1fe) {
            s.faceTracks.first.select(0xa, 0);
            s.faceTracks.second.select(7, 0);
            skip();
            s.subState = 0;
        }
        return;
    case RoutineId::St4bScript59Step18:
        if (s.subState == 0 && t == 0x327) {
            skip();
            s.subState = 0;
        }
        return;
    case RoutineId::St4bScript59Step19:
        switch (s.subState) {
        case 0:
            playerXZ(0xffb00000u, 0xffa00000u);
            s.subState = 1;
            return;
        case 1:
            if (t == 5) {
                anim(0x89);
                s.subState = 2;
            }
            return;
        case 2:
            if (t == 0x1fb) {
                skip();
                s.subState = 0;
            }
            return;
        default: return;
        }
    case RoutineId::St4bScript59Step20: // the face keys, then the DAT load and sub 4 (the end without its own load)
        switch (s.subState) {
        case 0:
            f.clearFaceRows();
            s.subState = 1;
            return;
        case 1:
            faceKeys(stateAddress(TableId::St4bSt4cRoomsStep20FaceRows), stateAddress(TableId::St4bSt4cRoomsStep20FaceRowsSecond));
            if (uint32_t(s.stepTick) == 0x40b) {
                scriptFade(0x11, 0);
                voiceClipFadeOut(8);
                s.subState = 2;
                unskippable(true);
            }
            return;
        case 2:
            if (voiceClipIdle() && !fadeBusy()) {
                actorsSweep(0xffe, 0x80);
                datLoadRequest(0x12e);
                s.subState = 3;
            }
            return;
        case 3:
            if (bufferSwap()) {
                voiceClipPlay(6);
                s.subState = 4;
            }
            return;
        case 4:
            if (voiceClipPlaying()) {
                anim(0);
                p.setDrawn(false);
                f.loaded = 1;
                skip();
                subRequest(4);
                s.subState = 0;
            }
            return;
        default: return;
        }
    case RoutineId::St4bScript59Step21:
        switch (s.subState) {
        case 0:
            scriptFade(1, 0);
            s.subState = 1;
            return;
        case 1:
            if (!fadeBusy()) {
                s.subState = 2;
                unskippable(false);
            }
            return;
        case 2:
            if (t == 0x559) {
                skip();
                subRequest(5);
                s.subState = 0;
            }
            return;
        default: return;
        }
    case RoutineId::St4bScript59Step22: // the walk to the boss
        switch (s.subState) {
        case 0:
            s.faceTracks.first.select(0xd, 0);
            s.faceTracks.second.select(8, 0);
            anim(0x8a);
            p.pos.x = 0;
            p.setDrawn(true);
            p.pos.z = 0xde00000;
            p.rotation.y = 0;
            s.subState = 1;
            return;
        case 1:
            walk(-0x152);
            if (uint8_t(p.body.key.frame) == 3) s.subState = 2;
            return;
        case 2:
            walk(-0x7c);
            if (uint8_t(p.body.key.frame) == 9) s.subState = 3;
            return;
        case 3:
            if (t == 0x3c) {
                skip();
                s.subState = 0;
            }
            return;
        default: return;
        }
    case RoutineId::St4bScript59Step23: // the stage's debris by tick (the point (0x68, -0x6c8, 0x935))
        switch (t) {
        case 0xdc:
            cameraShake(true, 0xf00, 0);
            sceneController();
            return;
        case 0xdd:
            sceneBurst(0x68, -0x6c8, 0x935);
            debrisSparks(0x68, -0x6c8, 0x935);
            return;
        case 0xe1: case 0xe6: case 0x107: case 0x109: case 0x11f: debrisSparks(0x68, -0x6c8, 0x935); return;
        case 0x104: cameraShake(true, 0, 0); return;
        default: return;
        }
    case RoutineId::St4bScript59Step24:
        switch (t) {
        case 0xa: case 0x16: case 0x1d: case 0x2b: case 0x32: case 0x3a: case 0x3e: case 0x45: case 0x4e: case 0x5d:
        case 0x66: case 0x73: case 0x7b: case 0x88: case 0x8e:
            debrisSparks(0x100, -0x8c0, 0);
            return;
        default: return;
        }
    case RoutineId::St4bScript59Step25:
        switch (t) {
        case 1: { // the cast's actor 2 (an effect) placed
            EffectActor& actor2 = asEffect(*scriptState().actors[2]);
            actor2.pos.x = 0x100;
            actor2.pos.y = int16_t(0xf750);
            actor2.pos.z = 0;
            return;
        }
        case 0xa: case 0x16: case 0x1d: case 0x2e: case 0x34: case 0x3c: case 0x48: case 0x4f: case 0x5c: case 0x64:
        case 0x6c:
            debrisSparks(0x100, -0x8c0, 0);
            return;
        default: return;
        }
    case RoutineId::St4bScript59Step26: // the sparks, the cast's actor 3 fading in, the fade, actor 1 placed
        switch (t) {
        case 0xa: case 0xf: case 0x3c: case 0x40: case 0x5a: case 0x62: case 0x8c: case 0x91: case 0xc8: case 0xe6:
        case 0x118: case 0x122: case 0x154:
            debrisSparks(0x100, -0x9c0, -0x100);
            break;
        default: break;
        }
        if (s.stepTick > 0x14a) {
            ActorHeader& actor3 = castActor(3);
            actor3.params[0] += 2u;
            actor3.params[1] += 2u;
        }
        switch (s.subState) {
        case 0:
            if (uint32_t(s.stepTick) == 0x168) {
                scriptFade(0x1a, 0);
                s.subState = 1;
                unskippable(true);
            }
            return;
        case 1:
            if (!fadeBusy()) s.subState = 2;
            return;
        case 2:
            if (uint32_t(s.stepTick) == 0x186) {
                Actor& actor1 = actorLayoutOf(*scriptState().actors[1]);
                actor1.pos.x.whole = int16_t(0xdef8);
                actor1.pos.y.whole = int16_t(0xf600);
                actor1.pos.z.whole = int16_t(0xed00);
                skip();
                s.subState = 0;
            }
            return;
        default: return;
        }
    case RoutineId::St4bScript59Step27:
        switch (s.subState) {
        case 0:
            scriptFade(0, 0);
            cameraShake(true, 0x400, 0);
            s.subState = 1;
            unskippable(false);
            return;
        case 1:
            if (t == 0x1e) {
                cameraShake(true, 0, 0);
            } else if (t == 0x3c) {
                actorsSweep(0xe0, 0x80);
                skip();
                s.subState = 0;
            }
            return;
        default: return;
        }
    case RoutineId::St4bScript59Step28:
        if (s.subState != 0) return;
        for (uint32_t k = 20; k < 28; ++k) sceneTrail(scenePoint(k), 0x400, 1); // exe 0x5355c0
        s.subState += 1u;
        soundFadeOut(0xf, 0x222, 0);
        return;
    default: notPortedAt(kStoryPort, "script 0x59 step", routine.pointerValue());
    }
}

// The opening's prop: its first parameter byte
struct ScriptPropParams {
    explicit ScriptPropParams(const ActorHeader& actor) : ScriptPropParams(const_cast<ActorHeader&>(actor)) {}
    explicit ScriptPropParams(ActorHeader& actor) : kind(actor.params[0]) {}
    uint8_t& kind;
};
// script 0x59 (exe 0x533b20; ST4B sub 0, the first visit): the cast timeline's tick copied, then the state [0x8ab27c]
void script59() {
    ScriptState& s = scriptState();
    Script59Fields& f = fields();
    Player& p = player();
    scriptState().part<CastTimelineTick>().stepTick = s.stepTick;
    switch (CutsceneState(s.state)) {
    case CutsceneState::SetUp: // exe 0x533b40
        switch (s.subState) {
        case 0:
            scriptReplayPadOn(); // exe 0x5117f0
            clearStoryFlag(0x3d3);
            s.subState = 1;
            unskippable(true);
            [[fallthrough]];
        case 1: {
            if (playerToScriptIfFree() != 0) return; // exe 0x572300
            p.clearDirectionOffsetWords();
            p.idleFaces[0] = 0;
            p.idleFaces[1] = 0;
            p.pos.x = int32_t(0xfe200000u);
            p.pos.y = 0;
            p.pos.z = int32_t(0xfbe00000u);
            p.rotation.y = 0x800;
            s.faceTracks.init(stateAddress(TableId::St4bSt4cRoomsScript59Eyes), stateAddress(TableId::St4bSt4cRoomsScript59Mouth)); // exe 0x5e33a0
            s.faceTracks.first.select(0, 0);
            s.faceTracks.second.select(0, 0);
            actorsSweep(0xffe, 0x80); // exe 0x5e05d0
            segmentSetDoorsHidden(GridCell(gridCellAtIndex(0x3f, 0x3e)).head, true); // exe 0x600f50
            if (const uint32_t slot = allocActor60(0)) {                               // exe 0x5e1300
                StaticActor& prop = staticActorAt(slot);
                const uint8_t loop = prop.loopFlags;
                prop.flags = kActorLiveDrawn;
                prop.type = kScriptPropType;
                prop.variant = 2;
                prop.loopFlags = uint8_t(loop | kActorLateLoop);
                prop.pos.x.whole = int16_t(0xfdb0);
                prop.pos.y.whole = 0;
                prop.pos.z.whole = int16_t(0xfc80);
                prop.rotation.y = 0x800;
                ScriptPropParams(actorHeaderAt(slot)).kind = 1;
                prop.subState = 0;
                f.prop = &prop;
                f.propSpawned = 1;
            } else {
                f.propSpawned = 0;
            }
            f.loaded = 0;
            f.shake = 0;
            scriptSequenceStart(stateAddress(TableId::St4bSt4cRoomsScript59Camera), stateAddress(TableId::St4bSt4cRoomsScript59Steps)); // exe 0x511390
            scriptSequenceTick();                    // exe 0x5114d0
            s.subState = 2;
            return;
        }
        case 2:
            if (!(fileLoads().loading & kFileLoading)) s.subState = 3;
            return;
        case 3:
            if (!(fileLoads().loading & kStreamBusy)) {
                voiceClipPlay(1); // exe 0x601050
                s.subState = 4;
            }
            return;
        case 4:
            if (!voiceClipPlaying()) return;
            unskippable(false);
            anim(0x81);
            scriptActorSpawnRecord(stateAddress(TableId::St4bSt4cRoomsScript59Record));
            s.state = uint8_t(CutsceneState::Steps);
            s.subState = 0;
            return;
        default: return;
        }
    case CutsceneState::Steps: { // exe 0x533d20: the step; the list's end or the skip -> the wrap-up
        const ScriptStep current = scriptStepAt(s.steps.value());
        if (current.id == kScriptStepEnd) {
            s.enterState(CutsceneState::WrapUp);
        } else {
            script59Step(stateRoutine(current.routine));
            if (scriptSkipRequested()) s.enterState(CutsceneState::WrapUp); // exe 0x511790
        }
        scriptSequenceTick();
        faceTracksTick(s.faceTracks); // exe 0x5e3410
        return;
    }
    case CutsceneState::WrapUp: // exe 0x533d60
        switch (s.subState) {
        case 0:
            if (display().drawBuffer == 0) return;
            scriptFade(0x22, 0);
            s.subState = 1;
            return;
        case 1:
            if (voiceClipPlaying()) voiceClipFadeOut(8); // exe 0x601220
            actorsSweep(0xffe, 0x80);
            soundFadeOut(0xf, 0xfff, 0);
            s.subState = 2;
            return;
        case 2:
            if (!voiceClipIdle() || fadeBusy() || !soundDone(0xf)) return; // exe 0x5d5900
            s.subState = f.loaded != 0 ? 5 : 3;
            return;
        case 3:
            actorsSweep(0xffe, 0x80);
            datLoadRequest(0x12e);
            s.subState = 4;
            return;
        case 4:
            if (bufferSwap()) s.subState = 5;
            return;
        case 5:
            if (gameState().room != 5) subRequest(5);
            s.subState = 6;
            return;
        case 6:
            scriptFade(1, 0);
            switchGridCellVariant(0x3f, 0x47, 1); // exe 0x58d5f0
            switchGridCellVariant(0x3f, 0x48, 1);
            switchGridCellVariant(0x40, 0x47, 1);
            switchGridCellVariant(0x40, 0x48, 1);
            spawnRecords(stateAddress(TableId::St4bSt4cRoomsScript59Spawns), 2);
            soundPlayFlatForced(0x4c);
            playerBackToPlay();   // exe 0x5723c0
            scriptReplayPadOff(); // exe 0x511890
            p.airborne = 0;
            p.pos.x = 0;
            p.setDrawn(true);
            p.pos.y = int32_t(0xfd300000u);
            p.pos.z = 0xa000000;
            p.rotation.y = 0;
            p.animPaused = 0;
            scriptEndBlock(); // exe 0x5116a0
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x59 state", s.state);
    }
}

} // namespace

bool st4bScript59Ported(RoutineId handler) { return handler == RoutineId::St4bScript59; }
bool st4bScript59Tick(const PlayerEnv&, RoutineId handler) {
    if (!st4bScript59Ported(handler)) return false;
    script59();
    return true;
}

bool st4bSt4cRoomHandler(RoutineId fn, bool run) {
    RoomScriptState& room = roomScriptState();
    switch (fn) {
    // ---- ST4B (case 0x58b2e0, table 0x8cd58c) -------------------------------------------------------------------
    case RoutineId::St4BSub0RoomHandler: // sub 0: script 0x59, once
        if (!run || room.handlerStep != 0) return true;
        scriptRun(0x59);
        room.handlerStep += 1u;
        return true;
    case RoutineId::St4BSub5RoomHandler:
        // sub 5: flag 0x680 -> script 0x5a (step 1); then flag 0x681 -> script 0x5b (step 2)
        if (!run) return true;
        switch (room.handlerStep) {
        case 0:
            if (storyFlag(0x680)) {
                scriptRun(0x5a);
                room.handlerStep += 1u;
            }
            return true;
        case 1:
            if (storyFlag(0x681)) {
                scriptRun(0x5b);
                room.handlerStep += 1u;
            }
            return true;
        default: return true;
        }
    // ---- ST4C (case 0x58b380, table 0x8cd5a8) -------------------------------------------------------------------
    case RoutineId::St4CSub3RoomHandler:
        // sub 3: records 0x8cd5c0 x 5, flag 0x711 until flag 0x177; the door: north of z -0x400 the
        // game-clear image rebuilt with the position (0xf0, 0, -0x35a) yaw 0 (flag 0x700 kept out of it), then script 0
        if (!run) return true;
        switch (room.handlerStep) {
        case 0:
            spawnRecords(stateAddress(TableId::St4bSt4cRoomsSt4CSub3Spawns), 5);
            if (!storyFlag(0x177)) setStoryFlag(0x711);
            room.handlerStep += 1u;
            [[fallthrough]];
        case 1:
            if (storyFlag(0x177)) {
                clearStoryFlag(0x711);
                room.handlerStep += 1u;
            }
            break;
        default: break;
        }
        if (!storyFlag(kDoorFlag)) return true;
        if (player().pos.z.whole < -0x400) {
            clearStoryFlag(kDoorFlag);
            saveSerialise(kClearDataImage); // exe 0x542ac0
            SaveImage& image = saveImageAt(kClearDataImage);
            image.playerPos.x = 0xf00000;
            image.playerPos.y = 0;
            image.playerPos.z = int32_t(0xfca60000u);
            image.playerYaw = 0;
            saveChecksumWrite(kClearDataImage); // exe 0x542f40
            setStoryFlag(kDoorFlag);
        }
        scriptRun(0);
        return true;
    case RoutineId::St4CSub4RoomHandler:
        // sub 4: flags 0x680 / 0x681 cleared; flag 0x680 (no event) -> script 0x6b; then flag 0x681 ->
        // script 0x6c; otherwise the door
        if (!run) return true;
        switch (room.handlerStep) {
        case 0:
            clearStoryFlag(0x680);
            clearStoryFlag(0x681);
            room.handlerStep += 1u;
            [[fallthrough]];
        case 1:
            if (storyFlag(0x680) && eventState().active == 0) {
                scriptRun(0x6b);
                room.handlerStep += 1u;
                return true;
            }
            break;
        case 2:
            if (storyFlag(0x681)) {
                scriptRun(0x6c);
                room.handlerStep += 1u;
                return true;
            }
            break;
        default: break;
        }
        doorScript();
        return true;
    default: return false;
    }
}

} // namespace od::port

// script 0x59 kHandlersTranscribed: 0x533b20
