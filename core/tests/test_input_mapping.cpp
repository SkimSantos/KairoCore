#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>

#include "emulator/input_state.hpp"
#include "platform/input_mapper.hpp"

int main() {
    using kairo::platform::InputAction;
    using kairo::platform::InputMapper;

    InputMapper m;

    // Default state: no bindings set → all actions unpressed regardless
    // of keyboard state.
    std::array<std::uint8_t, 512> keys{};
    keys.fill(1);
    auto s = m.build_input_state(keys.data(), static_cast<int>(keys.size()));
    assert(!s.a && !s.b && !s.l && !s.r);
    assert(!s.start && !s.select);
    assert(!s.up && !s.down && !s.left && !s.right);

    // set_binding / get_binding round-trip.
    m.set_binding(InputAction::a, 42);
    assert(m.get_binding(InputAction::a) == 42);
    m.set_binding(InputAction::a, 7);
    assert(m.get_binding(InputAction::a) == 7);

    // Individual bindings map through to the right action.
    keys.fill(0);
    m.set_binding(InputAction::a, 42);
    m.set_binding(InputAction::up, 100);
    m.set_binding(InputAction::start, 200);

    keys[42] = 1;
    keys[100] = 1;
    keys[200] = 1;
    s = m.build_input_state(keys.data(), static_cast<int>(keys.size()));
    assert(s.a);
    assert(s.up);
    assert(s.start);
    assert(!s.b);
    assert(!s.down);
    assert(!s.select);

    // Releasing a key clears just that action.
    keys[42] = 0;
    s = m.build_input_state(keys.data(), static_cast<int>(keys.size()));
    assert(!s.a);
    assert(s.up);
    assert(s.start);

    // Out-of-range scancode must not read out of bounds.
    m.set_binding(InputAction::b, 9999);
    s = m.build_input_state(keys.data(), static_cast<int>(keys.size()));
    assert(!s.b);

    // Negative scancode is also safe.
    m.set_binding(InputAction::r, -1);
    s = m.build_input_state(keys.data(), static_cast<int>(keys.size()));
    assert(!s.r);

    // Full 10-action remap.
    m.set_binding(InputAction::a,      1);
    m.set_binding(InputAction::b,      2);
    m.set_binding(InputAction::l,      3);
    m.set_binding(InputAction::r,      4);
    m.set_binding(InputAction::start,  5);
    m.set_binding(InputAction::select, 6);
    m.set_binding(InputAction::up,     7);
    m.set_binding(InputAction::down,   8);
    m.set_binding(InputAction::left,   9);
    m.set_binding(InputAction::right, 10);

    std::fill(keys.begin(), keys.end(), std::uint8_t{0});
    for (int i = 1; i <= 10; ++i) keys[static_cast<std::size_t>(i)] = 1;

    s = m.build_input_state(keys.data(), static_cast<int>(keys.size()));
    assert(s.a && s.b && s.l && s.r);
    assert(s.start && s.select);
    assert(s.up && s.down && s.left && s.right);

    // Small keyboard state: bindings beyond size are treated as unpressed.
    std::array<std::uint8_t, 5> small{1, 1, 1, 1, 1};
    s = m.build_input_state(small.data(), static_cast<int>(small.size()));
    assert(s.a);      // scancode 1 in range
    assert(s.b);      // 2 in range
    assert(s.l);      // 3 in range
    assert(s.r);      // 4 in range
    assert(!s.start); // 5 is out of range (size == 5 → indices 0..4)
    assert(!s.up);    // 7 is out of range

    return 0;
}
