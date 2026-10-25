// The camera scripts: the interpreter of the word lists a step sequence runs (exe 0x5118a0) on the script camera block
// (game/camera_state ScriptCamera: its point / offset / look-at point and angles, their speeds and interpolations, exe
// 0x512260 and its easing curves), the script camera taken from the follow camera when a sequence starts and given back
// when the script ends. The camera's scripted mode (game/camera) builds the view from the block each tick.
#pragma once

#include <cstddef>
#include <cstdint>


namespace od::port {

// exe 0x511390's camera part: the script camera's point and angles from the follow camera's target and pose, its
// speeds, settings and interpolations cleared, the camera script `script` (exe data; 0: none), the camera in its
// scripted mode, the shake cleared
// The easing curves of the script camera's interpolations (the routines of exe table 0x89b6b0 by the easing kind)
enum class CameraEaseCurve : uint8_t {
    Linear,  // exe 0x512360
    Cosine,  // exe 0x512460
    Out,     // exe 0x512510
    In,      // exe 0x512590
    Unknown, // a routine the port does not have (the interpolation's tick stops on it)
};
// interpolation k's curve (0: the point's, 1: the angles'), by its easing nibble
CameraEaseCurve scriptCameraEaseCurve(size_t k);

void scriptCameraStart(uint32_t script);
// exe 0x5118a0: one tick of the camera script (none while the script block pauses it): its ops up to the first wait
// that is not over, then the point / offset and the angles / look-at point moved by their speeds or interpolations
void scriptCameraTick();
// exe 0x5116a0's camera part: the camera back to the follow mode, the shake cleared
void scriptCameraEnd();

} // namespace od::port
