#include "backend/cpu.hpp"

#include "backend/memory.hpp"

namespace kairo::backend {

// Forward-declared in cpu_arm.cpp / cpu_thumb.cpp. Free functions in
// this TU rather than class methods so the dispatch tables can point
// at them directly without PMF overhead.
void execute_arm(Cpu& cpu, MemoryBus& bus, std::uint32_t instr);
void execute_thumb(Cpu& cpu, MemoryBus& bus, std::uint16_t instr);

int bank_index_for_mode(CpuMode mode) {
    switch (mode) {
        case CpuMode::User:       return 0;
        case CpuMode::System:     return 0;
        case CpuMode::Fiq:        return 1;
        case CpuMode::Supervisor: return 2;
        case CpuMode::Abort:      return 3;
        case CpuMode::Irq:        return 4;
        case CpuMode::Undefined:  return 5;
    }
    return 0;
}

Cpu::Cpu() { reset(true); }

void Cpu::reset(bool skip_bios) {
    regs_.fill(0);
    banked_r13_.fill(0);
    banked_r14_.fill(0);
    banked_r8_12_non_fiq_.fill(0);
    banked_r8_12_fiq_.fill(0);
    spsr_.fill(0);

    if (skip_bios) {
        // Post-BIOS state programmed by the real GBA BIOS before handing
        // control to the cart entry point. Values come from GBATEK
        // "BIOS RAM Usage" and verified test ROMs.
        //
        // We set up the three stacks the BIOS leaves behind, then drop
        // into System mode with IRQ/FIQ masked.
        banked_r13_[bank_index_for_mode(CpuMode::Supervisor)] = 0x03007FE0;
        banked_r13_[bank_index_for_mode(CpuMode::Irq)]        = 0x03007FA0;
        banked_r13_[bank_index_for_mode(CpuMode::User)]       = 0x03007F00;

        cpsr_ = static_cast<std::uint32_t>(CpuMode::System) | kCpsrI | kCpsrF;
        // System mode shares the User bank → r13 = 0x03007F00.
        regs_[13] = banked_r13_[0];
        flush_pipeline(0x08000000); // cart entry
    } else {
        // Cold-boot into Supervisor mode at the reset vector. This is
        // what you'd do to actually execute a BIOS. Irrelevant until
        // we have a BIOS replacement.
        cpsr_ = static_cast<std::uint32_t>(CpuMode::Supervisor) | kCpsrI | kCpsrF;
        flush_pipeline(0x00000000);
    }
    branched_ = false;
}

void Cpu::flush_pipeline(std::uint32_t target) {
    // ARM7TDMI has a 3-stage pipeline. By the time an instruction
    // executes, r15 already holds the address two slots ahead of it:
    // target + 8 in ARM, target + 4 in Thumb. Align to the instruction
    // boundary — real hardware masks PC LSBs on pipeline refill.
    if (in_thumb()) {
        regs_[15] = (target & ~1u) + 4;
    } else {
        regs_[15] = (target & ~3u) + 8;
    }
    branched_ = true;
}

void Cpu::set_cpsr(std::uint32_t value) {
    const auto new_mode = static_cast<CpuMode>(value & kCpsrModeMask);
    if (new_mode != mode()) {
        switch_mode(new_mode);
    }
    cpsr_ = value;
}

std::uint32_t Cpu::spsr() const {
    const int bank = bank_index_for_mode(mode());
    if (bank == 0) return cpsr_; // user/system have no SPSR
    return spsr_[bank];
}

void Cpu::set_spsr(std::uint32_t value) {
    const int bank = bank_index_for_mode(mode());
    if (bank == 0) return; // user/system: drop silently
    spsr_[bank] = value;
}

void Cpu::switch_mode(CpuMode new_mode) {
    const CpuMode old = mode();
    const int old_bank = bank_index_for_mode(old);
    const int new_bank = bank_index_for_mode(new_mode);

    // Stash current r13/r14 back to their bank.
    banked_r13_[old_bank] = regs_[13];
    banked_r14_[old_bank] = regs_[14];

    // Handle FIQ banking of r8..r12. Only matters when crossing the
    // FIQ boundary; otherwise the non-FIQ copies stay live.
    const bool old_is_fiq = (old == CpuMode::Fiq);
    const bool new_is_fiq = (new_mode == CpuMode::Fiq);
    if (old_is_fiq != new_is_fiq) {
        if (old_is_fiq) {
            // Leaving FIQ: save FIQ bank, restore non-FIQ.
            for (int i = 0; i < 5; ++i) {
                banked_r8_12_fiq_[i] = regs_[8 + i];
                regs_[8 + i] = banked_r8_12_non_fiq_[i];
            }
        } else {
            // Entering FIQ: save non-FIQ bank, restore FIQ copies.
            for (int i = 0; i < 5; ++i) {
                banked_r8_12_non_fiq_[i] = regs_[8 + i];
                regs_[8 + i] = banked_r8_12_fiq_[i];
            }
        }
    }

    // Load the new mode's r13/r14.
    regs_[13] = banked_r13_[new_bank];
    regs_[14] = banked_r14_[new_bank];

    // CPSR mode bits are updated by the caller (set_cpsr).
}

bool Cpu::check_condition(std::uint8_t cond) const {
    return evaluate_condition(cond, cpsr_);
}

bool evaluate_condition(std::uint8_t cond, std::uint32_t cpsr) {
    const bool n = (cpsr & kCpsrN) != 0;
    const bool z = (cpsr & kCpsrZ) != 0;
    const bool c = (cpsr & kCpsrC) != 0;
    const bool v = (cpsr & kCpsrV) != 0;
    switch (cond & 0xF) {
        case kCondEQ: return z;
        case kCondNE: return !z;
        case kCondCS: return c;
        case kCondCC: return !c;
        case kCondMI: return n;
        case kCondPL: return !n;
        case kCondVS: return v;
        case kCondVC: return !v;
        case kCondHI: return c && !z;
        case kCondLS: return !c || z;
        case kCondGE: return n == v;
        case kCondLT: return n != v;
        case kCondGT: return !z && (n == v);
        case kCondLE: return z || (n != v);
        case kCondAL: return true;
        case kCondNV: return false; // reserved on ARMv4
    }
    return false;
}

std::uint32_t barrel_shift(std::uint32_t value,
                           std::uint8_t shift_type,
                           std::uint32_t amount,
                           bool is_register_shift,
                           bool& carry_out) {
    // `carry_out` comes in holding the current C flag. It is updated
    // only for shift amounts that affect the shifter carry-out.
    //
    // shift_type: 0=LSL, 1=LSR, 2=ASR, 3=ROR (with RRX special case)
    switch (shift_type & 0x3) {
        case 0: { // LSL
            if (amount == 0) {
                // LSL #0 leaves value and carry untouched.
                return value;
            }
            if (amount < 32) {
                carry_out = ((value >> (32 - amount)) & 1u) != 0;
                return value << amount;
            }
            if (amount == 32) {
                carry_out = (value & 1u) != 0;
                return 0;
            }
            // > 32
            carry_out = false;
            return 0;
        }
        case 1: { // LSR
            if (amount == 0) {
                if (is_register_shift) return value; // LSR by Rs==0: nothing
                // LSR #0 is encoded as LSR #32.
                carry_out = (value & 0x80000000u) != 0;
                return 0;
            }
            if (amount < 32) {
                carry_out = ((value >> (amount - 1)) & 1u) != 0;
                return value >> amount;
            }
            if (amount == 32) {
                carry_out = (value & 0x80000000u) != 0;
                return 0;
            }
            carry_out = false;
            return 0;
        }
        case 2: { // ASR
            if (amount == 0) {
                if (is_register_shift) return value;
                // ASR #0 is encoded as ASR #32: fill with sign bit.
                const bool sign = (value & 0x80000000u) != 0;
                carry_out = sign;
                return sign ? 0xFFFFFFFFu : 0u;
            }
            if (amount < 32) {
                const auto signed_val = static_cast<std::int32_t>(value);
                carry_out = ((value >> (amount - 1)) & 1u) != 0;
                return static_cast<std::uint32_t>(signed_val >> amount);
            }
            // amount >= 32: replicate sign bit across the whole word.
            const bool sign = (value & 0x80000000u) != 0;
            carry_out = sign;
            return sign ? 0xFFFFFFFFu : 0u;
        }
        case 3: { // ROR / RRX
            if (amount == 0) {
                if (is_register_shift) return value;
                // ROR #0 decodes to RRX: 33-bit rotate through carry.
                const std::uint32_t old_c = carry_out ? 1u : 0u;
                carry_out = (value & 1u) != 0;
                return (old_c << 31) | (value >> 1);
            }
            const std::uint32_t r = amount & 31u;
            if (r == 0) {
                // Rotate by a multiple of 32 — value unchanged, carry
                // is the top bit.
                carry_out = (value & 0x80000000u) != 0;
                return value;
            }
            carry_out = ((value >> (r - 1)) & 1u) != 0;
            return (value >> r) | (value << (32 - r));
        }
    }
    return value;
}

int Cpu::step(MemoryBus& bus, int max_cycles) {
    int consumed = 0;
    while (consumed < max_cycles) {
        branched_ = false;
        if (in_thumb()) {
            // In Thumb, r15 = execute_pc + 4, so the instruction to
            // run this tick is at r15 - 4.
            const std::uint32_t exec_pc = regs_[15] - 4;
            const std::uint16_t instr = bus.read16(exec_pc);
            execute_thumb(*this, bus, instr);
            if (!branched_) regs_[15] += 2;
        } else {
            const std::uint32_t exec_pc = regs_[15] - 8;
            const std::uint32_t instr = bus.read32(exec_pc);
            execute_arm(*this, bus, instr);
            if (!branched_) regs_[15] += 4;
        }
        // Flat-rate cycle accounting for now — every instruction gets
        // charged 1 cycle. Will be refined once the PPU forces the
        // issue (timing-sensitive effects, DMA stalls, etc.).
        consumed += 1;
    }
    return consumed;
}

} // namespace kairo::backend
