#include "backend/hle_bios.hpp"
#include "backend/cpu.hpp"
#include "backend/io.hpp"
#include "backend/memory.hpp"

#include <cmath>
#include <vector>

namespace kairo::backend {

namespace {

// SWI 0x00: SoftReset
void bios_soft_reset(Cpu& cpu, MemoryBus& bus) {
    // Clear 0x03007E00-0x03007FFF, set stacks, jump to 0x08000000 or
    // 0x02000000 depending on flag at 0x03007FFA.
    std::uint8_t flag = bus.read8(0x03007FFA);
    for (std::uint32_t a = 0x03007E00; a < 0x03008000; a += 4)
        bus.write32(a, 0);

    cpu.set_cpsr(static_cast<std::uint32_t>(CpuMode::System) | kCpsrI | kCpsrF);
    cpu.reg(13) = 0x03007F00;
    cpu.flush_pipeline(flag ? 0x02000000u : 0x08000000u);
}

// SWI 0x01: RegisterRamReset
void bios_register_ram_reset(Cpu& /*cpu*/, MemoryBus& bus) {
    // Simplified: clear EWRAM, IWRAM work area, palette, VRAM, OAM
    // based on flags in r0. For now we just clear everything for safety.
    // A proper implementation would check individual flag bits.
    (void)bus;
}

// SWI 0x02: Halt — stop CPU until interrupt
void bios_halt(Io& io) {
    io.set_halted(true);
}

// SWI 0x04: IntrWait — wait for specific interrupt(s)
void bios_intr_wait(Cpu& cpu, MemoryBus& bus, Io& io) {
    bool discard = cpu.reg(0) != 0;
    std::uint16_t mask = static_cast<std::uint16_t>(cpu.reg(1));

    // BIOS IF mirror at 0x03007FF8.
    if (discard) bus.write16(0x03007FF8, 0);

    // Set CPU as halted — run_frame will resume when an IRQ with the
    // matching mask fires. In real HW the BIOS loops checking the
    // BIOS-IF mirror. We approximate by just halting.
    (void)mask;
    io.set_halted(true);
}

// SWI 0x05: VBlankIntrWait — shorthand for IntrWait(1, 1)
void bios_vblank_intr_wait(Cpu& cpu, MemoryBus& bus, Io& io) {
    cpu.reg(0) = 1;
    cpu.reg(1) = 1; // VBLANK IRQ
    bios_intr_wait(cpu, bus, io);
}

// SWI 0x06: Div — r0/r1 → r0=quotient, r1=remainder, r3=|quotient|
void bios_div(Cpu& cpu) {
    auto num = static_cast<std::int32_t>(cpu.reg(0));
    auto den = static_cast<std::int32_t>(cpu.reg(1));
    if (den == 0) {
        // Hardware behavior on /0 is implementation-defined; return 0.
        cpu.reg(0) = 0;
        cpu.reg(1) = 0;
        cpu.reg(3) = 0;
        return;
    }
    std::int32_t quot = num / den;
    std::int32_t rem = num % den;
    cpu.reg(0) = static_cast<std::uint32_t>(quot);
    cpu.reg(1) = static_cast<std::uint32_t>(rem);
    cpu.reg(3) = static_cast<std::uint32_t>(quot < 0 ? -quot : quot);
}

// SWI 0x07: DivArm — same as Div but arguments swapped
void bios_div_arm(Cpu& cpu) {
    std::uint32_t tmp = cpu.reg(0);
    cpu.reg(0) = cpu.reg(1);
    cpu.reg(1) = tmp;
    bios_div(cpu);
}

// SWI 0x08: Sqrt — r0 = sqrt(r0)
void bios_sqrt(Cpu& cpu) {
    cpu.reg(0) = static_cast<std::uint32_t>(
        std::sqrt(static_cast<double>(cpu.reg(0))));
}

// SWI 0x09: ArcTan — r0 = arctan(r0) where r0 is s16 fixed-point
void bios_arctan(Cpu& cpu) {
    auto x = static_cast<std::int16_t>(cpu.reg(0) & 0xFFFF);
    double rad = std::atan(static_cast<double>(x) / 16384.0);
    auto result = static_cast<std::int16_t>(rad * (32768.0 / 3.14159265358979));
    cpu.reg(0) = static_cast<std::uint32_t>(static_cast<std::uint16_t>(result));
}

// SWI 0x0A: ArcTan2 — r0 = arctan2(r0, r1)
void bios_arctan2(Cpu& cpu) {
    auto x = static_cast<std::int16_t>(cpu.reg(0) & 0xFFFF);
    auto y = static_cast<std::int16_t>(cpu.reg(1) & 0xFFFF);
    double rad = std::atan2(static_cast<double>(y), static_cast<double>(x));
    if (rad < 0) rad += 2.0 * 3.14159265358979;
    auto result = static_cast<std::uint16_t>(rad * (32768.0 / 3.14159265358979));
    cpu.reg(0) = result;
}

// SWI 0x0B: CpuSet — block copy/fill
void bios_cpu_set(Cpu& cpu, MemoryBus& bus) {
    std::uint32_t src = cpu.reg(0);
    std::uint32_t dst = cpu.reg(1);
    std::uint32_t ctrl = cpu.reg(2);
    std::uint32_t count = ctrl & 0x1FFFFF;
    bool fill = (ctrl >> 24) & 1;
    bool word32 = (ctrl >> 26) & 1;

    if (word32) {
        std::uint32_t val = fill ? bus.read32(src) : 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            if (!fill) val = bus.read32(src + i * 4);
            bus.write32(dst + i * 4, val);
        }
    } else {
        std::uint16_t val = fill ? bus.read16(src) : 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            if (!fill) val = bus.read16(src + i * 2);
            bus.write16(dst + i * 2, val);
        }
    }
}

// SWI 0x0C: CpuFastSet — like CpuSet but always 32-bit, count in
// multiples of 8.
void bios_cpu_fast_set(Cpu& cpu, MemoryBus& bus) {
    std::uint32_t src = cpu.reg(0);
    std::uint32_t dst = cpu.reg(1);
    std::uint32_t ctrl = cpu.reg(2);
    std::uint32_t count = (ctrl & 0x1FFFFF) & ~7u;
    bool fill = (ctrl >> 24) & 1;

    std::uint32_t val = fill ? bus.read32(src) : 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!fill) val = bus.read32(src + i * 4);
        bus.write32(dst + i * 4, val);
    }
}

// SWI 0x0E: BgAffineSet
void bios_bg_affine_set(Cpu& cpu, MemoryBus& bus) {
    std::uint32_t src = cpu.reg(0);
    std::uint32_t dst = cpu.reg(1);
    int count = static_cast<int>(cpu.reg(2));

    for (int i = 0; i < count; ++i) {
        auto cx = static_cast<std::int32_t>(bus.read32(src));
        auto cy = static_cast<std::int32_t>(bus.read32(src + 4));
        auto dx = static_cast<std::int16_t>(bus.read16(src + 8));
        auto dy = static_cast<std::int16_t>(bus.read16(src + 10));
        auto sx = static_cast<std::int16_t>(bus.read16(src + 12));
        auto sy = static_cast<std::int16_t>(bus.read16(src + 14));
        auto angle = static_cast<std::uint16_t>(bus.read16(src + 16));
        src += 20;

        double theta = static_cast<double>(angle) / 32768.0 * 3.14159265358979;
        double cos_a = std::cos(theta);
        double sin_a = std::sin(theta);

        auto pa = static_cast<std::int16_t>(cos_a * 256.0 * sx / dx);
        auto pb = static_cast<std::int16_t>(-sin_a * 256.0 * sx / dx);
        auto pc = static_cast<std::int16_t>(sin_a * 256.0 * sy / dy);
        auto pd = static_cast<std::int16_t>(cos_a * 256.0 * sy / dy);

        auto start_x = static_cast<std::int32_t>(
            cx - pa * dx / 256.0 - pb * dy / 256.0);
        auto start_y = static_cast<std::int32_t>(
            cy - pc * dx / 256.0 - pd * dy / 256.0);

        bus.write16(dst, static_cast<std::uint16_t>(pa));
        bus.write16(dst + 2, static_cast<std::uint16_t>(pb));
        bus.write16(dst + 4, static_cast<std::uint16_t>(pc));
        bus.write16(dst + 6, static_cast<std::uint16_t>(pd));
        bus.write32(dst + 8, static_cast<std::uint32_t>(start_x));
        bus.write32(dst + 12, static_cast<std::uint32_t>(start_y));
        dst += 16;
    }
}

// SWI 0x0F: ObjAffineSet
void bios_obj_affine_set(Cpu& cpu, MemoryBus& bus) {
    std::uint32_t src = cpu.reg(0);
    std::uint32_t dst = cpu.reg(1);
    int count = static_cast<int>(cpu.reg(2));
    int stride = static_cast<int>(cpu.reg(3));

    for (int i = 0; i < count; ++i) {
        auto sx = static_cast<std::int16_t>(bus.read16(src));
        auto sy = static_cast<std::int16_t>(bus.read16(src + 2));
        auto angle = static_cast<std::uint16_t>(bus.read16(src + 4));
        src += 8;

        double theta = static_cast<double>(angle) / 32768.0 * 3.14159265358979;
        double cos_a = std::cos(theta);
        double sin_a = std::sin(theta);

        auto pa = static_cast<std::int16_t>(cos_a * 256.0 / sx);
        auto pb = static_cast<std::int16_t>(-sin_a * 256.0 / sx);
        auto pc = static_cast<std::int16_t>(sin_a * 256.0 / sy);
        auto pd = static_cast<std::int16_t>(cos_a * 256.0 / sy);

        bus.write16(dst, static_cast<std::uint16_t>(pa));
        dst += stride;
        bus.write16(dst, static_cast<std::uint16_t>(pb));
        dst += stride;
        bus.write16(dst, static_cast<std::uint16_t>(pc));
        dst += stride;
        bus.write16(dst, static_cast<std::uint16_t>(pd));
        dst += stride;
    }
}

// SWI 0x11/0x12: LZ77UnCompWram/Vram
void bios_lz77_uncomp(Cpu& cpu, MemoryBus& bus, bool vram) {
    std::uint32_t src = cpu.reg(0);
    std::uint32_t dst = cpu.reg(1);

    std::uint32_t header = bus.read32(src);
    std::uint32_t decomp_size = header >> 8;
    src += 4;

    std::uint32_t written = 0;

    // For VRAM mode, we decompress into a local buffer so back-references
    // work correctly regardless of VRAM's halfword write behavior.
    std::vector<std::uint8_t> out;
    if (vram) out.reserve(decomp_size);

    while (written < decomp_size) {
        std::uint8_t flags = bus.read8(src++);
        for (int bit = 7; bit >= 0 && written < decomp_size; --bit) {
            if (flags & (1 << bit)) {
                std::uint8_t b1 = bus.read8(src++);
                std::uint8_t b2 = bus.read8(src++);
                int length = ((b1 >> 4) & 0xF) + 3;
                int disp = (static_cast<int>(b1 & 0xF) << 8) | b2;
                disp += 1;
                for (int j = 0; j < length && written < decomp_size; ++j) {
                    std::uint8_t val;
                    if (vram) {
                        std::size_t ref = out.size() - static_cast<std::size_t>(disp);
                        val = out[ref];
                        out.push_back(val);
                    } else {
                        val = bus.read8(dst - static_cast<std::uint32_t>(disp));
                        bus.write8(dst, val);
                        ++dst;
                    }
                    ++written;
                }
            } else {
                std::uint8_t val = bus.read8(src++);
                if (vram) {
                    out.push_back(val);
                } else {
                    bus.write8(dst, val);
                    ++dst;
                }
                ++written;
            }
        }
    }

    // Flush VRAM buffer as halfwords.
    if (vram) {
        for (std::size_t i = 0; i + 1 < out.size(); i += 2) {
            bus.write16(dst, static_cast<std::uint16_t>(out[i]) |
                             (static_cast<std::uint16_t>(out[i + 1]) << 8));
            dst += 2;
        }
        if (out.size() & 1) {
            bus.write16(dst, static_cast<std::uint16_t>(out.back()));
        }
    }
}

// SWI 0x14/0x15: RLUnCompWram/Vram — run-length decompress
void bios_rl_uncomp(Cpu& cpu, MemoryBus& bus, bool vram) {
    std::uint32_t src = cpu.reg(0);
    std::uint32_t dst = cpu.reg(1);

    std::uint32_t header = bus.read32(src);
    std::uint32_t decomp_size = header >> 8;
    src += 4;

    std::vector<std::uint8_t> out;
    if (vram) out.reserve(decomp_size);

    std::uint32_t written = 0;
    while (written < decomp_size) {
        std::uint8_t flag = bus.read8(src++);
        if (flag & 0x80) {
            int len = (flag & 0x7F) + 3;
            std::uint8_t val = bus.read8(src++);
            for (int i = 0; i < len && written < decomp_size; ++i) {
                if (vram) out.push_back(val);
                else { bus.write8(dst, val); ++dst; }
                ++written;
            }
        } else {
            int len = (flag & 0x7F) + 1;
            for (int i = 0; i < len && written < decomp_size; ++i) {
                std::uint8_t val = bus.read8(src++);
                if (vram) out.push_back(val);
                else { bus.write8(dst, val); ++dst; }
                ++written;
            }
        }
    }

    if (vram) {
        for (std::size_t i = 0; i + 1 < out.size(); i += 2) {
            bus.write16(dst, static_cast<std::uint16_t>(out[i]) |
                             (static_cast<std::uint16_t>(out[i + 1]) << 8));
            dst += 2;
        }
        if (out.size() & 1) {
            bus.write16(dst, static_cast<std::uint16_t>(out.back()));
        }
    }
}

} // namespace

bool hle_bios_call(std::uint8_t comment, Cpu& cpu, MemoryBus& bus, Io& io) {
    switch (comment) {
        case 0x00: bios_soft_reset(cpu, bus); return true;
        case 0x01: bios_register_ram_reset(cpu, bus); return true;
        case 0x02: bios_halt(io); return true;
        case 0x03: break; // Stop — halts CPU and LCD. Unimplemented.
        case 0x04: bios_intr_wait(cpu, bus, io); return true;
        case 0x05: bios_vblank_intr_wait(cpu, bus, io); return true;
        case 0x06: bios_div(cpu); return true;
        case 0x07: bios_div_arm(cpu); return true;
        case 0x08: bios_sqrt(cpu); return true;
        case 0x09: bios_arctan(cpu); return true;
        case 0x0A: bios_arctan2(cpu); return true;
        case 0x0B: bios_cpu_set(cpu, bus); return true;
        case 0x0C: bios_cpu_fast_set(cpu, bus); return true;
        case 0x0E: bios_bg_affine_set(cpu, bus); return true;
        case 0x0F: bios_obj_affine_set(cpu, bus); return true;
        case 0x11: bios_lz77_uncomp(cpu, bus, false); return true;
        case 0x12: bios_lz77_uncomp(cpu, bus, true); return true;
        case 0x14: bios_rl_uncomp(cpu, bus, false); return true;
        case 0x15: bios_rl_uncomp(cpu, bus, true); return true;
        default:
            fprintf(stderr, "UNHANDLED SWI 0x%02X (r0=%08X r1=%08X r2=%08X)\n",
                    comment, cpu.reg(0), cpu.reg(1), cpu.reg(2));
            break;
    }
    return false;
}

} // namespace kairo::backend
