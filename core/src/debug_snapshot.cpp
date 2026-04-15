#include "emulator/debug_snapshot.hpp"

namespace kairo::core {

std::string format_hex(std::uint32_t value, int width) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string out(static_cast<std::size_t>(width), '0');
    for (int i = width - 1; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = digits[value & 0xFu];
        value >>= 4;
    }
    return "0x" + out;
}

} // namespace kairo::core
