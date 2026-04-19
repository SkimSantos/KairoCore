#pragma once

#include <array>
#include <cstdint>

#include "backend/cart.hpp"
#include "backend/flash.hpp"

namespace kairo::backend {

class Io;

// GBA memory map (see GBATEK §Memory Overview). Addresses are 32-bit
// but only the low 28 bits are meaningful; the top nibble selects the
// region. Regions smaller than their address window alias/mirror.
//
//   0x00 BIOS ROM (16 KiB, read-only)
//   0x02 EWRAM   (256 KiB)
//   0x03 IWRAM   (32 KiB)
//   0x04 I/O registers (~1 KiB, handled per-register)
//   0x05 Palette RAM (1 KiB)
//   0x06 VRAM     (96 KiB, with a quirky mirror)
//   0x07 OAM      (1 KiB)
//   0x08/0x0A/0x0C Cartridge ROM (same data, different wait states)
//   0x0E SRAM     (up to 64 KiB, cartridge save RAM)
//
// Addresses outside any of these regions read as "open bus" — we
// return 0 for now; a faithful implementation would return the last
// prefetched instruction.

inline constexpr std::size_t kBiosSize    = 16 * 1024;
inline constexpr std::size_t kEwramSize   = 256 * 1024;
inline constexpr std::size_t kIwramSize   = 32 * 1024;
inline constexpr std::size_t kPaletteSize = 1 * 1024;
inline constexpr std::size_t kVramSize    = 96 * 1024;
inline constexpr std::size_t kOamSize     = 1 * 1024;
inline constexpr std::size_t kIoSize      = 0x400;
inline constexpr std::size_t kSramSize    = 64 * 1024;

class MemoryBus {
public:
    MemoryBus();

    // Attach a loaded cart. The bus does NOT take ownership — the Cart
    // must outlive the bus. Passing nullptr clears the cart.
    void set_cart(const Cart* cart);
    void set_io(Io* io);

    // Clear all RAM to zero (hardware reset).
    void reset();

    std::uint8_t  read8(std::uint32_t addr) const;
    std::uint16_t read16(std::uint32_t addr) const;
    std::uint32_t read32(std::uint32_t addr) const;

    void write8(std::uint32_t addr, std::uint8_t value);
    void write16(std::uint32_t addr, std::uint16_t value);
    void write32(std::uint32_t addr, std::uint32_t value);

    // Backdoor write into BIOS memory — used to patch the IRQ/SWI
    // stubs that the real BIOS would contain. Normal bus writes to
    // region 0x00 are blocked (BIOS is read-only to the CPU).
    void write_bios32(std::uint32_t offset, std::uint32_t value);

    // Direct RAM accessors (bypass region dispatch) — useful for tests
    // and for the PPU which reads VRAM/palette/OAM every scanline.
    const std::array<std::uint8_t, kEwramSize>&   ewram()   const { return ewram_; }
    const std::array<std::uint8_t, kIwramSize>&   iwram()   const { return iwram_; }
    const std::array<std::uint8_t, kVramSize>&    vram()    const { return vram_; }
    const std::array<std::uint8_t, kPaletteSize>& palette() const { return palette_; }
    const std::array<std::uint8_t, kOamSize>&     oam()     const { return oam_; }

private:
    std::array<std::uint8_t, kBiosSize>    bios_{};
    std::array<std::uint8_t, kEwramSize>   ewram_{};
    std::array<std::uint8_t, kIwramSize>   iwram_{};
    std::array<std::uint8_t, kIoSize>      io_raw_{};
    std::array<std::uint8_t, kPaletteSize> palette_{};
    std::array<std::uint8_t, kVramSize>    vram_{};
    std::array<std::uint8_t, kOamSize>     oam_{};
    Flash128K flash_{};

    const Cart* cart_ = nullptr;
    Io* io_ = nullptr;
};

} // namespace kairo::backend
