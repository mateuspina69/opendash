// The texts the message codes build (game/text_script_state) and the program banks' bytes a code patched
// (game/text_memory), at their exe addresses.
#include <cstdint>

#include "game/text_memory.hpp"
#include "game/text_script_state.hpp"
#include "translator/layout.hpp"

namespace od::test {

void textLayout(LayoutVisitor& v) {
    auto& texts = od::port::text_script_state::insertedTexts();
    v.field(od::stateAddress(od::TableId::TextScriptNumberText), texts.number);
    v.field(od::stateAddress(od::TableId::TextScriptTimeText), texts.time);
    // the patches the run made (a load starts none: the code patches a byte before it reads it)
    auto& patched = od::port::patchedProgramText();
    if (v.loading()) patched.clear();
    for (auto& [address, byte] : patched) v.field(address, byte);
}

} // namespace od::test
