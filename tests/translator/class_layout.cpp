// The class state selects its named layout; stage/variant dispatch stays in the game.
#include "translator/actor_layout.hpp"
#include <stdexcept>
#include <string>
namespace od::test {
void classLayout(LayoutVisitor& v, uint32_t a, od::port::ActorClassState& s) {
    if (classes_calbania(v, a, s)) return;
    if (classes_forbidden_island(v, a, s)) return;
    if (classes_game(v, a, s)) return;
    if (classes_game_shots(v, a, s)) return;
    if (classes_intro(v, a, s)) return;
    if (classes_late_game(v, a, s)) return;
    if (classes_manda(v, a, s)) return;
    if (classes_nino(v, a, s)) return;
    if (classes_saul_kada(v, a, s)) return;
    if (classes_subruins(v, a, s)) return;
    if (classes_yosyonke(v, a, s)) return;
    throw std::runtime_error(std::string("translator: no class layout for ") + typeid(s).name());
}
}
