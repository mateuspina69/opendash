#include "translator/game_memory.hpp"

#include <stdexcept>


namespace od {

namespace detail {
void gameMemoryUnbound() { throw std::logic_error("game memory: no GameMem bound (bindGameMemory)"); }
} // namespace detail

void bindGameMemory(GameMem* m) {
    detail::boundGameMemory = m;
    bindLoadedFiles(m ? &m->mutableLoadedFiles() : nullptr); // the image's loaded files are the game's
}

} // namespace od
