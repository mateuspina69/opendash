// The running stage's state in the exe's layout: the stage's globals, the transition request, the event, the room
// scripts, the script block's native part and the minimap (game/stage_state, transition_state, event_state,
// room_scripts, script_state, minimap).
#include "game/event_state.hpp"
#include "game/minimap.hpp"
#include "game/room_scripts.hpp"
#include "game/room_triggers.hpp"
#include "game/script_state.hpp"
#include "game/stage_state.hpp"
#include "game/transition_state.hpp"
#include "game/room_uv_animation.hpp"
#include "translator/layout.hpp"

namespace od::test {

using namespace od::port;

void stageStateLayout(LayoutVisitor& v) {
    StageState& s = stage();
    v.field(0x6d000c, s.bankBuffer);
    v.field(0x6d000d, s.spareBuffer);
    v.field(0x6d000e, s.depthOffset);
    v.field(0x6d0010, s.depthRange);
    v.field(0x6d0012, s.effectsFar);
    v.field(0x6d0074, s.modelBank);
    v.field(0x6d009a, s.gridByte);
    v.field(0x6d009c, s.lodDistances);
    v.field(0x6d00a8, s.depthCue);
    v.field(0x6d00ac, s.clutAnimations);
    v.field(0x6d00b0, s.textureAnimations);
    v.field(0x6d00b4, s.textureAnimationCount);
    // the room's texture animation list at its address (its entries up to the count)
    for (uint32_t k = 0; s.textureAnimations != 0 && k < s.textureAnimationCount; ++k) {
        RoomUvAnimation& e = roomUvAnimations().entries[k];
        const uint32_t a = s.textureAnimations + k * kRoomUvAnimationSize;
        v.field(a + 0x0, e.flags);
        v.field(a + 0x1, e.frameCount);
        v.field(a + 0x2, e.frame);
        v.field(a + 0x3, e.ticksLeft);
        v.field(a + 0x4, e.faceTexels);
        v.field(a + 0x8, e.faceShades);
        v.field(a + 0xc, e.steps);
        v.field(a + 0x10, e.frames);
    }
    v.field(0x6d00b5, s.coordinateShift);
    v.field(0x6d00b6, s.drawWord);
    v.field(0x6d00b8, s.warpOffsetX);
    v.field(0x6d00ba, s.warpOffsetZ);
    v.field(0x6d00bc, s.drawByteA);
    v.field(0x6d00bd, s.drawByteB);
    v.field(0xa72244, s.townRoutes);
    v.field(0xa7180c, s.exitTable);
    v.field(0xa5f268, s.transitionHandlers);
    v.field(0xa72584, s.roomCallback);
    v.field(0xa6bb80, s.script8Callback);
    v.field(0xa72580, s.script30Callback);
    v.field(0xa72230, s.secondRoomCallback);
    v.field(0xa6c420, s.groundKinds);
    v.field(0xa6beb4, s.vertexColourHook);

    TransitionRequest& r = transitionRequest();
    v.field(0xa6d2e0, r.state);
    v.field(0xa6d2e1, r.keepPosition);
    v.field(0xa6d2e2, r.exitId);
    v.field(0xa6d2e3, r.exitByte1);
    v.field(0xa6d2e4, r.destStage);
    v.field(0xa6d2e5, r.destRoom);
    v.field(0xa6d2e6, r.kind);
    v.field(0xa6d2e7, r.exitByte2);
    v.field(0xa6d2e8, r.trigger);
    v.field(0xa6d2f0, r.destination);
    v.field(0xa6d2f8, r.fadeIn);
    v.field(0xa6d2f9, r.fadeOut);
    v.field(0xa6d2fa, r.spare);

    v.field(0xa5a040, eventStartPoint().point); // the speaker point the start routines pass (exe 0x543b40)
    EventState& e = eventState();
    v.field(0xa5f280, e.active);
    v.field(0xa5f281, e.flags);
    v.field(0xa5f282, e.mode);
    v.field(0xa5f283, e.state);
    v.field(0xa5f284, e.window);
    v.field(0xa5f286, e.message);
    v.field(0xa5f288, e.countdown);
    v.field(0xa5f28a, e.newLine);
    v.field(0xa5f28b, e.speakerAnimation);
    v.field(0xa5f28c, e.speakerPosition);
    v.field(0xa5f290, e.turnedRotation);
    v.field(0xa5f294, e.restoreYaw);
    v.field(0xa5f296, e.faceYaw);

    RoomScriptState& rs = roomScriptState();
    v.field(0xa714e0, rs.handlerStep);
    v.field(0xa714e1, rs.handlerByte1);
    v.field(0xa714e2, rs.handlerByte2);
    v.field(0xa714e3, rs.handlerByte3);
    v.field(0xa714e4, rs.actors);
    v.field(0xa71564, rs.actorMask);

    ScriptState& sc = scriptState();
    v.field(0xa725a0, sc.flags);
    v.field(0xa725a1, sc.id);
    v.field(0xa725a2, sc.stepId);
    v.field(0xa725a3, sc.stepSub);
    v.field(0xa725a4, sc.state);
    v.field(0xa725a5, sc.subState);
    v.field(0xa725a6, sc.stepPhase);
    v.field(0xa725a7, sc.stepSubPhase);
    v.field(0xa725a8, sc.savedInputMode);
    v.field(0xa725a9, sc.skipRequested);
    v.field(0xa725aa, sc.inputSpare);
    v.field(0xa725c0, sc.totalTicks);
    v.field(0xa725c4, sc.idTicks);
    v.field(0xa725c8, sc.stepTick);
    for (uint32_t i = 0; i < 32; ++i) v.field(0xa725cc + i * 4, sc.actors[i]);
    v.field(0xa7264c, sc.steps);
    v.field(0xa72650, sc.faceTracks);

    TriggerRecord& t = trigger();
    v.field(0xa7b260, t.type);
    v.field(0xa7b262, t.flags);
    v.field(0xa7b264, t.centre);

    MinimapState& m = minimap();
    v.field(0xa6d320, m.flags);
    v.field(0xa6d321, m.layer);
    v.field(0xa6d322, m.area);
    v.field(0xa6d323, m.spare03);
    v.field(0xa6d324, m.ready);
    v.field(0xa6d325, m.spare05);
    v.field(0xa6d327, m.phase);
    v.field(0xa6d328, m.slideX);
    v.field(0xa6d32a, m.visitedBase);
    v.field(0xa6d32c, m.entryX);
    v.field(0xa6d32e, m.entryZ);
    v.field(0xa6d330, m.entryYaw);
    v.field(0xa6d332, m.entryYawSpare);
    v.field(0xa6d334, m.spare14);
    v.field(0xa6d338, m.areaExtra);
    v.field(0xa6d339, m.spare19);
}

} // namespace od::test
