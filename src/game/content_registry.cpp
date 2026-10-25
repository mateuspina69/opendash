// The content registry: the one table the content areas fill at start-up (see content_registry.hpp).
#include "game/content_registry.hpp"

#include <stdexcept>
#include <string>

namespace od::port {

void ContentRegistry::addClass(RoutineId cls, ActorUpdateFn fn) {
    if (!classes.try_emplace(cls, fn).second)
        throw std::logic_error(std::string("actor class ") + routineName(cls) + " registered twice");
}

void ContentRegistry::addClasses(std::initializer_list<std::pair<const RoutineId, ActorUpdateFn>> entries) {
    for (const auto& [cls, fn] : entries) addClass(cls, fn);
}

ContentRegistry& contentRegistry() {
    static ContentRegistry r;
    return r;
}

const ContentRegistry& content() {
    const ContentRegistry& r = contentRegistry();
    if (!r.ready) throw std::logic_error("content not registered: call od::content::registerAll() at startup");
    return r;
}

} // namespace od::port
