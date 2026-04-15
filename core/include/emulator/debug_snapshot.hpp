#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kairo::core {

struct DebugEntry {
    std::string name;
    std::string value;
};

struct MemoryWatch {
    std::string name;
    std::uint32_t address = 0;
    // Number of bytes to read starting at address. Valid values: 1, 2, 4.
    std::uint8_t byte_count = 4;
};

struct DebugSnapshot {
    std::vector<DebugEntry> cpu_registers;
    std::vector<DebugEntry> memory_watch;
    std::vector<DebugEntry> hardware_registers;
    std::uint64_t frame_number = 0;
};

// Formats a value as "0x" + uppercase hex, zero-padded to `width` nibbles.
std::string format_hex(std::uint32_t value, int width);

} // namespace kairo::core
