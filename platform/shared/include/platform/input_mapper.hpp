#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "emulator/input_state.hpp"

namespace kairo::platform {

enum class InputAction : int {
    a = 0,
    b,
    l,
    r,
    start,
    select,
    up,
    down,
    left,
    right,
};

// Maps engine actions to platform-specific scancodes. The scancode type is
// deliberately `int` so this header stays free of SDL / NDK includes.
class InputMapper {
public:
    static constexpr std::size_t action_count = 10;

    InputMapper();

    void set_binding(InputAction action, int scancode);
    int get_binding(InputAction action) const;

    // Build an InputState from a raw keyboard state array (e.g. the result
    // of SDL_GetKeyboardState). Out-of-range or negative scancodes are
    // treated as "not pressed" rather than read out of bounds.
    kairo::core::InputState build_input_state(
        const std::uint8_t* keyboard_state, int size) const;

private:
    std::array<int, action_count> bindings_;
};

} // namespace kairo::platform
