#include <cassert>
#include <chrono>

#include "emulator/emulator_instance.hpp"

int main() {
    using kairo::core::EmulatorInstance;

    EmulatorInstance emu(nullptr, nullptr);

    // Default multiplier is 1x.
    assert(emu.get_fast_forward_multiplier() == 1);

    // Valid values stick.
    emu.set_fast_forward_multiplier(2);
    assert(emu.get_fast_forward_multiplier() == 2);

    emu.set_fast_forward_multiplier(8);
    assert(emu.get_fast_forward_multiplier() == 8);

    emu.set_fast_forward_multiplier(16);
    assert(emu.get_fast_forward_multiplier() == 16);

    // Clamp above the maximum.
    emu.set_fast_forward_multiplier(17);
    assert(emu.get_fast_forward_multiplier() == 16);
    emu.set_fast_forward_multiplier(1000);
    assert(emu.get_fast_forward_multiplier() == 16);

    // Clamp below the minimum.
    emu.set_fast_forward_multiplier(0);
    assert(emu.get_fast_forward_multiplier() == 1);
    emu.set_fast_forward_multiplier(-5);
    assert(emu.get_fast_forward_multiplier() == 1);

    // Frame duration scales inversely with multiplier.
    // Base period: 16'743'035 ns (GBA 59.7275 Hz).
    emu.set_fast_forward_multiplier(1);
    const auto d1 = emu.target_frame_duration().count();
    assert(d1 == 16'743'035);

    emu.set_fast_forward_multiplier(2);
    const auto d2 = emu.target_frame_duration().count();
    assert(d2 == 8'371'517);

    emu.set_fast_forward_multiplier(4);
    const auto d4 = emu.target_frame_duration().count();
    assert(d4 == 4'185'758);

    emu.set_fast_forward_multiplier(16);
    const auto d16 = emu.target_frame_duration().count();
    assert(d16 == 1'046'439);

    // Monotonic: higher multiplier → shorter frame period.
    assert(d1 > d2);
    assert(d2 > d4);
    assert(d4 > d16);

    return 0;
}
