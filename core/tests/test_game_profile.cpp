#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>

#include "platform/settings_store.hpp"

int main() {
    using kairo::platform::GameProfile;
    using kairo::platform::load_profile;
    using kairo::platform::profile_path_for_rom;
    using kairo::platform::rom_id_to_hex;
    using kairo::platform::save_dir_for_rom;
    using kairo::platform::save_profile;
    using kairo::platform::user_config_dir;
    using kairo::platform::user_data_dir;

    // Hex encoding: 16 zero-padded lowercase digits.
    {
        assert(rom_id_to_hex(0) == "0000000000000000");
        assert(rom_id_to_hex(0xDEADBEEFull) == "00000000deadbeef");
        assert(rom_id_to_hex(0xFFFFFFFFFFFFFFFFull) == "ffffffffffffffff");
    }

    // XDG env overrides honored; filenames routed through rom hex.
    {
        std::random_device rd;
        const auto root =
            std::filesystem::temp_directory_path() /
            ("kairocore_xdg_" + std::to_string(rd()));
        const auto cfg = root / "cfg";
        const auto data = root / "data";
        std::filesystem::create_directories(cfg);
        std::filesystem::create_directories(data);

        setenv("XDG_CONFIG_HOME", cfg.c_str(), 1);
        setenv("XDG_DATA_HOME", data.c_str(), 1);

        assert(user_config_dir() == cfg / "kairocore");
        assert(user_data_dir() == data / "kairocore");

        const std::uint64_t rom = 0xABCDEF0123456789ull;
        const auto prof_path = profile_path_for_rom(rom);
        assert(prof_path.parent_path() ==
               cfg / "kairocore" / "profiles");
        assert(prof_path.filename() == "abcdef0123456789.profile");

        const auto save_path = save_dir_for_rom(rom);
        assert(save_path == data / "kairocore" / "saves" /
                              "abcdef0123456789");

        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        unsetenv("XDG_CONFIG_HOME");
        unsetenv("XDG_DATA_HOME");
    }

    // Round-trip: save → load recovers every field.
    {
        std::random_device rd;
        const auto tmp =
            std::filesystem::temp_directory_path() /
            ("kairocore_profile_" + std::to_string(rd()) + ".profile");

        GameProfile p;
        p.volume = 0.25f;
        p.fast_forward_multiplier = 8;
        p.input_bindings = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};

        assert(save_profile(tmp, p));
        const auto loaded = load_profile(tmp);
        assert(loaded.has_value());
        // volume is float; allow a small tolerance for text round-trip.
        assert(loaded->volume > 0.24f && loaded->volume < 0.26f);
        assert(loaded->fast_forward_multiplier == 8);
        for (int i = 0; i < 10; ++i) {
            assert(loaded->input_bindings[static_cast<std::size_t>(i)] ==
                   p.input_bindings[static_cast<std::size_t>(i)]);
        }

        std::error_code ec;
        std::filesystem::remove(tmp, ec);
    }

    // Missing file → nullopt.
    {
        const auto nonexistent =
            std::filesystem::temp_directory_path() /
            "kairocore_missing_profile_xyz.profile";
        std::error_code ec;
        std::filesystem::remove(nonexistent, ec);
        assert(!load_profile(nonexistent).has_value());
    }

    // Malformed lines are skipped; valid lines still land. Unknown keys
    // ignored for forward-compat.
    {
        std::random_device rd;
        const auto tmp =
            std::filesystem::temp_directory_path() /
            ("kairocore_profile_bad_" + std::to_string(rd()) + ".profile");
        {
            std::ofstream f(tmp);
            f << "# comment line\n";
            f << "\n";
            f << "volume = 0.5\n";
            f << "not_an_assignment_line\n";
            f << "fast_forward_multiplier = 4\n";
            f << "bind_a = 42\n";
            f << "unknown_future_key = whatever\n";
            f << "bind_b = not_a_number\n"; // parse error -> skipped
        }
        const auto loaded = load_profile(tmp);
        assert(loaded.has_value());
        assert(loaded->volume > 0.49f && loaded->volume < 0.51f);
        assert(loaded->fast_forward_multiplier == 4);
        assert(loaded->input_bindings[0] == 42);
        // bind_b line was bad → stays at default (-1).
        assert(loaded->input_bindings[1] == -1);

        std::error_code ec;
        std::filesystem::remove(tmp, ec);
    }

    return 0;
}
