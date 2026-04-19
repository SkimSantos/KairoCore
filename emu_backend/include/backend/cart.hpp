#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kairo::backend {

// A parsed GBA cartridge header. Fields follow the 192-byte header layout
// documented in GBATEK. Strings are sanitized to printable ASCII with
// trailing padding stripped so they're safe to log.
struct CartHeader {
    std::string title;        // 12 bytes, NUL-padded
    std::string game_code;    // 4 bytes, e.g. "AGBE" or "BPEE"
    std::string maker_code;   // 2 bytes, e.g. "01" = Nintendo
    std::uint8_t main_unit_code = 0;
    std::uint8_t device_type = 0;
    std::uint8_t version = 0;
    std::uint8_t complement_check = 0;

    // Computed (not loaded): 1-byte complement of the header bytes
    // [0xA0..0xBC]. A real GBA rejects the cart if this doesn't match
    // `complement_check` — we record both so callers can decide what to do.
    std::uint8_t computed_complement = 0;
    bool complement_ok = false;
};

// Smallest valid ROM is the 192-byte header; any smaller is rejected.
inline constexpr std::size_t kCartHeaderSize = 192;

std::optional<CartHeader> parse_cart_header(const std::uint8_t* data,
                                            std::size_t size);

class Cart {
public:
    // Best-effort load. Returns false if the file cannot be read or if
    // the header fails to parse. A failing complement check is logged
    // via header().complement_ok but does NOT block the load — some
    // homebrew ROMs have incorrect headers and we still want to run them.
    bool load_from_file(const std::string& path);
    bool load_from_bytes(std::vector<std::uint8_t> bytes);

    const CartHeader& header() const { return header_; }
    const std::vector<std::uint8_t>& rom() const { return rom_; }
    std::size_t size() const { return rom_.size(); }
    bool loaded() const { return !rom_.empty(); }

    // Hash of the ROM contents, stable across file renames. Used as
    // the per-ROM key for save states and profiles.
    std::uint64_t content_id() const { return content_id_; }

    void clear();

private:
    std::vector<std::uint8_t> rom_;
    CartHeader header_{};
    std::uint64_t content_id_ = 0;
};

} // namespace kairo::backend
