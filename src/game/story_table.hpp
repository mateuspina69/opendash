// OpenDash's table of what advances the story, for a session: the exe has no boss marker, so the rooms of its story bosses and its
// missions are listed here stage by stage, read from their classes and room scripts (content/...). What is not here has no gate at
// its door; a scene (a script in a world) has one wherever it starts, but in the optional stages.
#pragma once

#include <cstdint>

namespace od::port {

enum class StoryPlaceKind : uint8_t {
    Free,    // free roam
    Boss,    // a story boss's room: everyone goes in together, nobody leaves during the fight
    Mission, // a clock or progress across rooms: the group stays together until it ends
};
constexpr int kAnySub = -1;
constexpr uint8_t kAnyStory = 0xff;

StoryPlaceKind storyPlaceOf(uint8_t stage, int sub, uint8_t story);
// the sub of an entry may not be known: a door inside such a stage has no gate (fromStage: where the door is)
bool storyDoorGated(uint8_t fromStage, uint8_t toStage, int toSub, uint8_t story);
// the sub-ruins and what else is off the story: no gate, a player who was not there misses it
bool storyOptionalStage(uint8_t stage);

} // namespace od::port
