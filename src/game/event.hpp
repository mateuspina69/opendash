// The event runner of dash2.exe: the event request (game/event_state) a talk, a door, a chest or a script starts, and
// its modes run every tick before the player (exe 0x543680): the talk event (the talk camera, the speaker turned
// toward the player, the message window), the player's event and item-get poses, the chests' stand.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include "game/actor_state.hpp"
#include "game/event_state.hpp"
#include "game/transition_state.hpp"

namespace od::port {

// exe 0x543680: runs the active event request's mode (talk, poses, item get, stand); another mode throws notPorted
void eventTick();
// exe 0x543bb0 (only in the field / pause modes): the event request (pos = the speaker's 16.16 position or null, rot =
// the rotation of the actor to turn or null, param = mode << 16 | flags, window, message id); the player's input off
void eventStart(const Fixed16Vec3* pos, ShortVec3* rot, uint32_t param, uint16_t window, uint16_t message);
// exe 0x543b40: the same with the address of an s16 position in the loaded data (or 0), copied as 16.16 into
// EventStartPoint::point
void eventStartAt(uint32_t pos16, ShortVec3* rot, uint32_t param, uint16_t window, uint16_t message);
// ... at a room place's point (the trigger the scan found)
void eventStartAtPoint(const RoomPlace& place, ShortVec3* rot, uint32_t param, uint16_t window, uint16_t message);
// exe 0x543af0(actor, param, window, message): the event at an actor (its position, the rotation it turns)
void eventStartAtActor(const Actor& actor, uint32_t param, uint16_t window, uint16_t message);
// exe 0x543aa0: the running event ended at once (the camera back to the saved follow pose without a blend, the turned
// actor's yaw restored, the player's invulnerability / input mode by the event's flags)
void eventAbort();

// A session's world: the events run on the machine of the player they are for, who has the windows and the camera; here the
// event only holds the room's classes and turns its speaker
struct OwnerEvent {
    size_t player = 0;
    bool hasPos = false;
    int32_t x = 0, y = 0, z = 0;
    uint32_t param = 0;
    uint16_t window = 0, message = 0;
    friend bool operator==(const OwnerEvent&, const OwnerEvent&) = default;
};
std::vector<uint8_t> encode(const OwnerEvent& e);
std::optional<OwnerEvent> decodeOwnerEvent(std::span<const uint8_t> bytes);
void setEventOwnerHook(std::function<void(const OwnerEvent&)> hook);
// the player whose Use asked for the next event
void setEventOwner(size_t player);
// the owner's event is over: its speaker turns back, then it ends
void eventOwnerEnded();
// the player is out of the world: their event ends with them
void eventOwnerLeft(size_t player);

} // namespace od::port
