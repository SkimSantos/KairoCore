#pragma once

#include <array>
#include <cstdint>

namespace kairo::core {

struct VideoFrame {
    static constexpr int width = 240;
    static constexpr int height = 160;
    static constexpr int pixel_count = width * height;

    // ARGB8888, packed in a uint32_t as 0xAARRGGBB on little-endian hosts.
    std::array<std::uint32_t, pixel_count> pixels{};
    std::uint64_t frame_number = 0;
};

} // namespace kairo::core
