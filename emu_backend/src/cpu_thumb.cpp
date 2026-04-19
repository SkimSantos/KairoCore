#include "backend/cpu.hpp"
#include "backend/hle_bios.hpp"
#include "backend/io.hpp"
#include "backend/memory.hpp"

namespace kairo::backend {

using ThumbHandler = void (*)(Cpu&, MemoryBus&, std::uint16_t);

namespace {

inline int trd(std::uint16_t i) { return i & 0x7; }
inline int trs(std::uint16_t i) { return (i >> 3) & 0x7; }
inline int trn(std::uint16_t i) { return (i >> 6) & 0x7; }

// ── Helpers ──────────────────────────────────────────────────────────

struct AddResult {
    std::uint32_t val;
    bool carry;
    bool overflow;
};

AddResult add_with_carry(std::uint32_t a, std::uint32_t b, bool c_in) {
    std::uint64_t sum = static_cast<std::uint64_t>(a) + b + (c_in ? 1u : 0u);
    auto r = static_cast<std::uint32_t>(sum);
    return {r, sum > 0xFFFFFFFFull,
            ((a ^ r) & (b ^ r) & 0x80000000u) != 0};
}

// ── Format 1: Move shifted register ──────────────────────────────────

void thumb_shift_imm(Cpu& cpu, MemoryBus& /*bus*/, std::uint16_t instr) {
    int op = (instr >> 11) & 3;
    std::uint32_t amount = (instr >> 6) & 0x1F;
    std::uint32_t val = cpu.reg(trs(instr));
    bool carry = cpu.flag_c();
    auto result =
        barrel_shift(val, static_cast<std::uint8_t>(op), amount, false, carry);
    cpu.reg(trd(instr)) = result;
    cpu.set_nz_from(result);
    cpu.set_flag(kCpsrC, carry);
}

// ── Format 2: Add / subtract ─────────────────────────────────────────

void thumb_add_sub(Cpu& cpu, MemoryBus& /*bus*/, std::uint16_t instr) {
    bool imm = (instr >> 10) & 1;
    bool sub = (instr >> 9) & 1;
    std::uint32_t src = cpu.reg(trs(instr));
    std::uint32_t operand = imm ? static_cast<std::uint32_t>((instr >> 6) & 7)
                                : cpu.reg(trn(instr));
    AddResult r = sub ? add_with_carry(src, ~operand, true)
                      : add_with_carry(src, operand, false);
    cpu.reg(trd(instr)) = r.val;
    cpu.set_nz_from(r.val);
    cpu.set_flag(kCpsrC, r.carry);
    cpu.set_flag(kCpsrV, r.overflow);
}

// ── Format 3: Mov/cmp/add/sub immediate ──────────────────────────────

void thumb_imm_op(Cpu& cpu, MemoryBus& /*bus*/, std::uint16_t instr) {
    int op = (instr >> 11) & 3;
    int dest = (instr >> 8) & 7;
    std::uint32_t imm = instr & 0xFF;
    std::uint32_t rd_val = cpu.reg(dest);
    AddResult r{};
    std::uint32_t result = 0;

    switch (op) {
        case 0: // MOV
            cpu.reg(dest) = imm;
            cpu.set_nz_from(imm);
            return;
        case 1: // CMP
            r = add_with_carry(rd_val, ~imm, true);
            cpu.set_nz_from(r.val);
            cpu.set_flag(kCpsrC, r.carry);
            cpu.set_flag(kCpsrV, r.overflow);
            return;
        case 2: // ADD
            r = add_with_carry(rd_val, imm, false);
            result = r.val;
            break;
        case 3: // SUB
            r = add_with_carry(rd_val, ~imm, true);
            result = r.val;
            break;
    }
    cpu.reg(dest) = result;
    cpu.set_nz_from(result);
    cpu.set_flag(kCpsrC, r.carry);
    cpu.set_flag(kCpsrV, r.overflow);
}

// ── Format 4: ALU operations ─────────────────────────────────────────

void thumb_alu(Cpu& cpu, MemoryBus& /*bus*/, std::uint16_t instr) {
    int op = (instr >> 6) & 0xF;
    int dest = trd(instr);
    std::uint32_t rd_val = cpu.reg(dest);
    std::uint32_t rs_val = cpu.reg(trs(instr));
    std::uint32_t result = 0;
    bool carry = cpu.flag_c();
    bool overflow = cpu.flag_v();

    switch (op) {
        case 0x0: result = rd_val & rs_val; break;  // AND
        case 0x1: result = rd_val ^ rs_val; break;  // EOR
        case 0x2: {                                  // LSL
            result = barrel_shift(rd_val, 0, rs_val & 0xFF, true, carry);
            break;
        }
        case 0x3: {                                  // LSR
            result = barrel_shift(rd_val, 1, rs_val & 0xFF, true, carry);
            break;
        }
        case 0x4: {                                  // ASR
            result = barrel_shift(rd_val, 2, rs_val & 0xFF, true, carry);
            break;
        }
        case 0x5: {                                  // ADC
            auto r = add_with_carry(rd_val, rs_val, cpu.flag_c());
            result = r.val; carry = r.carry; overflow = r.overflow;
            break;
        }
        case 0x6: {                                  // SBC
            auto r = add_with_carry(rd_val, ~rs_val, cpu.flag_c());
            result = r.val; carry = r.carry; overflow = r.overflow;
            break;
        }
        case 0x7: {                                  // ROR
            result = barrel_shift(rd_val, 3, rs_val & 0xFF, true, carry);
            break;
        }
        case 0x8: result = rd_val & rs_val; break;  // TST (no write)
        case 0x9: {                                  // NEG (RSB #0)
            auto r = add_with_carry(0, ~rs_val, true);
            result = r.val; carry = r.carry; overflow = r.overflow;
            break;
        }
        case 0xA: {                                  // CMP
            auto r = add_with_carry(rd_val, ~rs_val, true);
            result = r.val; carry = r.carry; overflow = r.overflow;
            cpu.set_nz_from(result);
            cpu.set_flag(kCpsrC, carry);
            cpu.set_flag(kCpsrV, overflow);
            return; // no register write
        }
        case 0xB: {                                  // CMN
            auto r = add_with_carry(rd_val, rs_val, false);
            result = r.val; carry = r.carry; overflow = r.overflow;
            cpu.set_nz_from(result);
            cpu.set_flag(kCpsrC, carry);
            cpu.set_flag(kCpsrV, overflow);
            return;
        }
        case 0xC: result = rd_val | rs_val; break;  // ORR
        case 0xD: result = rd_val * rs_val; break;   // MUL
        case 0xE: result = rd_val & ~rs_val; break;  // BIC
        case 0xF: result = ~rs_val; break;            // MVN
    }

    bool no_write = (op == 0x8);
    if (!no_write) cpu.reg(dest) = result;
    cpu.set_nz_from(result);
    cpu.set_flag(kCpsrC, carry);
    cpu.set_flag(kCpsrV, overflow);
}

// ── Format 5: Hi-register ops / BX ──────────────────────────────────

void thumb_hireg_bx(Cpu& cpu, MemoryBus& /*bus*/, std::uint16_t instr) {
    int op = (instr >> 8) & 3;
    int dest = ((instr >> 4) & 0x8) | (instr & 0x7);
    int src = (instr >> 3) & 0xF;
    std::uint32_t rd_val = cpu.reg(dest);
    std::uint32_t rs_val = cpu.reg(src);
    if (src == 15) rs_val = cpu.pc();
    if (dest == 15 && op != 3) rd_val = cpu.pc();

    switch (op) {
        case 0: // ADD
            if (dest == 15)
                cpu.flush_pipeline(rd_val + rs_val);
            else
                cpu.reg(dest) = rd_val + rs_val;
            break;
        case 1: { // CMP
            auto r = add_with_carry(rd_val, ~rs_val, true);
            cpu.set_nz_from(r.val);
            cpu.set_flag(kCpsrC, r.carry);
            cpu.set_flag(kCpsrV, r.overflow);
            break;
        }
        case 2: // MOV
            if (dest == 15)
                cpu.flush_pipeline(rs_val);
            else
                cpu.reg(dest) = rs_val;
            break;
        case 3: // BX
            if (rs_val & 1) {
                cpu.set_cpsr(cpu.cpsr() | kCpsrT);
                cpu.flush_pipeline(rs_val & ~1u);
            } else {
                cpu.set_cpsr(cpu.cpsr() & ~kCpsrT);
                cpu.flush_pipeline(rs_val & ~3u);
            }
            break;
    }
}

// ── Format 6: PC-relative load ───────────────────────────────────────

void thumb_pc_relative_load(Cpu& cpu, MemoryBus& bus, std::uint16_t instr) {
    int dest = (instr >> 8) & 7;
    std::uint32_t offset = (instr & 0xFF) * 4;
    std::uint32_t addr = (cpu.pc() & ~3u) + offset;
    cpu.reg(dest) = bus.read32(addr);
}

// ── Format 7/8: Load/store register offset ───────────────────────────

void thumb_load_store_reg(Cpu& cpu, MemoryBus& bus, std::uint16_t instr) {
    int op = (instr >> 10) & 3;
    std::uint32_t addr = cpu.reg(trs(instr)) + cpu.reg(trn(instr));
    int dest = trd(instr);
    switch (op) {
        case 0: bus.write32(addr, cpu.reg(dest)); break;          // STR
        case 1: bus.write8(addr, static_cast<std::uint8_t>(
                    cpu.reg(dest))); break;                        // STRB
        case 2: {                                                  // LDR
            std::uint32_t val = bus.read32(addr & ~3u);
            int rot = static_cast<int>(addr & 3) * 8;
            if (rot) val = (val >> rot) | (val << (32 - rot));
            cpu.reg(dest) = val;
            break;
        }
        case 3: cpu.reg(dest) = bus.read8(addr); break;           // LDRB
    }
}

void thumb_load_store_sign(Cpu& cpu, MemoryBus& bus, std::uint16_t instr) {
    int op = (instr >> 10) & 3;
    std::uint32_t addr = cpu.reg(trs(instr)) + cpu.reg(trn(instr));
    int dest = trd(instr);
    switch (op) {
        case 0: // STRH
            bus.write16(addr, static_cast<std::uint16_t>(cpu.reg(dest)));
            break;
        case 1: // LDSB
            cpu.reg(dest) = static_cast<std::uint32_t>(
                static_cast<std::int8_t>(bus.read8(addr)));
            break;
        case 2: // LDRH
            cpu.reg(dest) = bus.read16(addr & ~1u);
            break;
        case 3: // LDSH
            cpu.reg(dest) = static_cast<std::uint32_t>(
                static_cast<std::int16_t>(bus.read16(addr & ~1u)));
            break;
    }
}

// ── Format 9: Load/store immediate offset ────────────────────────────

void thumb_load_store_imm(Cpu& cpu, MemoryBus& bus, std::uint16_t instr) {
    bool byte = (instr >> 12) & 1;
    bool load = (instr >> 11) & 1;
    std::uint32_t offset = (instr >> 6) & 0x1F;
    std::uint32_t base = cpu.reg(trs(instr));
    int dest = trd(instr);

    if (byte) {
        if (load)
            cpu.reg(dest) = bus.read8(base + offset);
        else
            bus.write8(base + offset,
                       static_cast<std::uint8_t>(cpu.reg(dest)));
    } else {
        offset *= 4;
        if (load) {
            std::uint32_t addr = base + offset;
            std::uint32_t val = bus.read32(addr & ~3u);
            int rot = static_cast<int>(addr & 3) * 8;
            if (rot) val = (val >> rot) | (val << (32 - rot));
            cpu.reg(dest) = val;
        } else {
            bus.write32(base + offset, cpu.reg(dest));
        }
    }
}

// ── Format 10: Load/store halfword ───────────────────────────────────

void thumb_load_store_half(Cpu& cpu, MemoryBus& bus, std::uint16_t instr) {
    bool load = (instr >> 11) & 1;
    std::uint32_t offset = ((instr >> 6) & 0x1F) * 2;
    std::uint32_t addr = cpu.reg(trs(instr)) + offset;
    int dest = trd(instr);
    if (load)
        cpu.reg(dest) = bus.read16(addr & ~1u);
    else
        bus.write16(addr & ~1u,
                    static_cast<std::uint16_t>(cpu.reg(dest)));
}

// ── Format 11: SP-relative load/store ────────────────────────────────

void thumb_sp_relative(Cpu& cpu, MemoryBus& bus, std::uint16_t instr) {
    bool load = (instr >> 11) & 1;
    int dest = (instr >> 8) & 7;
    std::uint32_t offset = (instr & 0xFF) * 4;
    std::uint32_t addr = cpu.reg(13) + offset;
    if (load)
        cpu.reg(dest) = bus.read32(addr);
    else
        bus.write32(addr, cpu.reg(dest));
}

// ── Format 12: Load address (ADD Rd, PC/SP, #nn) ────────────────────

void thumb_load_address(Cpu& cpu, MemoryBus& /*bus*/, std::uint16_t instr) {
    bool sp = (instr >> 11) & 1;
    int dest = (instr >> 8) & 7;
    std::uint32_t offset = (instr & 0xFF) * 4;
    if (sp)
        cpu.reg(dest) = cpu.reg(13) + offset;
    else
        cpu.reg(dest) = (cpu.pc() & ~3u) + offset;
}

// ── Format 13: Add offset to SP ──────────────────────────────────────

void thumb_sp_offset(Cpu& cpu, MemoryBus& /*bus*/, std::uint16_t instr) {
    std::uint32_t offset = (instr & 0x7F) * 4;
    if (instr & 0x80)
        cpu.reg(13) -= offset;
    else
        cpu.reg(13) += offset;
}

// ── Format 14: Push / Pop ────────────────────────────────────────────

void thumb_push_pop(Cpu& cpu, MemoryBus& bus, std::uint16_t instr) {
    bool load = (instr >> 11) & 1;
    bool pc_lr = (instr >> 8) & 1;
    std::uint8_t reg_list = instr & 0xFF;

    if (load) {
        // POP
        std::uint32_t addr = cpu.reg(13);
        for (int i = 0; i < 8; ++i) {
            if (reg_list & (1 << i)) {
                cpu.reg(i) = bus.read32(addr);
                addr += 4;
            }
        }
        if (pc_lr) {
            std::uint32_t val = bus.read32(addr);
            addr += 4;
            cpu.flush_pipeline(val & ~1u);
        }
        cpu.reg(13) = addr;
    } else {
        // PUSH
        int count = 0;
        for (int i = 0; i < 8; ++i)
            if (reg_list & (1 << i)) ++count;
        if (pc_lr) ++count;

        std::uint32_t addr = cpu.reg(13) - static_cast<std::uint32_t>(count) * 4;
        cpu.reg(13) = addr;
        for (int i = 0; i < 8; ++i) {
            if (reg_list & (1 << i)) {
                bus.write32(addr, cpu.reg(i));
                addr += 4;
            }
        }
        if (pc_lr) {
            bus.write32(addr, cpu.reg(14));
        }
    }
}

// ── Format 15: Multiple load/store ───────────────────────────────────

void thumb_ldm_stm(Cpu& cpu, MemoryBus& bus, std::uint16_t instr) {
    bool load = (instr >> 11) & 1;
    int rn_idx = (instr >> 8) & 7;
    std::uint8_t reg_list = instr & 0xFF;
    std::uint32_t addr = cpu.reg(rn_idx);

    // Empty list: ARM7 quirk — r15 transferred + base incremented by 0x40.
    // Rarely used; stub for now.
    if (reg_list == 0) return;

    for (int i = 0; i < 8; ++i) {
        if (!(reg_list & (1 << i))) continue;
        if (load)
            cpu.reg(i) = bus.read32(addr);
        else
            bus.write32(addr, cpu.reg(i));
        addr += 4;
    }
    // Writeback unless Rn is in the load list.
    if (!load || !(reg_list & (1 << rn_idx)))
        cpu.reg(rn_idx) = addr;
}

// ── Format 16: Conditional branch ────────────────────────────────────

void thumb_cond_branch(Cpu& cpu, MemoryBus& /*bus*/, std::uint16_t instr) {
    auto cond = static_cast<std::uint8_t>((instr >> 8) & 0xF);
    if (!cpu.check_condition(cond)) return;
    auto offset =
        static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::int8_t>(instr & 0xFF)) * 2);
    cpu.flush_pipeline(cpu.pc() + offset);
}

// ── Format 17: SWI ───────────────────────────────────────────────────

void thumb_swi(Cpu& cpu, MemoryBus& bus, std::uint16_t instr) {
    std::uint8_t comment = static_cast<std::uint8_t>(instr & 0xFF);
    Io* io = cpu.get_io();
    if (io && hle_bios_call(comment, cpu, bus, *io)) return;

    std::uint32_t return_addr = cpu.pc() - 2;
    std::uint32_t old_cpsr = cpu.cpsr();
    cpu.set_cpsr((old_cpsr & ~(kCpsrModeMask | kCpsrT)) |
                 static_cast<std::uint32_t>(CpuMode::Supervisor) | kCpsrI);
    cpu.set_spsr(old_cpsr);
    cpu.reg(14) = return_addr;
    cpu.flush_pipeline(0x00000008);
}

// ── Format 18: Unconditional branch ──────────────────────────────────

void thumb_branch(Cpu& cpu, MemoryBus& /*bus*/, std::uint16_t instr) {
    auto offset = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(static_cast<std::int16_t>(
            (instr & 0x7FF) << 5)) >> 4);
    cpu.flush_pipeline(cpu.pc() + offset);
}

// ── Format 19: Long branch with link ─────────────────────────────────
// Two-instruction sequence: first sets up the high bits in LR, second
// adds the low bits and performs the call.

void thumb_long_branch_hi(Cpu& cpu, MemoryBus& /*bus*/, std::uint16_t instr) {
    auto offset = static_cast<std::int32_t>(
        static_cast<std::int16_t>((instr & 0x7FF) << 5)) >> 5;
    cpu.reg(14) = cpu.pc() + static_cast<std::uint32_t>(offset << 12);
}

void thumb_long_branch_lo(Cpu& cpu, MemoryBus& /*bus*/, std::uint16_t instr) {
    std::uint32_t offset = (instr & 0x7FF) * 2;
    std::uint32_t target = cpu.reg(14) + offset;
    cpu.reg(14) = (cpu.pc() - 2) | 1;
    cpu.flush_pipeline(target);
}

// ── Undefined ────────────────────────────────────────────────────────

void thumb_undefined(Cpu& /*cpu*/, MemoryBus& /*bus*/, std::uint16_t /*i*/) {}

// ── Dispatch table ───────────────────────────────────────────────────
// 10-bit key = bits 15-6 of the instruction.

ThumbHandler classify_thumb(std::uint32_t key) {
    // Bits 15:13 give the major group.
    std::uint32_t top3 = key >> 7;

    switch (top3) {
        case 0b000: {
            // Format 1 (shift) unless bits 12:11 = 11 → format 2 (add/sub)
            if (((key >> 5) & 3) == 3) return thumb_add_sub;
            return thumb_shift_imm;
        }
        case 0b001: return thumb_imm_op;         // format 3
        case 0b010: {
            std::uint32_t sub = (key >> 4) & 0x3F; // bits 15:10 >> offset
            // bits 15:10 == 010000 → ALU (format 4)
            if ((key >> 4) == 0b010000) return thumb_alu;
            // bits 15:10 == 010001 → hi-reg/BX (format 5)
            if ((key >> 4) == 0b010001) return thumb_hireg_bx;
            // bits 15:11 == 01001 → PC-relative load (format 6)
            if ((key >> 5) == 0b01001) return thumb_pc_relative_load;
            // bits 15:12 == 0101:
            //   bit 9 = 0 → format 7 (load/store reg offset)
            //   bit 9 = 1 → format 8 (load/store sign-extended)
            if (((key >> 6) & 0xF) == 0b0101) {
                if ((key >> 3) & 1) return thumb_load_store_sign;
                return thumb_load_store_reg;
            }
            (void)sub;
            return thumb_undefined;
        }
        case 0b011: return thumb_load_store_imm;  // format 9
        case 0b100: {
            // bits 15:12 == 1000 → format 10 (halfword)
            if (((key >> 6) & 1) == 0) return thumb_load_store_half;
            // bits 15:12 == 1001 → format 11 (SP-relative)
            return thumb_sp_relative;
        }
        case 0b101: {
            // bits 15:12 == 1010 → format 12 (load address)
            if (((key >> 6) & 1) == 0) return thumb_load_address;
            // bits 15:12 == 1011:
            //   bits 15:8 == 10110000 → format 13 (SP offset)
            //   bits 10:9 == 10       → format 14 (push/pop)
            if (((key >> 2) & 0xFF) == 0xB0) return thumb_sp_offset;
            if (((key >> 3) & 3) == 2) return thumb_push_pop;
            return thumb_undefined;
        }
        case 0b110: {
            // bits 15:12 == 1100 → format 15 (LDM/STM)
            if (((key >> 6) & 1) == 0) return thumb_ldm_stm;
            // bits 15:12 == 1101:
            //   bits 11:8 == 1111 → SWI (format 17)
            //   bits 11:8 == 1110 → undefined
            //   else → conditional branch (format 16)
            std::uint32_t cond = (key >> 2) & 0xF;
            if (cond == 0xF) return thumb_swi;
            if (cond == 0xE) return thumb_undefined;
            return thumb_cond_branch;
        }
        case 0b111: {
            // bits 15:12 == 1110 → format 18 (unconditional branch)
            if (((key >> 6) & 1) == 0) return thumb_branch;
            // bits 15:12 == 1111:
            //   bit 11 = 0 → long branch high (format 19 part 1)
            //   bit 11 = 1 → long branch low  (format 19 part 2)
            if ((key >> 5) & 1) return thumb_long_branch_lo;
            return thumb_long_branch_hi;
        }
    }
    return thumb_undefined;
}

struct ThumbDispatchTable {
    ThumbHandler handlers[1024];
    ThumbDispatchTable() {
        for (std::uint32_t i = 0; i < 1024; ++i)
            handlers[i] = classify_thumb(i);
    }
};

const ThumbDispatchTable& thumb_table() {
    static const ThumbDispatchTable table;
    return table;
}

} // anonymous namespace

void execute_thumb(Cpu& cpu, MemoryBus& bus, std::uint16_t instr) {
    std::uint32_t key = (instr >> 6) & 0x3FF;
    thumb_table().handlers[key](cpu, bus, instr);
}

} // namespace kairo::backend
