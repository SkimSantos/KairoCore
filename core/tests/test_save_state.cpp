#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "emulator/emulator_instance.hpp"
#include "emulator/save_state.hpp"

int main() {
    using kairo::core::EmulatorInstance;
    using kairo::core::InputState;
    using kairo::core::SaveStatePayload;

    // Round-trip the frame counter through save / load, across slots.
    {
        EmulatorInstance emu(nullptr, nullptr);
        for (int i = 0; i < 5; ++i) emu.step_one_frame();
        assert(emu.frame_number() == 5);

        emu.save_state_to_slot(1);

        for (int i = 0; i < 3; ++i) emu.step_one_frame();
        assert(emu.frame_number() == 8);

        emu.save_state_to_slot(2);

        for (int i = 0; i < 10; ++i) emu.step_one_frame();
        assert(emu.frame_number() == 18);

        assert(emu.load_state_from_slot(1));
        assert(emu.frame_number() == 5);

        assert(emu.load_state_from_slot(2));
        assert(emu.frame_number() == 8);

        // Non-existent slot fails gracefully.
        assert(!emu.load_state_from_slot(99));
    }

    // list_save_slots returns entries sorted by id.
    {
        EmulatorInstance emu(nullptr, nullptr);
        assert(emu.list_save_slots().empty());

        emu.save_state_to_slot(3);
        emu.step_one_frame();
        emu.save_state_to_slot(1);
        emu.step_one_frame();
        emu.save_state_to_slot(2);

        const auto slots = emu.list_save_slots();
        assert(slots.size() == 3);
        assert(slots[0].slot_id == 1);
        assert(slots[1].slot_id == 2);
        assert(slots[2].slot_id == 3);

        for (const auto& s : slots) {
            assert(!s.state_data.empty());
            assert(s.timestamp > 0);
        }
    }

    // Overwriting a slot replaces its contents.
    {
        EmulatorInstance emu(nullptr, nullptr);
        emu.step_one_frame();
        emu.save_state_to_slot(1);

        for (int i = 0; i < 5; ++i) emu.step_one_frame();
        emu.save_state_to_slot(1);

        assert(emu.list_save_slots().size() == 1);

        for (int i = 0; i < 10; ++i) emu.step_one_frame();
        assert(emu.frame_number() == 16);

        assert(emu.load_state_from_slot(1));
        assert(emu.frame_number() == 6);
    }

    // Low-level serializer round-trip (v2: includes timestamp + thumbnail).
    {
        SaveStatePayload p;
        p.version = kairo::core::kSaveStateVersion;
        p.rom_id = 0xDEADBEEFCAFEBABEull;
        p.frame_number = 123'456'789ull;
        p.timestamp = 0x1122334455667788ull;
        p.input.a = true;
        p.input.b = false;
        p.input.start = true;
        p.input.down = true;
        p.input.right = true;
        // Fill the thumbnail with a deterministic pattern we can verify.
        for (int i = 0; i < kairo::core::kSaveStateThumbnailPixelCount; ++i) {
            p.thumbnail[static_cast<std::size_t>(i)] =
                0xFF000000u | static_cast<std::uint32_t>(i * 37);
        }

        const auto bytes = kairo::core::serialize_state(p);
        const auto round = kairo::core::deserialize_state(bytes);
        assert(round.has_value());
        assert(round->version == p.version);
        assert(round->rom_id == p.rom_id);
        assert(round->frame_number == p.frame_number);
        assert(round->timestamp == p.timestamp);
        assert(round->input.a == true);
        assert(round->input.b == false);
        assert(round->input.start == true);
        assert(round->input.down == true);
        assert(round->input.right == true);
        assert(round->input.up == false);
        assert(round->input.select == false);
        for (int i = 0; i < kairo::core::kSaveStateThumbnailPixelCount; ++i) {
            assert(round->thumbnail[static_cast<std::size_t>(i)] ==
                   p.thumbnail[static_cast<std::size_t>(i)]);
        }
    }

    // Slot capture populates the thumbnail alongside state_data.
    {
        EmulatorInstance emu(nullptr, nullptr);
        emu.step_one_frame();
        emu.step_one_frame();
        emu.save_state_to_slot(1);
        const auto slots = emu.list_save_slots();
        assert(slots.size() == 1);
        // The render_test_pattern output is non-uniform, so at least
        // one thumbnail pixel must differ from the zero default.
        bool any_nonzero = false;
        for (auto px : slots[0].thumbnail) {
            if (px != 0) { any_nonzero = true; break; }
        }
        assert(any_nonzero);
    }

    // Disk persistence round-trip via a temp directory.
    {
        std::random_device rd;
        const auto dir =
            std::filesystem::temp_directory_path() /
            ("kairocore_test_" + std::to_string(rd()));
        std::filesystem::create_directories(dir);

        EmulatorInstance emu(nullptr, nullptr);
        // Force a rom_id so states get filtered correctly on reload.
        // load_rom is the public way; use a writable scratch file.
        const auto rom_file = dir / "dummy.gba";
        {
            std::ofstream out(rom_file, std::ios::binary);
            out << "fake-rom";
        }
        assert(emu.load_rom(rom_file.string()));

        emu.step_one_frame();
        emu.save_state_to_slot(1);
        for (int i = 0; i < 4; ++i) emu.step_one_frame();
        emu.save_state_to_slot(2);

        assert(emu.sync_slots_to_dir(dir / "saves"));

        // Fresh emulator + same ROM → disk states load and replay.
        EmulatorInstance reloaded(nullptr, nullptr);
        assert(reloaded.load_rom(rom_file.string()));
        assert(reloaded.load_slots_from_dir(dir / "saves"));

        const auto slots = reloaded.list_save_slots();
        assert(slots.size() == 2);
        assert(reloaded.load_state_from_slot(1));
        assert(reloaded.frame_number() == 1);
        assert(reloaded.load_state_from_slot(2));
        assert(reloaded.frame_number() == 5);

        // A different ROM must not pick up another ROM's saves.
        EmulatorInstance other(nullptr, nullptr);
        const auto other_rom = dir / "other.gba";
        {
            std::ofstream out(other_rom, std::ios::binary);
            out << "different-rom";
        }
        assert(other.load_rom(other_rom.string()));
        assert(other.load_slots_from_dir(dir / "saves"));
        assert(other.list_save_slots().empty());

        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    // Malformed inputs reject gracefully.
    {
        std::vector<std::uint8_t> empty;
        assert(!kairo::core::deserialize_state(empty).has_value());

        std::vector<std::uint8_t> bogus{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        assert(!kairo::core::deserialize_state(bogus).has_value());

        // Valid magic but wrong version.
        SaveStatePayload p;
        p.version = kairo::core::kSaveStateVersion + 1;
        const auto bytes = kairo::core::serialize_state(p);
        assert(!kairo::core::deserialize_state(bytes).has_value());
    }

    return 0;
}
