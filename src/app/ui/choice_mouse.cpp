#include "app/ui/choice_mouse.hpp"

#include "game/choice_pointer.hpp"

namespace od::app {

bool choiceMouse(const SDL_Event& e, float density, float s, float ox, int& hover,
                 uint16_t& clickPress, int& clickDelay) {
    const od::port::MessageWindow* w = od::port::choiceWindow();
    if (!w) return false;
    auto at = [&](float x, float y) { return od::port::choiceOptionAt(*w, (x * density - ox) / s, y * density / s); };
    if (e.type == SDL_EVENT_MOUSE_MOTION) {
        const int k = at(e.motion.x, e.motion.y);
        if (k >= 0 && k != hover) od::port::choicePointer() = {w, k};
        hover = k;
    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        const int k = at(e.button.x, e.button.y);
        if (e.button.button == SDL_BUTTON_LEFT && k >= 0) { // onto the option, then its confirm
            od::port::choicePointer() = {w, k};
            clickPress = 0x2000;
            clickDelay = 2;
        } else if (e.button.button == SDL_BUTTON_RIGHT) {
            clickPress = 0x4000;
            clickDelay = 1;
        }
    }
    return true;
}

} // namespace od::app
