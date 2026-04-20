#include "backend/gba_system.hpp"

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

    return cycles_per_frame;
}

} // namespace kairo::backend
