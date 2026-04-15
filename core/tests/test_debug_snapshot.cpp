#include <algorithm>
#include <cassert>
#include <string>

#include "emulator/debug_snapshot.hpp"
#include "emulator/emulator_instance.hpp"

int main() {
    using kairo::core::DebugEntry;
    using kairo::core::DebugSnapshot;
    using kairo::core::EmulatorInstance;
    using kairo::core::MemoryWatch;

    // format_hex pads and uppercases.
    {
        assert(kairo::core::format_hex(0, 4) == "0x0000");
        assert(kairo::core::format_hex(0xAB, 4) == "0x00AB");
        assert(kairo::core::format_hex(0xDEADBEEF, 8) == "0xDEADBEEF");
        assert(kairo::core::format_hex(0x1, 2) == "0x01");
    }

    // Default debug mode is off; snapshot still captures regardless.
    {
        EmulatorInstance emu(nullptr, nullptr);
        assert(!emu.is_debug_mode());

        emu.set_debug_mode(true);
        assert(emu.is_debug_mode());
        emu.set_debug_mode(false);
        assert(!emu.is_debug_mode());
    }

    // Snapshot shape: CPU regs, hardware regs, frame counter tracks.
    {
        EmulatorInstance emu(nullptr, nullptr);
        auto snap = emu.capture_debug_snapshot();
        assert(snap.frame_number == 0);
        // r0..r15 + CPSR + frame = 18 entries
        assert(snap.cpu_registers.size() == 18);
        assert(snap.cpu_registers[0].name == "r0");
        assert(snap.cpu_registers[15].name == "r15");
        assert(snap.cpu_registers[16].name == "CPSR");
        assert(snap.cpu_registers[17].name == "frame");
        assert(snap.cpu_registers[17].value == "0");

        // Hardware regs include the core MMIO set.
        assert(snap.hardware_registers.size() >= 6);
        const auto has_reg = [&](const std::string& name) {
            return std::any_of(
                snap.hardware_registers.begin(),
                snap.hardware_registers.end(),
                [&](const DebugEntry& e) { return e.name == name; });
        };
        assert(has_reg("DISPCNT"));
        assert(has_reg("DISPSTAT"));
        assert(has_reg("VCOUNT"));
        assert(has_reg("IE"));
        assert(has_reg("IF"));
        assert(has_reg("IME"));

        // No watches configured → empty memory_watch section.
        assert(snap.memory_watch.empty());

        // Frame counter propagates into the snapshot.
        for (int i = 0; i < 7; ++i) emu.step_one_frame();
        snap = emu.capture_debug_snapshot();
        assert(snap.frame_number == 7);
        assert(snap.cpu_registers[17].value == "7");
    }

    // Watch list: add / list / remove / clear.
    {
        EmulatorInstance emu(nullptr, nullptr);
        assert(emu.list_memory_watches().empty());

        emu.add_memory_watch("hp", 0x02000000, 2);
        emu.add_memory_watch("gold", 0x02000010, 4);
        emu.add_memory_watch("lives", 0x02000020, 1);

        auto watches = emu.list_memory_watches();
        assert(watches.size() == 3);
        assert(watches[0].name == "hp");
        assert(watches[0].address == 0x02000000);
        assert(watches[0].byte_count == 2);
        assert(watches[2].name == "lives");
        assert(watches[2].byte_count == 1);

        // Invalid byte_count falls back to 4.
        emu.add_memory_watch("weird", 0x02000030, 3);
        watches = emu.list_memory_watches();
        assert(watches.size() == 4);
        assert(watches[3].byte_count == 4);

        // Re-adding the same address replaces the existing watch in place.
        emu.add_memory_watch("hp_renamed", 0x02000000, 4);
        watches = emu.list_memory_watches();
        assert(watches.size() == 4);
        assert(watches[0].address == 0x02000000);
        assert(watches[0].name == "hp_renamed");
        assert(watches[0].byte_count == 4);

        // Snapshot reflects the current watch list.
        auto snap = emu.capture_debug_snapshot();
        assert(snap.memory_watch.size() == 4);
        // Each entry encodes name + hex address + size.
        assert(snap.memory_watch[0].name.find("hp_renamed") != std::string::npos);
        assert(snap.memory_watch[0].name.find("0x02000000") != std::string::npos);
        assert(snap.memory_watch[0].value == "??");

        // Remove by address.
        assert(emu.remove_memory_watch(0x02000010));
        assert(!emu.remove_memory_watch(0xDEADBEEF)); // non-existent
        watches = emu.list_memory_watches();
        assert(watches.size() == 3);
        const bool gold_gone = std::none_of(
            watches.begin(), watches.end(),
            [](const MemoryWatch& w) { return w.address == 0x02000010; });
        assert(gold_gone);

        emu.clear_memory_watches();
        assert(emu.list_memory_watches().empty());
        snap = emu.capture_debug_snapshot();
        assert(snap.memory_watch.empty());
    }

    // Watches survive reset — they're user configuration, not game state.
    {
        EmulatorInstance emu(nullptr, nullptr);
        emu.add_memory_watch("hp", 0x02000000, 2);
        emu.set_debug_mode(true);
        emu.reset();
        assert(emu.list_memory_watches().size() == 1);
        assert(emu.is_debug_mode());
    }

    return 0;
}
