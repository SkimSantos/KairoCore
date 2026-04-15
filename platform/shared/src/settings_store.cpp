#include "platform/settings_store.hpp"

#include <cstdlib>
#include <fstream>
#include <string>
#include <system_error>

namespace kairo::platform {

namespace {

std::filesystem::path home_dir() {
    if (const char* h = std::getenv("HOME"); h && *h) {
        return std::filesystem::path(h);
    }
    return std::filesystem::path("/tmp");
}

std::filesystem::path xdg_or_fallback(const char* env_var,
                                      const std::filesystem::path& fallback) {
    if (const char* v = std::getenv(env_var); v && *v) {
        return std::filesystem::path(v) / "kairocore";
    }
    return fallback / "kairocore";
}

// Trim whitespace from both ends (spaces, tabs, CR, LF).
std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

// Map InputAction index -> key name used in the INI file.
constexpr const char* kBindingKeys[10] = {
    "bind_a", "bind_b", "bind_l", "bind_r",
    "bind_start", "bind_select",
    "bind_up", "bind_down", "bind_left", "bind_right",
};

int binding_index_from_key(const std::string& key) {
    for (int i = 0; i < 10; ++i) {
        if (key == kBindingKeys[i]) return i;
    }
    return -1;
}

} // namespace

std::filesystem::path user_config_dir() {
    return xdg_or_fallback("XDG_CONFIG_HOME", home_dir() / ".config");
}

std::filesystem::path user_data_dir() {
    return xdg_or_fallback("XDG_DATA_HOME", home_dir() / ".local" / "share");
}

std::string rom_id_to_hex(std::uint64_t rom_id) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = digits[rom_id & 0xFu];
        rom_id >>= 4;
    }
    return out;
}

std::filesystem::path profile_path_for_rom(std::uint64_t rom_id) {
    return user_config_dir() / "profiles" / (rom_id_to_hex(rom_id) + ".profile");
}

std::filesystem::path save_dir_for_rom(std::uint64_t rom_id) {
    return user_data_dir() / "saves" / rom_id_to_hex(rom_id);
}

bool save_profile(const std::filesystem::path& path,
                  const GameProfile& profile) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;

    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;

    f << "# KairoCore game profile\n";
    f << "volume = " << profile.volume << '\n';
    f << "fast_forward_multiplier = " << profile.fast_forward_multiplier << '\n';
    for (int i = 0; i < 10; ++i) {
        f << kBindingKeys[i] << " = " << profile.input_bindings[i] << '\n';
    }
    return static_cast<bool>(f);
}

std::optional<GameProfile> load_profile(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f) return std::nullopt;

    GameProfile profile;
    std::string line;
    while (std::getline(f, line)) {
        const auto trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        const auto eq = trimmed.find('=');
        if (eq == std::string::npos) continue;

        const auto key = trim(trimmed.substr(0, eq));
        const auto value = trim(trimmed.substr(eq + 1));
        if (key.empty() || value.empty()) continue;

        try {
            if (key == "volume") {
                profile.volume = std::stof(value);
            } else if (key == "fast_forward_multiplier") {
                profile.fast_forward_multiplier = std::stoi(value);
            } else if (const int idx = binding_index_from_key(key); idx >= 0) {
                profile.input_bindings[static_cast<std::size_t>(idx)] =
                    std::stoi(value);
            }
            // Unknown keys are silently ignored for forward-compat.
        } catch (...) {
            // One bad line shouldn't invalidate an entire profile.
            continue;
        }
    }
    return profile;
}

} // namespace kairo::platform
