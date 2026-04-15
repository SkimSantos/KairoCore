#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace kairo::platform {

// A GameProfile bundles per-ROM settings that should survive restarts:
// audio volume, emulation speed, and game-input key bindings. One file
// per ROM, keyed by rom_id. The input binding array is indexed by
// `kairo::platform::InputAction` (10 entries).
struct GameProfile {
    float volume = 1.0f;
    int fast_forward_multiplier = 1;
    std::array<int, 10> input_bindings;

    GameProfile() { input_bindings.fill(-1); }
};

// XDG base-directory-compliant user paths, falling back to
// ~/.config/kairocore and ~/.local/share/kairocore when the
// XDG env vars are unset.
std::filesystem::path user_config_dir();
std::filesystem::path user_data_dir();

// rom_id encoded as a 16-hex-digit lowercase string, safe for filenames.
std::string rom_id_to_hex(std::uint64_t rom_id);

// <config>/profiles/<rom_hex>.profile
std::filesystem::path profile_path_for_rom(std::uint64_t rom_id);

// <data>/saves/<rom_hex>/
std::filesystem::path save_dir_for_rom(std::uint64_t rom_id);

// INI-style serializer (zero-dep, hand-rolled). Returns false on any
// filesystem error; unrecognized keys are ignored by the loader so
// adding new fields is forward-compatible.
bool save_profile(const std::filesystem::path& path, const GameProfile& profile);
std::optional<GameProfile> load_profile(const std::filesystem::path& path);

} // namespace kairo::platform
