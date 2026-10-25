// The new-game opening (ST39, script 5): the narration over the sea, the talk with the Flutter's crew and the choice of
// the explanation, then the walk off to the Flutter (ST1E). The script (content/intro/opening_script.cpp) keys its
// cutscene actors (content/intro/opening_cast.cpp) on the step timeline; the room case of ST39 starts it.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

struct PlayerEnv;

// The bytes the script writes into the actors that play the animation it asks for (type 0x47, exe 0x4cd370)
struct ScriptAnimatedFields {
    explicit ScriptAnimatedFields(const AnimatedActor& actor) : ScriptAnimatedFields(const_cast<AnimatedActor&>(actor)) {}
    explicit ScriptAnimatedFields(AnimatedActor& actor) : anim(actor.params[0]), key(actor.params[1]) {}
    uint8_t& anim; // the animation to play
    FieldAlias<int8_t, uint8_t> key; // the key it starts at
};

// the explanation the player chose in the opening's choice: flag kOpeningChoiceFlags + choice (set by the choice's
// messages, cleared once it was acted out; the last choice ends the talk)
constexpr uint32_t kOpeningChoiceFlags = 0x680;
constexpr uint8_t kOpeningChoices = 4;
constexpr uint8_t kOpeningLastChoice = kOpeningChoices - 1;

// script 5 (exe handler 0x5151c0): whether `handler` is it / its tick
bool openingScriptPorted(RoutineId handler);
bool openingScriptTick(const PlayerEnv& env, RoutineId handler);
// ST39's room case (exe 0x587ba0): the opening (story 0, room 0: script 5) / story 0x11 room 1: script 0x68
bool openingRoomCase();
bool openingRoomCaseKnown(uint8_t stage); // ST39

// The opening's cutscene actors (ST39 class table)
void openingFlutterUpdate(const PlayerEnv& env, uint32_t actor);   // exe 0x49dab0 (type 0x30): the Flutter
void scriptAnimatedUpdate(const PlayerEnv& env, uint32_t actor);   // exe 0x4cd370 (type 0x47): the script's animation
void openingCrewUpdate(const PlayerEnv& env, uint32_t actor);      // exe 0x4e9ae0 (type 0x63, variants 0x896480)
void openingTalkerUpdate(const PlayerEnv& env, uint32_t actor);    // exe 0x4af7e0 (type 0x39, variants 0x894170)

} // namespace od::port
