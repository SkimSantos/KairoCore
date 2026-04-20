#include "backend/cpu.hpp"
#include "backend/hle_bios.hpp"
#include "backend/io.hpp"
#include "backend/memory.hpp"

namespace kairo::backend {

using ArmHandler = void (*)(Cpu&, MemoryBus&, std::uint32_t);

namespace {

inline int rd(std::uint32_t i) { return (i >> 12) & 0xF; }
inline int rn(std::uint32_t i) { return (i >> 16) & 0xF; }
inline int rm(std::uint32_t i) { return i & 0xF; }
inline int rs(std::uint32_t i) { return (i >> 8) & 0xF; }
inline bool s_bit(std::uint32_t i) { return (i >> 20) & 1; }
inline int dp_opcode(std::uint32_t i) { return (i >> 21) & 0xF; }

// ── Helpers ──────────────────────────────────────────────────────────

struct AddResult {
    std::uint32_t val;
    bool carry;
    bool overflow;
};

AddResult add_with_carry(std::uint32_t a, std::uint32_t b, bool carry_in) {
    std::uint64_t sum =
        static_cast<std::uint64_t>(a) + b + (carry_in ? 1u : 0u);
    auto result = static_cast<std::uint32_t>(sum);
    bool c = sum > 0xFFFFFFFFull;
    bool v = ((a ^ result) & (b ^ result) & 0x80000000u) != 0;
    return {result, c, v};
}

std::uint32_t dp_operand2_reg(Cpu& cpu, std::uint32_t instr, bool& carry) {
    std::uint32_t val = cpu.reg(rm(instr));
    if (rm(instr) == 15) val = cpu.pc();

    auto shift_type = static_cast<std::uint8_t>((instr >> 5) & 3);
    std::uint32_t amount;
    bool is_reg_shift;

    if ((instr >> 4) & 1) {
        amount = cpu.reg(rs(instr)) & 0xFF;
        is_reg_shift = true;
    } else {
        amount = (instr >> 7) & 0x1F;
        is_reg_shift = false;
    }
    return barrel_shift(val, shift_type, amount, is_reg_shift, carry);
}

std::uint32_t dp_operand2_imm(std::uint32_t instr, bool& carry) {
    std::uint32_t imm = instr & 0xFF;
    std::uint32_t rotate = ((instr >> 8) & 0xF) * 2;
    if (rotate == 0) return imm;
    std::uint32_t result = (imm >> rotate) | (imm << (32 - rotate));
    carry = (result >> 31) != 0;
    return result;
}

// ── Undefined ────────────────────────────────────────────────────────

void arm_undefined(Cpu& /*cpu*/, MemoryBus& /*bus*/, std::uint32_t /*i*/) {}

// ── Data Processing ──────────────────────────────────────────────────

void arm_data_processing(Cpu& cpu, MemoryBus& /*bus*/, std::uint32_t instr) {
    bool carry = cpu.flag_c();
    bool imm_form = (instr >> 25) & 1;

    std::uint32_t op2 = imm_form ? dp_operand2_imm(instr, carry)
                                 : dp_operand2_reg(cpu, instr, carry);

    std::uint32_t rn_val = cpu.reg(rn(instr));
    if (rn(instr) == 15) rn_val = cpu.pc();

    int op = dp_opcode(instr);
    bool set_flags = s_bit(instr);
    int dest = rd(instr);

    std::uint32_t result = 0;
    bool write_result = true;
    bool carry_flag = carry;
    bool overflow_flag = cpu.flag_v();

    switch (op) {
        case 0x0: result = rn_val & op2; break;
        case 0x1: result = rn_val ^ op2; break;
        case 0x2: {
            auto r = add_with_carry(rn_val, ~op2, true);
            result = r.val; carry_flag = r.carry; overflow_flag = r.overflow;
            break;
        }
        case 0x3: {
            auto r = add_with_carry(op2, ~rn_val, true);
            result = r.val; carry_flag = r.carry; overflow_flag = r.overflow;
            break;
        }
        case 0x4: {
            auto r = add_with_carry(rn_val, op2, false);
            result = r.val; carry_flag = r.carry; overflow_flag = r.overflow;
            break;
        }
        case 0x5: {
            auto r = add_with_carry(rn_val, op2, cpu.flag_c());
            result = r.val; carry_flag = r.carry; overflow_flag = r.overflow;
            break;
        }
        case 0x6: {
            auto r = add_with_carry(rn_val, ~op2, cpu.flag_c());
            result = r.val; carry_flag = r.carry; overflow_flag = r.overflow;
            break;
        }
        case 0x7: {
            auto r = add_with_carry(op2, ~rn_val, cpu.flag_c());
            result = r.val; carry_flag = r.carry; overflow_flag = r.overflow;
            break;
        }
        case 0x8: result = rn_val & op2; write_result = false; break;
        case 0x9: result = rn_val ^ op2; write_result = false; break;
        case 0xA: {
            auto r = add_with_carry(rn_val, ~op2, true);
            result = r.val; carry_flag = r.carry; overflow_flag = r.overflow;
            write_result = false; break;
        }
        case 0xB: {
            auto r = add_with_carry(rn_val, op2, false);
            result = r.val; carry_flag = r.carry; overflow_flag = r.overflow;
            write_result = false; break;
        }
        case 0xC: result = rn_val | op2; break;
        case 0xD: result = op2; break;
        case 0xE: result = rn_val & ~op2; break;
        case 0xF: result = ~op2; break;
    }

    if (set_flags && dest == 15) {
        cpu.set_cpsr(cpu.spsr());
    }

    if (write_result) {
        if (dest == 15) {
            cpu.flush_pipeline(result);
        } else {
            cpu.reg(dest) = result;
        }
    }

    if (set_flags && dest != 15) {
        cpu.set_nz_from(result);
        cpu.set_flag(kCpsrC, carry_flag);
        cpu.set_flag(kCpsrV, overflow_flag);
    }
}

// ── Branch / BX ──────────────────────────────────────────────────────

void arm_branch(Cpu& cpu, MemoryBus& /*bus*/, std::uint32_t instr) {
    bool link = (instr >> 24) & 1;
    auto offset =
        static_cast<std::uint32_t>(static_cast<std::int32_t>(instr << 8) >> 6);
    std::uint32_t target = cpu.pc() + offset;
    if (link) cpu.reg(14) = cpu.pc() - 4;
    cpu.flush_pipeline(target);
}

void arm_bx(Cpu& cpu, MemoryBus& /*bus*/, std::uint32_t instr) {
    std::uint32_t addr = cpu.reg(rm(instr));
    if (addr & 1) {
        cpu.set_cpsr(cpu.cpsr() | kCpsrT);
        cpu.flush_pipeline(addr & ~1u);
    } else {
        cpu.set_cpsr(cpu.cpsr() & ~kCpsrT);
        cpu.flush_pipeline(addr & ~3u);
    }
}

// ── MRS / MSR ────────────────────────────────────────────────────────

void arm_mrs(Cpu& cpu, MemoryBus& /*bus*/, std::uint32_t instr) {
    bool use_spsr = (instr >> 22) & 1;
    cpu.reg(rd(instr)) = use_spsr ? cpu.spsr() : cpu.cpsr();
}

void arm_msr_reg(Cpu& cpu, MemoryBus& /*bus*/, std::uint32_t instr) {
    bool use_spsr = (instr >> 22) & 1;
    std::uint32_t mask = 0;
    if (instr & (1 << 19)) mask |= 0xFF000000u;
    if (instr & (1 << 18)) mask |= 0x00FF0000u;
    if (instr & (1 << 17)) mask |= 0x0000FF00u;
    if (instr & (1 << 16)) mask |= 0x000000FFu;
    std::uint32_t val = cpu.reg(rm(instr)) & mask;
    if (use_spsr)
        cpu.set_spsr((cpu.spsr() & ~mask) | val);
    else
        cpu.set_cpsr((cpu.cpsr() & ~mask) | val);
}

void arm_msr_imm(Cpu& cpu, MemoryBus& /*bus*/, std::uint32_t instr) {
    bool use_spsr = (instr >> 22) & 1;
    std::uint32_t imm = instr & 0xFF;
    std::uint32_t rotate = ((instr >> 8) & 0xF) * 2;
    if (rotate > 0) imm = (imm >> rotate) | (imm << (32 - rotate));
    std::uint32_t mask = 0;
    if (instr & (1 << 19)) mask |= 0xFF000000u;
    if (instr & (1 << 18)) mask |= 0x00FF0000u;
    if (instr & (1 << 17)) mask |= 0x0000FF00u;
    if (instr & (1 << 16)) mask |= 0x000000FFu;
    std::uint32_t val = imm & mask;
    if (use_spsr)
        cpu.set_spsr((cpu.spsr() & ~mask) | val);
    else
        cpu.set_cpsr((cpu.cpsr() & ~mask) | val);
}

// ── Multiply ─────────────────────────────────────────────────────────

void arm_mul_mla(Cpu& cpu, MemoryBus& /*bus*/, std::uint32_t instr) {
    int dest = (instr >> 16) & 0xF;
    int rn_idx = (instr >> 12) & 0xF;
    int rs_idx = rs(instr);
    int rm_idx = rm(instr);
    bool accumulate = (instr >> 21) & 1;
    std::uint32_t result = cpu.reg(rm_idx) * cpu.reg(rs_idx);
    if (accumulate) result += cpu.reg(rn_idx);
    cpu.reg(dest) = result;
    if (s_bit(instr)) cpu.set_nz_from(result);
}

void arm_mull_mlal(Cpu& cpu, MemoryBus& /*bus*/, std::uint32_t instr) {
    int rd_hi = (instr >> 16) & 0xF;
    int rd_lo = (instr >> 12) & 0xF;
    int rs_idx = rs(instr);
    int rm_idx = rm(instr);
    bool accumulate = (instr >> 21) & 1;
    bool is_signed = (instr >> 22) & 1;

    std::uint64_t result;
    if (is_signed) {
        auto a = static_cast<std::int64_t>(static_cast<std::int32_t>(cpu.reg(rm_idx)));
        auto b = static_cast<std::int64_t>(static_cast<std::int32_t>(cpu.reg(rs_idx)));
        result = static_cast<std::uint64_t>(a * b);
    } else {
        result = static_cast<std::uint64_t>(cpu.reg(rm_idx)) *
                 static_cast<std::uint64_t>(cpu.reg(rs_idx));
    }
    if (accumulate) {
        result += (static_cast<std::uint64_t>(cpu.reg(rd_hi)) << 32) |
                  cpu.reg(rd_lo);
    }
    cpu.reg(rd_lo) = static_cast<std::uint32_t>(result);
    cpu.reg(rd_hi) = static_cast<std::uint32_t>(result >> 32);
    if (s_bit(instr)) {
        cpu.set_flag(kCpsrN, (result >> 63) != 0);
        cpu.set_flag(kCpsrZ, result == 0);
    }
}

// ── SWP / SWPB ───────────────────────────────────────────────────────

void arm_swp(Cpu& cpu, MemoryBus& bus, std::uint32_t instr) {
    bool byte = (instr >> 22) & 1;
    std::uint32_t addr = cpu.reg(rn(instr));
    int rd_idx = rd(instr);
    int rm_idx = rm(instr);
    if (byte) {
        std::uint8_t old = bus.read8(addr);
        bus.write8(addr, static_cast<std::uint8_t>(cpu.reg(rm_idx)));
        cpu.reg(rd_idx) = old;
    } else {
        std::uint32_t old = bus.read32(addr & ~3u);
        int rot = static_cast<int>(addr & 3) * 8;
        if (rot) old = (old >> rot) | (old << (32 - rot));
        bus.write32(addr & ~3u, cpu.reg(rm_idx));
        cpu.reg(rd_idx) = old;
    }
}

// ── Single Data Transfer (LDR / STR) ────────────────────────────────

void arm_single_transfer(Cpu& cpu, MemoryBus& bus, std::uint32_t instr) {
    bool reg_offset = (instr >> 25) & 1;
    bool pre = (instr >> 24) & 1;
    bool up = (instr >> 23) & 1;
    bool byte = (instr >> 22) & 1;
    bool writeback = (instr >> 21) & 1;
    bool load = (instr >> 20) & 1;
    int rn_idx = rn(instr);
    int rd_idx = rd(instr);

    std::uint32_t base = cpu.reg(rn_idx);
    if (rn_idx == 15) base = cpu.pc();

    std::uint32_t offset;
    if (!reg_offset) {
        offset = instr & 0xFFF;
    } else {
        bool carry = cpu.flag_c();
        offset = barrel_shift(cpu.reg(instr & 0xF),
                              static_cast<std::uint8_t>((instr >> 5) & 3),
                              (instr >> 7) & 0x1F, false, carry);
    }

    std::uint32_t addr = pre ? (up ? base + offset : base - offset) : base;

    if (load) {
        if (byte) {
            cpu.reg(rd_idx) = bus.read8(addr);
        } else {
            std::uint32_t val = bus.read32(addr & ~3u);
            int rot = static_cast<int>(addr & 3) * 8;
            if (rot) val = (val >> rot) | (val << (32 - rot));
            if (rd_idx == 15)
                cpu.flush_pipeline(val);
            else
                cpu.reg(rd_idx) = val;
        }
    } else {
        std::uint32_t val = cpu.reg(rd_idx);
        if (rd_idx == 15) val = cpu.pc() + 4;
        if (byte)
            bus.write8(addr, static_cast<std::uint8_t>(val));
        else
            bus.write32(addr, val);
    }

    if (!pre) {
        cpu.reg(rn_idx) = up ? base + offset : base - offset;
    } else if (writeback) {
        cpu.reg(rn_idx) = addr;
    }
}

// ── Halfword / Signed Transfer ───────────────────────────────────────

void arm_halfword_transfer(Cpu& cpu, MemoryBus& bus, std::uint32_t instr) {
    bool pre = (instr >> 24) & 1;
    bool up = (instr >> 23) & 1;
    bool imm_offset = (instr >> 22) & 1;
    bool writeback = (instr >> 21) & 1;
    bool load = (instr >> 20) & 1;
    int rn_idx = rn(instr);
    int rd_idx = rd(instr);
    int sh = (instr >> 5) & 3;

    std::uint32_t base = cpu.reg(rn_idx);
    if (rn_idx == 15) base = cpu.pc();

    std::uint32_t offset = imm_offset
        ? (((instr >> 4) & 0xF0) | (instr & 0xF))
        : cpu.reg(rm(instr));

    std::uint32_t addr = pre ? (up ? base + offset : base - offset) : base;

    if (load) {
        std::uint32_t val = 0;
        switch (sh) {
            case 1: val = bus.read16(addr & ~1u); break;
            case 2:
                val = static_cast<std::uint32_t>(
                    static_cast<std::int8_t>(bus.read8(addr)));
                break;
            case 3:
                val = static_cast<std::uint32_t>(
                    static_cast<std::int16_t>(bus.read16(addr & ~1u)));
                break;
            default: break;
        }
        if (rd_idx == 15)
            cpu.flush_pipeline(val);
        else
            cpu.reg(rd_idx) = val;
    } else {
        std::uint32_t val = cpu.reg(rd_idx);
        if (rd_idx == 15) val = cpu.pc() + 4;
        bus.write16(addr & ~1u, static_cast<std::uint16_t>(val));
    }

    if (!pre)
        cpu.reg(rn_idx) = up ? base + offset : base - offset;
    else if (writeback)
        cpu.reg(rn_idx) = addr;
}

// ── Block Data Transfer (LDM / STM) ─────────────────────────────────

void arm_block_transfer(Cpu& cpu, MemoryBus& bus, std::uint32_t instr) {
    bool pre = (instr >> 24) & 1;
    bool up = (instr >> 23) & 1;
    bool psr_or_force = (instr >> 22) & 1;
    bool writeback = (instr >> 21) & 1;
    bool load = (instr >> 20) & 1;
    int rn_idx = rn(instr);
    auto reg_list = static_cast<std::uint16_t>(instr & 0xFFFF);

    if (reg_list == 0) return;

    int count = 0;
    for (int i = 0; i < 16; ++i)
        if (reg_list & (1 << i)) ++count;

    std::uint32_t base = cpu.reg(rn_idx);
    std::uint32_t start;
    if (up)
        start = pre ? base + 4 : base;
    else
        start = pre ? base - static_cast<std::uint32_t>(count) * 4
                    : base - static_cast<std::uint32_t>(count) * 4 + 4;

    std::uint32_t addr = start;
    for (int i = 0; i < 16; ++i) {
        if (!(reg_list & (1 << i))) continue;
        if (load) {
            std::uint32_t val = bus.read32(addr);
            if (i == 15) {
                if (psr_or_force) cpu.set_cpsr(cpu.spsr());
                cpu.flush_pipeline(val);
            } else {
                cpu.reg(i) = val;
            }
        } else {
            std::uint32_t val = cpu.reg(i);
            if (i == 15) val = cpu.pc() + 4;
            bus.write32(addr, val);
        }
        addr += 4;
    }

    bool rn_in_list = (reg_list >> rn_idx) & 1;
    if (writeback && !(load && rn_in_list))
        cpu.reg(rn_idx) = up
            ? base + static_cast<std::uint32_t>(count) * 4
            : base - static_cast<std::uint32_t>(count) * 4;
}

// ── SWI ──────────────────────────────────────────────────────────────
// GBA games invoke BIOS services via SWI. Without a HLE BIOS the
// handler will execute whatever is at 0x08 in BIOS ROM (zeros → hangs).
// HLE BIOS is a follow-up step.

void arm_swi(Cpu& cpu, MemoryBus& bus, std::uint32_t instr) {
    std::uint8_t comment = static_cast<std::uint8_t>((instr >> 16) & 0xFF);
    Io* io = cpu.get_io();
    if (io && hle_bios_call(comment, cpu, bus, *io)) return;

    std::uint32_t return_addr = cpu.pc() - 4;
    std::uint32_t old_cpsr = cpu.cpsr();
    cpu.set_cpsr((old_cpsr & ~(kCpsrModeMask | kCpsrT)) |
                 static_cast<std::uint32_t>(CpuMode::Supervisor) | kCpsrI);
    cpu.set_spsr(old_cpsr);
    cpu.reg(14) = return_addr;
    cpu.flush_pipeline(0x00000008);
}

// ── Dispatch table ───────────────────────────────────────────────────
// 12-bit key = (bits 27-20) << 4 | (bits 7-4)

ArmHandler classify(std::uint32_t key) {
    std::uint32_t hi = (key >> 4) & 0xFF;
    std::uint32_t lo = key & 0xF;
    std::uint32_t cat = hi >> 5;

    switch (cat) {
        case 0b000: {
            if ((hi & 0xFC) == 0x00 && lo == 0x9) return arm_mul_mla;
            if ((hi & 0xF8) == 0x08 && lo == 0x9) return arm_mull_mlal;
            if ((hi & 0xFB) == 0x10 && lo == 0x9) return arm_swp;
            if (hi == 0x12 && lo == 0x1) return arm_bx;
            if ((lo & 0x9) == 0x9 && (lo & 0x6) != 0)
                return arm_halfword_transfer;
            if ((hi == 0x10 || hi == 0x14) && lo == 0x0) return arm_mrs;
            if ((hi == 0x12 || hi == 0x16) && lo == 0x0) return arm_msr_reg;
            return arm_data_processing;
        }
        case 0b001: {
            if ((hi & 0xFB) == 0x32) return arm_msr_imm;
            return arm_data_processing;
        }
        case 0b010:
            return arm_single_transfer;
        case 0b011:
            if (lo & 1) return arm_undefined;
            return arm_single_transfer;
        case 0b100:
            return arm_block_transfer;
        case 0b101:
            return arm_branch;
        case 0b110:
            return arm_undefined;
        case 0b111:
            if (hi & 0x10) return arm_swi;
            return arm_undefined;
    }
    return arm_undefined;
}

struct ArmDispatchTable {
    ArmHandler handlers[4096];
    ArmDispatchTable() {
        for (std::uint32_t i = 0; i < 4096; ++i)
            handlers[i] = classify(i);
    }
};

const ArmDispatchTable& arm_table() {
    static const ArmDispatchTable table;
    return table;
}

} // anonymous namespace

void execute_arm(Cpu& cpu, MemoryBus& bus, std::uint32_t instr) {
    auto cond = static_cast<std::uint8_t>((instr >> 28) & 0xF);
    if (!cpu.check_condition(cond)) return;
    std::uint32_t key = ((instr >> 20) & 0xFF) << 4 | ((instr >> 4) & 0xF);
    arm_table().handlers[key](cpu, bus, instr);
}

} // namespace kairo::backend
