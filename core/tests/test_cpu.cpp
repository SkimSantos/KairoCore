#include <cassert>
#include <cstdint>

#include "backend/cpu.hpp"
#include "backend/memory.hpp"

using kairo::backend::barrel_shift;
using kairo::backend::Cpu;
using kairo::backend::CpuMode;
using kairo::backend::evaluate_condition;
using kairo::backend::kCpsrC;
using kairo::backend::kCpsrF;
using kairo::backend::kCpsrI;
using kairo::backend::kCpsrN;
using kairo::backend::kCpsrT;
using kairo::backend::kCpsrV;
using kairo::backend::kCpsrZ;
using kairo::backend::MemoryBus;

// ── Barrel shifter ───────────────────────────────────────────────────

void test_barrel_shifter() {
    bool carry;

    // LSL #0 passes value and carry through.
    carry = true;
    assert(barrel_shift(0xDEADBEEF, 0, 0, false, carry) == 0xDEADBEEF);
    assert(carry == true);

    // LSL #1
    carry = false;
    assert(barrel_shift(0x80000001, 0, 1, false, carry) == 0x00000002);
    assert(carry == true); // bit 31 shifted out

    // LSR #0 immediate encodes LSR #32.
    carry = false;
    assert(barrel_shift(0x80000000, 1, 0, false, carry) == 0);
    assert(carry == true);

    // LSR by register with amount 0 → pass-through.
    carry = false;
    assert(barrel_shift(0x12345678, 1, 0, true, carry) == 0x12345678);
    assert(carry == false);

    // ASR #0 immediate encodes ASR #32 (fill with sign).
    carry = false;
    assert(barrel_shift(0x80000000, 2, 0, false, carry) == 0xFFFFFFFF);
    assert(carry == true);

    // ASR positive value
    carry = false;
    assert(barrel_shift(0x00000080, 2, 4, false, carry) == 0x00000008);
    assert(carry == false);

    // ROR #0 immediate encodes RRX.
    carry = true;
    assert(barrel_shift(0x00000001, 3, 0, false, carry) == 0x80000000);
    assert(carry == true); // bit 0 shifted out

    // ROR #4
    carry = false;
    assert(barrel_shift(0x0000000F, 3, 4, false, carry) == 0xF0000000);
    assert(carry == true);

    // LSL #32 → zero, carry = bit 0.
    carry = false;
    assert(barrel_shift(0x00000001, 0, 32, true, carry) == 0);
    assert(carry == true);

    // LSL > 32 → zero.
    carry = true;
    assert(barrel_shift(0xFFFFFFFF, 0, 33, true, carry) == 0);
    assert(carry == false);
}

// ── Condition evaluation ─────────────────────────────────────────────

void test_conditions() {
    // EQ: Z set
    assert(evaluate_condition(0x0, kCpsrZ));
    assert(!evaluate_condition(0x0, 0));

    // NE: Z clear
    assert(evaluate_condition(0x1, 0));
    assert(!evaluate_condition(0x1, kCpsrZ));

    // CS/HS: C set
    assert(evaluate_condition(0x2, kCpsrC));

    // MI: N set
    assert(evaluate_condition(0x4, kCpsrN));

    // VS: V set
    assert(evaluate_condition(0x6, kCpsrV));

    // HI: C set and Z clear
    assert(evaluate_condition(0x8, kCpsrC));
    assert(!evaluate_condition(0x8, kCpsrC | kCpsrZ));

    // GE: N == V
    assert(evaluate_condition(0xA, 0));
    assert(evaluate_condition(0xA, kCpsrN | kCpsrV));
    assert(!evaluate_condition(0xA, kCpsrN));

    // GT: Z clear and N == V
    assert(evaluate_condition(0xC, 0));
    assert(!evaluate_condition(0xC, kCpsrZ));

    // AL: always
    assert(evaluate_condition(0xE, 0));
    assert(evaluate_condition(0xE, 0xFFFFFFFF));

    // NV: never
    assert(!evaluate_condition(0xF, 0));
}

// ── CPU reset state ──────────────────────────────────────────────────

void test_reset_state() {
    Cpu cpu;
    // Post-BIOS: System mode, IRQ+FIQ disabled, ARM, PC at cart entry.
    assert(cpu.mode() == CpuMode::System);
    assert(!cpu.in_thumb());
    assert((cpu.cpsr() & kCpsrI) != 0);
    assert((cpu.cpsr() & kCpsrF) != 0);
    // r15 = 0x08000000 + 8 (pipeline offset)
    assert(cpu.pc() == 0x08000008);
    // SP = user/system stack
    assert(cpu.reg(13) == 0x03007F00);
}

// ── Instruction execution via bus ────────────────────────────────────
// We write ARM instructions into EWRAM and set PC there. This avoids
// needing a cart for the simplest tests.

void write_arm(MemoryBus& bus, std::uint32_t addr, std::uint32_t instr) {
    bus.write32(addr, instr);
}

void setup_cpu_at(Cpu& cpu, std::uint32_t addr) {
    // Point r15 into EWRAM. Keep ARM mode.
    cpu.set_cpsr((cpu.cpsr() & ~kCpsrT));
    // flush_pipeline isn't accessible directly; rewrite r15 to
    // simulate it: r15 = addr + 8 for ARM pipeline.
    cpu.reg(15) = addr + 8;
    cpu.clear_branched();
}

void test_data_processing() {
    MemoryBus bus;
    Cpu cpu;

    const std::uint32_t base = 0x02000000;

    // MOV r0, #42 (condition AL)
    // Encoding: 0xE3A0002A = cond=AL, I=1, opcode=MOV(1101), S=0, Rn=0, Rd=0, imm=0x2A
    write_arm(bus, base, 0xE3A0002A);
    setup_cpu_at(cpu, base);
    cpu.step(bus, 1);
    assert(cpu.reg(0) == 42);

    // ADD r1, r0, #8 → r1 = 50
    write_arm(bus, base, 0xE2801008);
    setup_cpu_at(cpu, base);
    cpu.reg(0) = 42;
    cpu.step(bus, 1);
    assert(cpu.reg(1) == 50);

    // SUBS r2, r0, r0 → r2 = 0, Z set
    // Encoding: 0xE0502000 = SUB, S=1, Rd=r2, Rn=r0, Rm=r0
    write_arm(bus, base, 0xE0502000);
    setup_cpu_at(cpu, base);
    cpu.reg(0) = 100;
    cpu.step(bus, 1);
    assert(cpu.reg(2) == 0);
    assert(cpu.flag_z());
    assert(!cpu.flag_n());
    assert(cpu.flag_c()); // SUB: 100 - 100 → no borrow → C=1

    // CMP r0, #100 with r0=50 → should set N (50-100 is negative)
    // Encoding: 0xE3500064 = CMP, S=1, Rn=r0, imm=100
    write_arm(bus, base, 0xE3500064);
    setup_cpu_at(cpu, base);
    cpu.reg(0) = 50;
    cpu.step(bus, 1);
    assert(cpu.flag_n());
    assert(!cpu.flag_z());
    assert(!cpu.flag_c()); // 50 < 100 → borrow → C=0

    // AND r3, r0, r1
    write_arm(bus, base, 0xE0003001);
    setup_cpu_at(cpu, base);
    cpu.reg(0) = 0xFF00FF00;
    cpu.reg(1) = 0x0F0F0F0F;
    cpu.step(bus, 1);
    assert(cpu.reg(3) == 0x0F000F00);

    // ORR r4, r0, r1
    write_arm(bus, base, 0xE1804001);
    setup_cpu_at(cpu, base);
    cpu.reg(0) = 0xFF000000;
    cpu.reg(1) = 0x000000FF;
    cpu.step(bus, 1);
    assert(cpu.reg(4) == 0xFF0000FF);

    // MVN r5, #0 → r5 = 0xFFFFFFFF
    write_arm(bus, base, 0xE3E05000);
    setup_cpu_at(cpu, base);
    cpu.step(bus, 1);
    assert(cpu.reg(5) == 0xFFFFFFFF);
}

void test_branch() {
    MemoryBus bus;
    Cpu cpu;

    const std::uint32_t base = 0x02000000;

    // B +8 (skip one instruction): offset field = +8 relative to PC+8,
    // but the 24-bit field encodes (target - (PC+8)) >> 2.
    // From base: target = base + 8 + 8 = base + 16.
    // offset_field = 8 >> 2 = 2. Encoding: 0xEA000002
    write_arm(bus, base, 0xEA000002);
    setup_cpu_at(cpu, base);
    cpu.step(bus, 1);
    // After branch: r15 = target + 8 = base + 16 + 8
    assert(cpu.pc() == base + 16 + 8);

    // BL: should set r14 = base + 4 (return address)
    // BL +0 (branch to next instruction, effectively): offset = 0
    // Encoding: 0xEB000000
    write_arm(bus, base, 0xEB000000);
    setup_cpu_at(cpu, base);
    cpu.step(bus, 1);
    assert(cpu.reg(14) == base + 4);
    // Target = base + 8 + 0 = base + 8, r15 = target + 8 = base + 16
    assert(cpu.pc() == base + 8 + 8);
}

void test_memory_ops() {
    MemoryBus bus;
    Cpu cpu;

    const std::uint32_t code = 0x02000000;
    const std::uint32_t data = 0x02001000;

    // STR r0, [r1] : store r0 at address in r1
    // Encoding: 0xE5810000 = STR Rd=r0, Rn=r1, offset=0, pre, up, no-wb
    write_arm(bus, code, 0xE5810000);
    setup_cpu_at(cpu, code);
    cpu.reg(0) = 0xCAFEBABE;
    cpu.reg(1) = data;
    cpu.step(bus, 1);
    assert(bus.read32(data) == 0xCAFEBABE);

    // LDR r2, [r1] : load from address in r1
    // Encoding: 0xE5912000 = LDR Rd=r2, Rn=r1, offset=0
    write_arm(bus, code, 0xE5912000);
    setup_cpu_at(cpu, code);
    cpu.reg(1) = data;
    cpu.step(bus, 1);
    assert(cpu.reg(2) == 0xCAFEBABE);

    // LDR r3, [r1, #4] : load with immediate offset
    // Encoding: 0xE5913004
    bus.write32(data + 4, 0xDEADBEEF);
    write_arm(bus, code, 0xE5913004);
    setup_cpu_at(cpu, code);
    cpu.reg(1) = data;
    cpu.step(bus, 1);
    assert(cpu.reg(3) == 0xDEADBEEF);

    // STRB r0, [r1] : store byte
    // Encoding: 0xE5C10000
    write_arm(bus, code, 0xE5C10000);
    setup_cpu_at(cpu, code);
    cpu.reg(0) = 0x42;
    cpu.reg(1) = data + 8;
    cpu.step(bus, 1);
    assert(bus.read8(data + 8) == 0x42);
}

void test_block_transfer() {
    MemoryBus bus;
    Cpu cpu;

    const std::uint32_t code = 0x02000000;

    // STMIA r0!, {r1, r2, r3}
    // Encoding: 0xE8A0000E = STM, P=0(IA), U=1(up), W=1(writeback), L=0(store)
    write_arm(bus, code, 0xE8A0000E);
    setup_cpu_at(cpu, code);
    std::uint32_t sp = 0x02002000;
    cpu.reg(0) = sp;
    cpu.reg(1) = 0x11111111;
    cpu.reg(2) = 0x22222222;
    cpu.reg(3) = 0x33333333;
    cpu.step(bus, 1);
    assert(bus.read32(sp + 0) == 0x11111111);
    assert(bus.read32(sp + 4) == 0x22222222);
    assert(bus.read32(sp + 8) == 0x33333333);
    assert(cpu.reg(0) == sp + 12);

    // LDMIA r0!, {r4, r5, r6} starting from sp
    // Encoding: 0xE8B00070
    write_arm(bus, code, 0xE8B00070);
    setup_cpu_at(cpu, code);
    cpu.reg(0) = sp;
    cpu.step(bus, 1);
    assert(cpu.reg(4) == 0x11111111);
    assert(cpu.reg(5) == 0x22222222);
    assert(cpu.reg(6) == 0x33333333);
    assert(cpu.reg(0) == sp + 12);
}

void test_multiply() {
    MemoryBus bus;
    Cpu cpu;

    const std::uint32_t code = 0x02000000;

    // MUL r0, r1, r2 → r0 = r1 * r2
    // Encoding: 0xE0000291 = MUL, Rd=r0, Rm=r1, Rs=r2
    write_arm(bus, code, 0xE0000291);
    setup_cpu_at(cpu, code);
    cpu.reg(1) = 7;
    cpu.reg(2) = 6;
    cpu.step(bus, 1);
    assert(cpu.reg(0) == 42);

    // MLA r0, r1, r2, r3 → r0 = r1 * r2 + r3
    // Encoding: 0xE0203291 = MLA, Rd=r0, Rm=r1, Rs=r2, Rn=r3
    write_arm(bus, code, 0xE0203291);
    setup_cpu_at(cpu, code);
    cpu.reg(1) = 10;
    cpu.reg(2) = 10;
    cpu.reg(3) = 5;
    cpu.step(bus, 1);
    assert(cpu.reg(0) == 105);
}

void test_bx_thumb() {
    MemoryBus bus;
    Cpu cpu;

    const std::uint32_t code = 0x02000000;

    // BX r0 with bit 0 set → switch to Thumb
    // Encoding: 0xE12FFF10
    write_arm(bus, code, 0xE12FFF10);
    setup_cpu_at(cpu, code);
    cpu.reg(0) = 0x02004001; // odd → Thumb
    cpu.step(bus, 1);
    assert(cpu.in_thumb());
    // r15 = 0x02004000 + 4 (Thumb pipeline)
    assert(cpu.pc() == 0x02004004);
}

void test_condition_skipping() {
    MemoryBus bus;
    Cpu cpu;

    const std::uint32_t code = 0x02000000;

    // MOVEQ r0, #99 : only executes when Z flag set
    // Encoding: 0x03A00063 (cond=EQ, MOV, imm=99)
    write_arm(bus, code, 0x03A00063);
    setup_cpu_at(cpu, code);
    cpu.reg(0) = 0;
    cpu.set_flag(kCpsrZ, false);
    cpu.step(bus, 1);
    assert(cpu.reg(0) == 0); // skipped

    // Now with Z set
    write_arm(bus, code, 0x03A00063);
    setup_cpu_at(cpu, code);
    cpu.reg(0) = 0;
    cpu.set_flag(kCpsrZ, true);
    cpu.step(bus, 1);
    assert(cpu.reg(0) == 99); // executed
}

void test_thumb_basic() {
    MemoryBus bus;
    Cpu cpu;

    const std::uint32_t code = 0x02000000;

    // MOV r0, #42 (Thumb format 3: 0010_0_000_00101010 = 0x202A)
    bus.write16(code, 0x202A);
    cpu.set_cpsr((cpu.cpsr() & ~kCpsrT) | kCpsrT);
    cpu.reg(15) = code + 4; // Thumb pipeline offset
    cpu.clear_branched();
    cpu.step(bus, 1);
    assert(cpu.reg(0) == 42);

    // ADD r0, #10 (Thumb format 3: 0011_0_000_00001010 = 0x300A)
    bus.write16(code, 0x300A);
    cpu.reg(15) = code + 4;
    cpu.reg(0) = 42;
    cpu.clear_branched();
    cpu.step(bus, 1);
    assert(cpu.reg(0) == 52);

    // SUB r0, #2 (Thumb format 3: 0011_1_000_00000010 = 0x3802)
    bus.write16(code, 0x3802);
    cpu.reg(15) = code + 4;
    cpu.reg(0) = 52;
    cpu.clear_branched();
    cpu.step(bus, 1);
    assert(cpu.reg(0) == 50);

    // PUSH {r0, r1} (Thumb format 14: 1011_0_10_0_00000011 = 0xB403)
    bus.write16(code, 0xB403);
    cpu.reg(15) = code + 4;
    cpu.reg(0) = 0xAAAAAAAA;
    cpu.reg(1) = 0xBBBBBBBB;
    cpu.reg(13) = 0x02002000;
    cpu.clear_branched();
    cpu.step(bus, 1);
    assert(cpu.reg(13) == 0x02002000 - 8);
    assert(bus.read32(0x02002000 - 8) == 0xAAAAAAAA);
    assert(bus.read32(0x02002000 - 4) == 0xBBBBBBBB);

    // POP {r2, r3} (1011_1_10_0_00001100 = 0xBC0C)
    bus.write16(code, 0xBC0C);
    cpu.reg(15) = code + 4;
    cpu.clear_branched();
    cpu.step(bus, 1);
    assert(cpu.reg(2) == 0xAAAAAAAA);
    assert(cpu.reg(3) == 0xBBBBBBBB);
    assert(cpu.reg(13) == 0x02002000);
}

int main() {
    test_barrel_shifter();
    test_conditions();
    test_reset_state();
    test_data_processing();
    test_branch();
    test_memory_ops();
    test_block_transfer();
    test_multiply();
    test_bx_thumb();
    test_condition_skipping();
    test_thumb_basic();
    return 0;
}
