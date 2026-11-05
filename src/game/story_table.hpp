// OpenDash's table of what advances the story, for a session: the exe has no boss marker, so the rooms of its story bosses and its
// missions are listed here stage by stage, read from their classes and room scripts (content/...), with the flag that says a boss
// is beaten. What is not here is optional and has no gate.
#pragma once

#include <cstdint>

namespace od::port {

enum class StoryPlaceKind : uint8_t {
    Free,    // free roam
    Boss,    // a story boss's room, until it is beaten: everyone goes in together, nobody leaves during the fight
    Mission, // a clock or progress across rooms: the group stays together until it ends
};
constexpr int kAnySub = -1;
constexpr uint8_t kAnyStory = 0xff;

StoryPlaceKind storyPlaceOf(uint8_t stage, int sub, uint8_t story);
// the door to this room waits for everyone (fromStage: where the door is: a stage listed whole has no gate inside it)
bool storyDoorGated(uint8_t fromStage, uint8_t toStage, int toSub, uint8_t story);
// the sub-ruins: no gate, a player who was not there misses it
bool storyOptionalStage(uint8_t stage);
// this script advances the story's byte, or opens a story boss's fight: it waits for everyone. The others are optional
bool storyScriptGated(uint8_t script);
// the time trial's courses (ST59 / 5A / 5B): who goes in races alone on their own machine, the others stay free
bool storyStageRunsAlone(uint8_t stage);

} // namespace od::port
