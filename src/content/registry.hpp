// The content's side of the game systems' extension points (game/content_registry): registerAll() fills the
// registry once, before the first tick -- the host calls it (app main, every test that runs game code).
#pragma once

#include "game/content_registry.hpp"

namespace od::content {

// Fills od::port::contentRegistry() and marks it ready; later calls do nothing.
void registerAll();

// The actor classes of each area (content/<area>/register.cpp)
void registerIntroClasses(od::port::ContentRegistry& r);
void registerYosyonkeClasses(od::port::ContentRegistry& r);
void registerForbiddenIslandClasses(od::port::ContentRegistry& r);
void registerMandaClasses(od::port::ContentRegistry& r);
void registerNinoClasses(od::port::ContentRegistry& r);
void registerCalbaniaClasses(od::port::ContentRegistry& r);
void registerSaulKadaClasses(od::port::ContentRegistry& r);
void registerLateGameClasses(od::port::ContentRegistry& r);
void registerSubruinsClasses(od::port::ContentRegistry& r);

} // namespace od::content
