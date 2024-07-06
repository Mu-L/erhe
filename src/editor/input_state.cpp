#include "input_state.hpp"

namespace editor {

auto Input_state::on_key(
    const signed int keycode,
    const uint32_t   modifier_mask,
    const bool       pressed
) -> bool
{
    static_cast<void>(keycode);
    static_cast<void>(pressed);
    shift   = (modifier_mask & erhe::window::Key_modifier_bit_shift) == erhe::window::Key_modifier_bit_shift;
    alt     = (modifier_mask & erhe::window::Key_modifier_bit_menu ) == erhe::window::Key_modifier_bit_menu;
    control = (modifier_mask & erhe::window::Key_modifier_bit_ctrl ) == erhe::window::Key_modifier_bit_ctrl;
    return false; // not consumed
}

auto Input_state::on_mouse_move(float absolute_x, float absolute_y, float, float, const uint32_t) -> bool
{
    mouse_position = glm::vec2{absolute_x, absolute_y};
    return false; // not consumed
}

auto Input_state::on_mouse_button(const uint32_t button, const bool pressed, uint32_t) -> bool
{
    mouse_button[button] = pressed;
    return false; // not consumed
}

} // namespace editor

