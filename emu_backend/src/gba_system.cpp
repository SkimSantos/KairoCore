#include "backend/gba_system.hpp"

#include <cstdio>

static int g_frame_count = 0;

void trace_310c_write(std::uint16_t old_val, std::uint16_t new_val) {
    std::fprintf(stderr, "  [WRITE 0300310C] frame=%d old=%04X new=%04X\n",
        g_frame_count, old_val, new_val);
}

namespace kairo::backend {

GbaSystem::GbaSystem() {
    bus_.set_cart(&cart_);
    bus_.set_io(&io_);
    cpu_.set_io(&io_);
    patch_bios_stubs();
    cpu_.reset(true);
}

bool GbaSystem::load_rom(const std::string& path) {
    if (!cart_.load_from_file(path)) return false;
    bus_.set_cart(&cart_);
    reset();
    return true;
}

void GbaSystem::reset() {
    io_.reset();
    bus_.reset();
    gpu_.reset();
    cpu_.set_io(&io_);
    patch_bios_stubs();
    cpu_.reset(true);
}

// Patch a minimal BIOS IRQ handler into BIOS memory. Games set their
// IRQ handler address at 0x03007FFC; the real BIOS at 0x18 dispatches
// to it. Without this stub, IRQ entry would execute zeros and hang.
//
// The stub at 0x18:
//   0x18: stmfd sp!, {r0-r3,r12,lr}  ; E92D500F
//   0x1C: mov   r0, #0x04000000      ; E3A00301
//   0x20: add   lr, pc, #0           ; E28FE000
//   0x24: ldr   pc, [r0, #-4]        ; E510F004  → jumps to [0x03FFFFFC]
//   0x28: ldmfd sp!, {r0-r3,r12,lr}  ; E8BD500F
//   0x2C: subs  pc, lr, #4           ; E25EF004

void GbaSystem::patch_bios_stubs() {
    const std::uint32_t irq_stub[] = {
        0xE92D500F, // stmfd sp!, {r0-r3,r12,lr}
        0xE3A00301, // mov r0, #0x04000000
        0xE28FE000, // add lr, pc, #0
        0xE510F004, // ldr pc, [r0, #-4]
        0xE8BD500F, // ldmfd sp!, {r0-r3,r12,lr}
        0xE25EF004, // subs pc, lr, #4
    };
    for (int i = 0; i < 6; ++i) {
        bus_.write_bios32(0x18 + static_cast<std::uint32_t>(i) * 4,
                          irq_stub[i]);
    }
}

void GbaSystem::check_irq() {
    if (!io_.ime()) return;
    std::uint16_t pending = io_.ie() & io_.if_pending();
    if (pending == 0) return;
    // CPSR.I must be clear for the CPU to accept an IRQ.
    if (cpu_.cpsr() & kCpsrI) return;

    (void)pending; // suppress unused warning when logging is disabled

    // Enter IRQ mode.
    std::uint32_t return_addr = cpu_.pc() - (cpu_.in_thumb() ? 0u : 4u);
    std::uint32_t old_cpsr = cpu_.cpsr();
    cpu_.set_cpsr((old_cpsr & ~(kCpsrModeMask | kCpsrT)) |
                  static_cast<std::uint32_t>(CpuMode::Irq) | kCpsrI);
    cpu_.set_spsr(old_cpsr);
    cpu_.reg(14) = return_addr;
    cpu_.flush_pipeline(0x00000018); // IRQ vector in BIOS
    io_.set_halted(false);
}

void GbaSystem::run_cpu(int cycles) {
    if (io_.halted()) {
        // CPU is halted (waiting for IRQ). Don't execute instructions
        // but still advance PPU timing.
        std::uint16_t irqs = io_.tick(cycles);
        if (irqs) io_.raise_irq(irqs);
        return;
    }

    int remaining = cycles;
    while (remaining > 0) {
        check_irq();
        int consumed = cpu_.step(bus_, 1);
        if (consumed <= 0) break;
        remaining -= consumed;
        std::uint16_t irqs = io_.tick(consumed);
        if (irqs) io_.raise_irq(irqs);
    }
}

int GbaSystem::run_frame() {
    int& frame_count = g_frame_count;

    for (int line = 0; line < kTotalLines; ++line) {
        run_cpu(kCyclesPerScanline);

        if (line < kVisibleLines) {
            gpu_.render_scanline(line, io_, bus_);
            io_.run_timed_dma(bus_, 2); // HBLANK DMA
        }

        if (line == kVisibleLines) {
            io_.run_timed_dma(bus_, 1); // VBLANK DMA
        }


        io_.run_immediate_dma(bus_);
    }

    if (frame_count == 300 || frame_count == 500 || frame_count == 800 ||
        frame_count == 1200) {
        std::uint16_t dc = io_.dispcnt();
        const auto& fb = gpu_.framebuffer();
        int non_black = 0;
        std::uint32_t first_color = 0;
        for (auto px : fb) {
            if (px != 0xFF000000 && px != 0) {
                ++non_black;
                if (!first_color) first_color = px;
            }
        }
        std::fprintf(stderr, "[frame %d] non-black pixels: %d/%d  first=0x%08X\n",
            frame_count, non_black, static_cast<int>(fb.size()), first_color);

        std::fprintf(stderr, "  DISPCNT=%04X  mode=%d  BG_en=%d%d%d%d  OBJ=%d  "
            "WIN0=%d WIN1=%d WOBJ=%d\n",
            dc, dc & 7,
            (dc >> 8) & 1, (dc >> 9) & 1, (dc >> 10) & 1, (dc >> 11) & 1,
            (dc >> 12) & 1, (dc >> 13) & 1, (dc >> 14) & 1, (dc >> 15) & 1);

        for (int bg = 0; bg < 4; ++bg) {
            std::uint16_t cnt = io_.raw16(io_reg::BG0CNT + bg * 2);
            std::uint16_t hofs = io_.raw16(io_reg::BG0HOFS + bg * 4) & 0x1FF;
            std::uint16_t vofs = io_.raw16(io_reg::BG0VOFS + bg * 4) & 0x1FF;
            std::fprintf(stderr, "  BG%dCNT=%04X prio=%d tile=%04X map=%05X "
                "256c=%d size=%d  HOFS=%d VOFS=%d\n",
                bg, cnt, cnt & 3,
                ((cnt >> 2) & 3) * 0x4000,
                ((cnt >> 8) & 0x1F) * 0x800,
                (cnt >> 7) & 1, (cnt >> 14) & 3, hofs, vofs);
        }

        std::uint16_t winin = io_.raw16(io_reg::WININ);
        std::uint16_t winout = io_.raw16(io_reg::WINOUT);
        std::uint16_t win0h = io_.raw16(io_reg::WIN0H);
        std::uint16_t win0v = io_.raw16(io_reg::WIN0V);
        std::uint16_t win1h = io_.raw16(io_reg::WIN1H);
        std::uint16_t win1v = io_.raw16(io_reg::WIN1V);
        std::fprintf(stderr, "  WIN0H=%04X(%d-%d) WIN0V=%04X(%d-%d)\n",
            win0h, win0h >> 8, win0h & 0xFF, win0v, win0v >> 8, win0v & 0xFF);
        std::fprintf(stderr, "  WIN1H=%04X(%d-%d) WIN1V=%04X(%d-%d)\n",
            win1h, win1h >> 8, win1h & 0xFF, win1v, win1v >> 8, win1v & 0xFF);
        std::fprintf(stderr, "  WININ=%04X (W0: %02X  W1: %02X)  WINOUT=%04X (%02X)\n",
            winin, winin & 0x3F, (winin >> 8) & 0x3F, winout, winout & 0x3F);

        // Palette dump: first 16 BG colors + first 16 OBJ colors
        std::fprintf(stderr, "  Palette BG[0..15]:");
        for (int i = 0; i < 16; ++i)
            std::fprintf(stderr, " %04X", bus_.read16(0x05000000 + i * 2));
        std::fprintf(stderr, "\n  Palette OBJ[0..15]:");
        for (int i = 0; i < 16; ++i)
            std::fprintf(stderr, " %04X", bus_.read16(0x05000200 + i * 2));
        std::fprintf(stderr, "\n");

        // VRAM sample: first 32 bytes at BG2 tile base and map base
        std::uint16_t bg2cnt = io_.raw16(io_reg::BG2CNT);
        std::uint32_t bg2_tile = ((bg2cnt >> 2) & 3) * 0x4000;
        std::uint32_t bg2_map  = ((bg2cnt >> 8) & 0x1F) * 0x800;
        std::fprintf(stderr, "  BG2 tile@%05X:", bg2_tile);
        for (int i = 0; i < 16; ++i)
            std::fprintf(stderr, " %04X", bus_.read16(0x06000000 + bg2_tile + i * 2));
        std::fprintf(stderr, "\n  BG2 map@%05X:", bg2_map);
        for (int i = 0; i < 16; ++i)
            std::fprintf(stderr, " %04X", bus_.read16(0x06000000 + bg2_map + i * 2));
        std::fprintf(stderr, "\n");

        // BG3 too
        std::uint16_t bg3cnt = io_.raw16(io_reg::BG3CNT);
        std::uint32_t bg3_tile = ((bg3cnt >> 2) & 3) * 0x4000;
        std::uint32_t bg3_map  = ((bg3cnt >> 8) & 0x1F) * 0x800;
        std::fprintf(stderr, "  BG3 tile@%05X:", bg3_tile);
        for (int i = 0; i < 16; ++i)
            std::fprintf(stderr, " %04X", bus_.read16(0x06000000 + bg3_tile + i * 2));
        std::fprintf(stderr, "\n  BG3 map@%05X:", bg3_map);
        for (int i = 0; i < 16; ++i)
            std::fprintf(stderr, " %04X", bus_.read16(0x06000000 + bg3_map + i * 2));
        std::fprintf(stderr, "\n");

        char path[128];
        std::snprintf(path, sizeof(path), "/tmp/gba_frame%d.ppm", frame_count);
        if (FILE* f = std::fopen(path, "wb")) {
            std::fprintf(f, "P6\n240 160\n255\n");
            for (auto px : fb) {
                std::uint8_t rgb[3] = {
                    static_cast<std::uint8_t>((px >> 16) & 0xFF),
                    static_cast<std::uint8_t>((px >> 8) & 0xFF),
                    static_cast<std::uint8_t>(px & 0xFF)
                };
                std::fwrite(rgb, 1, 3, f);
            }
            std::fclose(f);
            std::fprintf(stderr, "  wrote %s\n", path);
        }
    }
    ++frame_count;

    return cycles_per_frame;
}

} // namespace kairo::backend
