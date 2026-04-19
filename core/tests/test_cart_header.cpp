#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include "backend/cart.hpp"

namespace {

// Build a 192-byte ROM with the given header fields populated.
// Fills [0xA0..0xBD] manually and lets the caller decide whether to
// compute the complement or leave it broken.
std::vector<std::uint8_t> make_rom(const char* title,
                                   const char* game_code,
                                   const char* maker_code,
                                   bool fix_complement) {
    std::vector<std::uint8_t> rom(192, 0);

    std::memcpy(&rom[0xA0], title, std::strlen(title));        // up to 12
    std::memcpy(&rom[0xAC], game_code, 4);
    std::memcpy(&rom[0xB0], maker_code, 2);
    rom[0xB3] = 0x00; // main_unit_code
    rom[0xB4] = 0x00; // device_type
    rom[0xBC] = 0x01; // version

    if (fix_complement) {
        int sum = 0;
        for (std::size_t i = 0xA0; i < 0xBD; ++i) sum += rom[i];
        rom[0xBD] = static_cast<std::uint8_t>(-(sum + 0x19));
    } else {
        rom[0xBD] = 0xFF; // deliberately wrong
    }
    return rom;
}

} // namespace

int main() {
    using kairo::backend::Cart;
    using kairo::backend::parse_cart_header;

    // Too small — rejected.
    {
        std::vector<std::uint8_t> tiny(100, 0);
        assert(!parse_cart_header(tiny.data(), tiny.size()).has_value());
    }

    // Well-formed header with matching complement.
    {
        const auto rom = make_rom("POKEMON EMER", "BPEE", "01", true);
        const auto parsed = parse_cart_header(rom.data(), rom.size());
        assert(parsed.has_value());
        assert(parsed->title == "POKEMON EMER");
        assert(parsed->game_code == "BPEE");
        assert(parsed->maker_code == "01");
        assert(parsed->version == 0x01);
        assert(parsed->complement_ok);
    }

    // Wrong complement is still parsed, but flagged.
    {
        const auto rom = make_rom("BADCART", "XXXX", "99", false);
        const auto parsed = parse_cart_header(rom.data(), rom.size());
        assert(parsed.has_value());
        assert(!parsed->complement_ok);
    }

    // Cart loads bytes, caches header, computes a content_id.
    {
        Cart cart;
        assert(!cart.loaded());

        const auto rom = make_rom("TESTCART", "TEST", "42", true);
        const auto rom_copy = rom; // keep a copy for comparison
        assert(cart.load_from_bytes(rom));
        assert(cart.loaded());
        assert(cart.size() == 192);
        assert(cart.header().title == "TESTCART");
        assert(cart.header().complement_ok);
        assert(cart.content_id() != 0);

        // Reloading the same bytes yields the same content_id.
        Cart cart2;
        assert(cart2.load_from_bytes(rom_copy));
        assert(cart2.content_id() == cart.content_id());

        // A different ROM yields a different content_id.
        Cart cart3;
        assert(cart3.load_from_bytes(make_rom("OTHER", "OTHR", "99", true)));
        assert(cart3.content_id() != cart.content_id());
    }

    // Non-printable bytes in the title area get scrubbed.
    {
        std::vector<std::uint8_t> rom(192, 0);
        rom[0xA0] = 'O';
        rom[0xA1] = 'K';
        rom[0xA2] = 0x01; // non-printable
        rom[0xA3] = 'A';
        std::memcpy(&rom[0xAC], "AAAA", 4);
        std::memcpy(&rom[0xB0], "00", 2);
        const auto parsed = parse_cart_header(rom.data(), rom.size());
        assert(parsed.has_value());
        assert(parsed->title == "OK?A");
    }

    return 0;
}
