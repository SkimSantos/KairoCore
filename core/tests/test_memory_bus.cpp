#include <cassert>
#include <cstdint>
#include <vector>

#include "backend/cart.hpp"
#include "backend/memory.hpp"

int main() {
    using kairo::backend::Cart;
    using kairo::backend::MemoryBus;

    MemoryBus bus;

    // EWRAM round-trip (byte/halfword/word).
    {
        bus.write8(0x02000000, 0x42);
        assert(bus.read8(0x02000000) == 0x42);

        bus.write16(0x02000010, 0xBEEF);
        assert(bus.read16(0x02000010) == 0xBEEF);
        // Little-endian byte layout.
        assert(bus.read8(0x02000010) == 0xEF);
        assert(bus.read8(0x02000011) == 0xBE);

        bus.write32(0x02000020, 0xDEADBEEF);
        assert(bus.read32(0x02000020) == 0xDEADBEEF);
        assert(bus.read16(0x02000020) == 0xBEEF);
        assert(bus.read16(0x02000022) == 0xDEAD);
    }

    // EWRAM mirroring — addresses above the 256 KiB window wrap.
    {
        bus.write32(0x02000100, 0xCAFEBABE);
        // Same low 18 bits → same physical word.
        assert(bus.read32(0x02040100) == 0xCAFEBABE);
        assert(bus.read32(0x02FC0100) == 0xCAFEBABE);
    }

    // IWRAM round-trip and mirror within 32 KiB.
    {
        bus.write32(0x03000000, 0x12345678);
        assert(bus.read32(0x03000000) == 0x12345678);
        assert(bus.read32(0x03008000) == 0x12345678); // mirror
    }

    // BIOS is read-only: writes are ignored.
    {
        bus.write32(0x00000000, 0xFFFFFFFF);
        assert(bus.read32(0x00000000) == 0x00000000);
    }

    // VRAM mirror: offset 0x18000 mirrors 0x10000 (the 32 KiB block).
    {
        bus.write32(0x06010000, 0xAABBCCDD);
        assert(bus.read32(0x06018000) == 0xAABBCCDD);
    }

    // OAM ignores 8-bit writes.
    {
        bus.write16(0x07000000, 0x1234);
        bus.write8(0x07000000, 0xFF);
        assert(bus.read16(0x07000000) == 0x1234);
    }

    // Palette byte-write broadcasts to both halves of a halfword.
    {
        bus.write16(0x05000000, 0x0000);
        bus.write8(0x05000000, 0x7F);
        // Both bytes of the halfword should now be 0x7F.
        assert(bus.read16(0x05000000) == 0x7F7F);
    }

    // Cart region is read-only; reads return 0 when no cart is attached.
    {
        MemoryBus bus2;
        assert(bus2.read32(0x08000000) == 0);
        bus2.write32(0x08000000, 0xDEADBEEF);
        assert(bus2.read32(0x08000000) == 0); // write ignored
    }

    // With a cart attached, cart reads return ROM bytes.
    {
        MemoryBus bus3;
        // Build a minimal 192-byte ROM with a marker sequence at offset 0.
        std::vector<std::uint8_t> rom(192, 0);
        rom[0] = 0x11;
        rom[1] = 0x22;
        rom[2] = 0x33;
        rom[3] = 0x44;

        Cart cart;
        assert(cart.load_from_bytes(std::move(rom)));
        bus3.set_cart(&cart);

        assert(bus3.read8(0x08000000) == 0x11);
        assert(bus3.read8(0x08000001) == 0x22);
        assert(bus3.read32(0x08000000) == 0x44332211);
        // Same ROM mirrored at 0x0A/0x0C wait-state windows.
        assert(bus3.read32(0x0A000000) == 0x44332211);
        assert(bus3.read32(0x0C000000) == 0x44332211);
    }

    // I/O register region: reads and writes land in the 1 KiB window.
    {
        bus.write16(0x04000000, 0x1234);
        assert(bus.read16(0x04000000) == 0x1234);
    }

    // reset() clears all backing RAM.
    {
        bus.reset();
        assert(bus.read32(0x02000000) == 0);
        assert(bus.read32(0x03000000) == 0);
        assert(bus.read32(0x06010000) == 0);
    }

    return 0;
}
