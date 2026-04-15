#pragma once

#include <cstdint>
#include <vector>

namespace kairo::core {

struct AudioFrame {
    static constexpr int sample_rate = 32768;
    static constexpr int channels = 2;

    // Interleaved stereo samples: L, R, L, R, ...
    std::vector<std::int16_t> samples;
};

} // namespace kairo::core
