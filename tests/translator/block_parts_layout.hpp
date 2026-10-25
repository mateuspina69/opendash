// The script block's and the stage block's parts in the exe's layout (block_parts_layout.cpp): the blocks' retained
// bytes and the parts held, and the parts' lifetimes followed (game/block_parts BlockPartsObserver)
#pragma once

#include "translator/layout.hpp"

namespace od::test {

// the blocks' bytes and their held parts (the observers bound on the first call)
void blockPartsLayout(LayoutVisitor& v);
// a part of a type with no layout: a translator error naming it
[[noreturn]] void unknownBlockPart(const char* name);

} // namespace od::test
