#pragma once

#include <array>
#include <cstdint>

namespace kairo::backend {

class Io;
class MemoryBus;

// ARM7TDMI CPU mode. The low 5 bits of CPSR encode this.
enum class CpuMode : std::uint8_t {
    User       = 0x10,
    Fiq        = 0x11,
    Irq        = 0x12,
    Supervisor = 0x13,
    Abort      = 0x17,
    Undefined  = 0x1B,
    System     = 0x1F,
};

// CPSR flag bit positions.
inline constexpr std::uint32_t kCpsrN = 1u << 31; // negative
inline constexpr std::uint32_t kCpsrZ = 1u << 30; // zero
inline constexpr std::uint32_t kCpsrC = 1u << 29; // carry
inline constexpr std::uint32_t kCpsrV = 1u << 28; // overflow
inline constexpr std::uint32_t kCpsrI = 1u << 7;  // IRQ disable
inline constexpr std::uint32_t kCpsrF = 1u << 6;  // FIQ disable
inline constexpr std::uint32_t kCpsrT = 1u << 5;  // Thumb state
inline constexpr std::uint32_t kCpsrModeMask = 0x1F;

// ARM condition codes (top 4 bits of every ARM instruction).
enum ArmCondition : std::uint8_t {
    kCondEQ = 0x0, kCondNE = 0x1,
    kCondCS = 0x2, kCondCC = 0x3,
    kCondMI = 0x4, kCondPL = 0x5,
    kCondVS = 0x6, kCondVC = 0x7,
    kCondHI = 0x8, kCondLS = 0x9,
    kCondGE = 0xA, kCondLT = 0xB,
    kCondGT = 0xC, kCondLE = 0xD,
    kCondAL = 0xE, kCondNV = 0xF,
};

// ARM7TDMI register file with mode banking. r0..r7 are never banked.
// r8..r12 are banked only in FIQ mode. r13/r14 are banked per
// privileged mode. r15 (PC) is shared. Each privileged mode has its
// own SPSR.
class Cpu {
public:
    Cpu();

    // Reset to the power-on state. When `skip_bios` is true the CPU
    // starts in the post-BIOS state games expect: supervisor stack set,
    // PC = 0x08000000 (cart entry), mode = System, IRQ+FIQ disabled.
    // When false, PC = 0 and we'll run the BIOS reset vector once that
    // exists.
    void reset(bool skip_bios);

    // Access to the *active* (current-mode) copy of a register.
    std::uint32_t& reg(int index) { return regs_[index]; }
    std::uint32_t  reg(int index) const { return regs_[index]; }

    std::uint32_t cpsr() const { return cpsr_; }
    // Full CPSR write — re-banks registers if the mode bits changed.
    void set_cpsr(std::uint32_t value);

    // SPSR of the current mode. User/System have no SPSR; writes there
    // are dropped and reads return the CPSR (matches hardware).
    std::uint32_t spsr() const;
    void set_spsr(std::uint32_t value);

    CpuMode mode() const {
        return static_cast<CpuMode>(cpsr_ & kCpsrModeMask);
    }
    bool in_thumb() const { return (cpsr_ & kCpsrT) != 0; }

    // Flag accessors. Hot path — keep them trivial.
    bool flag_n() const { return (cpsr_ & kCpsrN) != 0; }
    bool flag_z() const { return (cpsr_ & kCpsrZ) != 0; }
    bool flag_c() const { return (cpsr_ & kCpsrC) != 0; }
    bool flag_v() const { return (cpsr_ & kCpsrV) != 0; }
    void set_flag(std::uint32_t bit, bool value) {
        cpsr_ = value ? (cpsr_ | bit) : (cpsr_ & ~bit);
    }
    void set_nz_from(std::uint32_t result) {
        set_flag(kCpsrN, (result & 0x80000000u) != 0);
        set_flag(kCpsrZ, result == 0);
    }

    // Evaluate an ARM condition code against the current flags.
    bool check_condition(std::uint8_t cond) const;

    // PC as seen *by* executing code — already r15 in our model,
    // because we keep r15 at execute_pc + pipeline_offset.
    std::uint32_t pc() const { return regs_[15]; }

    // Pipeline offset applied after a branch / mode switch. 8 bytes in
    // ARM mode (one fetched + one decoded), 4 in Thumb.
    std::uint32_t pipeline_offset() const { return in_thumb() ? 4u : 8u; }

    // Branch to `target` and refill the pipeline. After this call, r15
    // = target + pipeline_offset. The step loop sees `branched_` set
    // and skips its normal PC advance.
    void flush_pipeline(std::uint32_t target);
    bool branched() const { return branched_; }
    void clear_branched() { branched_ = false; }

    // Drive the CPU for up to `max_cycles` cycles. Returns the number
    // actually consumed (may slightly exceed the budget because we
    // always finish the current instruction).
    int step(MemoryBus& bus, int max_cycles);

    // Attach the I/O controller so SWI can dispatch HLE BIOS calls.
    void set_io(Io* p) { io_ = p; }
    Io* get_io() const { return io_; }

private:
    void switch_mode(CpuMode new_mode);

    // Active register file. r0..r14 are rewritten when switching modes;
    // r15 is always shared.
    std::array<std::uint32_t, 16> regs_{};

    // Banked storage for r13/r14 per privileged mode.
    // [0]=user/system, [1]=fiq, [2]=svc, [3]=abt, [4]=irq, [5]=und
    std::array<std::uint32_t, 6> banked_r13_{};
    std::array<std::uint32_t, 6> banked_r14_{};

    // FIQ banks r8..r12 additionally. [0]=non-fiq copy, [1]=fiq copy.
    std::array<std::uint32_t, 5> banked_r8_12_non_fiq_{};
    std::array<std::uint32_t, 5> banked_r8_12_fiq_{};

    // SPSR per privileged mode (user/system share no SPSR).
    std::array<std::uint32_t, 6> spsr_{};

    std::uint32_t cpsr_ = 0;

    // Set by flush_pipeline(); consumed by step() to suppress the
    // normal r15 += 4/2 advance after a branch.
    bool branched_ = false;

    Io* io_ = nullptr;
};

// Maps a CpuMode to a bank index (0..5). User and System share bank 0.
int bank_index_for_mode(CpuMode mode);

// Barrel shifter for ARM operand2. Returns the shifted value and
// writes the shifter carry-out to `carry_out` (which must already hold
// the current C flag — some shift-by-0 cases pass it through).
// `is_register_shift` distinguishes "shift-by-immediate" from
// "shift-by-register" semantics (they differ for LSR/ASR/ROR #0).
std::uint32_t barrel_shift(std::uint32_t value,
                           std::uint8_t shift_type,
                           std::uint32_t shift_amount,
                           bool is_register_shift,
                           bool& carry_out);

// Evaluate the 4-bit ARM condition field against packed CPSR flags.
bool evaluate_condition(std::uint8_t cond, std::uint32_t cpsr);

} // namespace kairo::backend
