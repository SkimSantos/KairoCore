#pragma once

#include <cstdint>

namespace kairo::backend {

class Cpu;
class MemoryBus;
class Io;

// High-level emulation of GBA BIOS calls. Reads arguments from CPU
// registers, performs the operation, writes results back, and returns
// as if the real BIOS SWI handler had run (restoring CPSR from SPSR).
//
// Call this from SWI handlers instead of branching to 0x08.
// `comment` is the SWI number (bits 23:16 of ARM instr, bits 7:0 of
// Thumb instr). Returns true if the call was handled.
bool hle_bios_call(std::uint8_t comment, Cpu& cpu, MemoryBus& bus, Io& io);

} // namespace kairo::backend
