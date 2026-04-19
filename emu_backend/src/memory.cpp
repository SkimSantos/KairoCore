#include "backend/memory.hpp"
#include "backend/io.hpp"

namespace kairo::backend {

namespace {

// VRAM mirror quirk: the 96 KiB region is addressed by masking into
// a 128 KiB window — the top 32 KiB mirrors the preceding 32 KiB slab.
std::size_t vram_offset(std::uint32_t addr) {
    std::uint32_t offset = addr & 0x1FFFF; // 128 KiB window
    if (offset >= 0x18000) {
        offset -= 0x8000;
    }
    return offset;
}

std::uint16_t read16_le(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) |
           (static_cast<std::uint16_t>(p[1]) << 8);
}

std::uint32_t read32_le(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

void write16_le(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
}

void write32_le(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

} // namespace

MemoryBus::MemoryBus() { reset(); }

void MemoryBus::set_cart(const Cart* cart) { cart_ = cart; }
void MemoryBus::set_io(Io* io) { io_ = io; }

void MemoryBus::reset() {
    bios_.fill(0);
    ewram_.fill(0);
    iwram_.fill(0);
    io_raw_.fill(0);
    palette_.fill(0);
    vram_.fill(0);
    oam_.fill(0);
    flash_.reset();
}

void MemoryBus::write_bios32(std::uint32_t offset, std::uint32_t value) {
    if (offset + 3 < kBiosSize) {
        bios_[offset]     = static_cast<std::uint8_t>(value);
        bios_[offset + 1] = static_cast<std::uint8_t>(value >> 8);
        bios_[offset + 2] = static_cast<std::uint8_t>(value >> 16);
        bios_[offset + 3] = static_cast<std::uint8_t>(value >> 24);
    }
}

// --- Reads ------------------------------------------------------------

std::uint8_t MemoryBus::read8(std::uint32_t addr) const {
    switch ((addr >> 24) & 0xF) {
        case 0x0:
            if ((addr & 0xFFFFFF) < kBiosSize) return bios_[addr & 0x3FFF];
            return 0;
        case 0x2:
            return ewram_[addr & 0x3FFFF];
        case 0x3:
            return iwram_[addr & 0x7FFF];
        case 0x4: {
            const std::uint32_t off = addr & 0xFFFFFF;
            if (io_) return io_->read8(off);
            return off < kIoSize ? io_raw_[off] : std::uint8_t{0};
        }
        case 0x5:
            return palette_[addr & 0x3FF];
        case 0x6:
            return vram_[vram_offset(addr)];
        case 0x7:
            return oam_[addr & 0x3FF];
        case 0x8: case 0x9:
        case 0xA: case 0xB:
        case 0xC: case 0xD: {
            if (!cart_) return 0;
            const auto& rom = cart_->rom();
            const std::uint32_t off = addr & 0x01FFFFFF;
            return off < rom.size() ? rom[off] : std::uint8_t{0};
        }
        case 0xE: case 0xF:
            return flash_.read(addr & 0xFFFF);
        default:
            return 0;
    }
}

std::uint16_t MemoryBus::read16(std::uint32_t addr) const {
    // ARM always halfword-aligns halfword reads; low bit is ignored.
    addr &= ~1u;
    switch ((addr >> 24) & 0xF) {
        case 0x0:
            if ((addr & 0xFFFFFF) + 1 < kBiosSize)
                return read16_le(&bios_[addr & 0x3FFE]);
            return 0;
        case 0x2: return read16_le(&ewram_[addr & 0x3FFFE]);
        case 0x3: return read16_le(&iwram_[addr & 0x7FFE]);
        case 0x4: {
            const std::uint32_t off = addr & 0xFFFFFE;
            if (io_) return io_->read16(off);
            return off + 1 < kIoSize ? read16_le(&io_raw_[off]) : std::uint16_t{0};
        }
        case 0x5: return read16_le(&palette_[addr & 0x3FE]);
        case 0x6: return read16_le(&vram_[vram_offset(addr) & ~std::size_t{1}]);
        case 0x7: return read16_le(&oam_[addr & 0x3FE]);
        case 0x8: case 0x9:
        case 0xA: case 0xB:
        case 0xC: case 0xD: {
            if (!cart_) return 0;
            const auto& rom = cart_->rom();
            const std::uint32_t off = addr & 0x01FFFFFE;
            return off + 1 < rom.size() ? read16_le(&rom[off]) : std::uint16_t{0};
        }
        case 0xE: case 0xF: return flash_.read(addr & 0xFFFF);
        default: return 0;
    }
}

std::uint32_t MemoryBus::read32(std::uint32_t addr) const {
    // ARM always word-aligns word reads.
    addr &= ~3u;
    switch ((addr >> 24) & 0xF) {
        case 0x0:
            if ((addr & 0xFFFFFF) + 3 < kBiosSize)
                return read32_le(&bios_[addr & 0x3FFC]);
            return 0;
        case 0x2: return read32_le(&ewram_[addr & 0x3FFFC]);
        case 0x3: return read32_le(&iwram_[addr & 0x7FFC]);
        case 0x4: {
            const std::uint32_t off = addr & 0xFFFFFC;
            if (io_) return io_->read32(off);
            return off + 3 < kIoSize ? read32_le(&io_raw_[off]) : std::uint32_t{0};
        }
        case 0x5: return read32_le(&palette_[addr & 0x3FC]);
        case 0x6: return read32_le(&vram_[vram_offset(addr) & ~std::size_t{3}]);
        case 0x7: return read32_le(&oam_[addr & 0x3FC]);
        case 0x8: case 0x9:
        case 0xA: case 0xB:
        case 0xC: case 0xD: {
            if (!cart_) return 0;
            const auto& rom = cart_->rom();
            const std::uint32_t off = addr & 0x01FFFFFC;
            return off + 3 < rom.size() ? read32_le(&rom[off]) : std::uint32_t{0};
        }
        case 0xE: case 0xF: return flash_.read(addr & 0xFFFF);
        default: return 0;
    }
}

// --- Writes -----------------------------------------------------------

void MemoryBus::write8(std::uint32_t addr, std::uint8_t value) {
    switch ((addr >> 24) & 0xF) {
        case 0x0: break; // BIOS is read-only
        case 0x2: ewram_[addr & 0x3FFFF] = value; break;
        case 0x3: iwram_[addr & 0x7FFF]  = value; break;
        case 0x4: {
            const std::uint32_t off = addr & 0xFFFFFF;
            if (io_) { io_->write8(off, value); break; }
            if (off < kIoSize) io_raw_[off] = value;
            break;
        }
        // 8-bit writes to palette/VRAM/OAM have quirky "write as halfword"
        // semantics on real hardware. For now we drop byte writes to OAM
        // (matches hardware) and broadcast byte writes to palette/VRAM as
        // the low byte in both halves — a common approximation.
        case 0x5: {
            const std::size_t off = (addr & 0x3FF) & ~std::size_t{1};
            palette_[off]     = value;
            palette_[off + 1] = value;
            break;
        }
        case 0x6: {
            const std::size_t off = vram_offset(addr) & ~std::size_t{1};
            vram_[off]     = value;
            vram_[off + 1] = value;
            break;
        }
        case 0x7: break; // OAM ignores 8-bit writes.
        case 0x8: case 0x9:
        case 0xA: case 0xB:
        case 0xC: case 0xD: break; // Cart ROM is read-only.
        case 0xE: case 0xF: flash_.write(addr & 0xFFFF, value); break;
        default: break;
    }
}

void MemoryBus::write16(std::uint32_t addr, std::uint16_t value) {
    addr &= ~1u;
    switch ((addr >> 24) & 0xF) {
        case 0x0: break;
        case 0x2: write16_le(&ewram_[addr & 0x3FFFE], value); break;
        case 0x3: write16_le(&iwram_[addr & 0x7FFE], value); break;
        case 0x4: {
            const std::uint32_t off = addr & 0xFFFFFE;
            if (io_) { io_->write16(off, value); break; }
            if (off + 1 < kIoSize) write16_le(&io_raw_[off], value);
            break;
        }
        case 0x5: write16_le(&palette_[addr & 0x3FE], value); break;
        case 0x6: write16_le(&vram_[vram_offset(addr) & ~std::size_t{1}], value); break;
        case 0x7: write16_le(&oam_[addr & 0x3FE], value); break;
        case 0x8: case 0x9:
        case 0xA: case 0xB:
        case 0xC: case 0xD: break;
        case 0xE: case 0xF:
            flash_.write(addr & 0xFFFF, static_cast<std::uint8_t>(value & 0xFF));
            break;
        default: break;
    }
}

void MemoryBus::write32(std::uint32_t addr, std::uint32_t value) {
    addr &= ~3u;
    switch ((addr >> 24) & 0xF) {
        case 0x0: break;
        case 0x2: write32_le(&ewram_[addr & 0x3FFFC], value); break;
        case 0x3: write32_le(&iwram_[addr & 0x7FFC], value); break;
        case 0x4: {
            const std::uint32_t off = addr & 0xFFFFFC;
            if (io_) { io_->write32(off, value); break; }
            if (off + 3 < kIoSize) write32_le(&io_raw_[off], value);
            break;
        }
        case 0x5: write32_le(&palette_[addr & 0x3FC], value); break;
        case 0x6: write32_le(&vram_[vram_offset(addr) & ~std::size_t{3}], value); break;
        case 0x7: write32_le(&oam_[addr & 0x3FC], value); break;
        case 0x8: case 0x9:
        case 0xA: case 0xB:
        case 0xC: case 0xD: break;
        case 0xE: case 0xF:
            flash_.write(addr & 0xFFFF, static_cast<std::uint8_t>(value & 0xFF));
            break;
        default: break;
    }
}

} // namespace kairo::backend
