// Saul Kada's rooms (ST26..ST2C, ST33, ST34, ST3B, ST3C, ST41, ST42, ST44, ST48) in the stage / room setup and the
// room scripts: their room helpers, the parts of their room functions beyond the template, ST48's encounter kinds,
// ST41 / ST42 / ST44's stage case heads and the door handler of their tables (game/stage_setup and game/room_scripts
// dispatch them through the content registry).
#pragma once

#include "game/content_registry.hpp"

namespace od::content {

// the island's room helpers / room-script handlers in the chains, its room functions' parts and its stage cases' heads
void registerSaulKadaRooms(od::port::ContentRegistry& r);

} // namespace od::content
