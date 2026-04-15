#include "platform/input_mapper.hpp"

namespace kairo::platform {

namespace {

constexpr std::size_t index_of(InputAction a) {
    return static_cast<std::size_t>(a);
}

} // namespace

InputMapper::InputMapper() {
    bindings_.fill(-1);
}

void InputMapper::set_binding(InputAction action, int scancode) {
    bindings_[index_of(action)] = scancode;
}

int InputMapper::get_binding(InputAction action) const {
    return bindings_[index_of(action)];
}

kairo::core::InputState InputMapper::build_input_state(
    const std::uint8_t* keyboard_state, int size) const {
    kairo::core::InputState s{};
    const auto pressed = [&](InputAction a) -> bool {
        const int sc = bindings_[index_of(a)];
        if (sc < 0 || sc >= size) return false;
        return keyboard_state[sc] != 0;
    };
    s.a      = pressed(InputAction::a);
    s.b      = pressed(InputAction::b);
    s.l      = pressed(InputAction::l);
    s.r      = pressed(InputAction::r);
    s.start  = pressed(InputAction::start);
    s.select = pressed(InputAction::select);
    s.up     = pressed(InputAction::up);
    s.down   = pressed(InputAction::down);
    s.left   = pressed(InputAction::left);
    s.right  = pressed(InputAction::right);
    return s;
}

} // namespace kairo::platform
