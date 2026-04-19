#include "backend/cart.hpp"

#include <fstream>
#include <iterator>

namespace kairo::backend {

namespace {

// Offsets into the GBA cartridge header (see GBATEK §Cartridge Header).
constexpr std::size_t kTitleOffset = 0xA0;
constexpr std::size_t kTitleSize = 12;
constexpr std::size_t kGameCodeOffset = 0xAC;
constexpr std::size_t kGameCodeSize = 4;
constexpr std::size_t kMakerCodeOffset = 0xB0;
constexpr std::size_t kMakerCodeSize = 2;
constexpr std::size_t kMainUnitCodeOffset = 0xB3;
constexpr std::size_t kDeviceTypeOffset = 0xB4;
constexpr std::size_t kVersionOffset = 0xBC;
constexpr std::size_t kComplementOffset = 0xBD;
constexpr std::size_t kComplementRangeStart = 0xA0;
constexpr std::size_t kComplementRangeEnd = 0xBD; // exclusive

std::string read_ascii(const std::uint8_t* data, std::size_t offset,
                       std::size_t size) {
    std::string out;
    out.reserve(size);
    for (std::size_t i = 0; i < size; ++i) {
        const char c = static_cast<char>(data[offset + i]);
        if (c == 0) break;
        // Scrub non-printable so logging stays safe; some ROMs have
        // garbage in the padding area.
        out.push_back((c >= 0x20 && c < 0x7F) ? c : '?');
    }
    return out;
}

std::uint8_t compute_header_complement(const std::uint8_t* data) {
    // complement = -(sum(header[0xA0..0xBC]) + 0x19), low 8 bits.
    int sum = 0;
    for (std::size_t i = kComplementRangeStart; i < kComplementRangeEnd; ++i) {
        sum += data[i];
    }
    return static_cast<std::uint8_t>(-(sum + 0x19));
}

// 64-bit FNV-1a. Deterministic and good enough for per-ROM keying.
std::uint64_t fnv1a_64(const std::uint8_t* data, std::size_t size) {
    constexpr std::uint64_t kOffsetBasis = 0xcbf29ce484222325ull;
    constexpr std::uint64_t kPrime = 0x100000001b3ull;
    std::uint64_t h = kOffsetBasis;
    for (std::size_t i = 0; i < size; ++i) {
        h ^= data[i];
        h *= kPrime;
    }
    return h;
}

} // namespace

std::optional<CartHeader> parse_cart_header(const std::uint8_t* data,
                                            std::size_t size) {
    if (size < kCartHeaderSize) return std::nullopt;

    CartHeader h;
    h.title = read_ascii(data, kTitleOffset, kTitleSize);
    h.game_code = read_ascii(data, kGameCodeOffset, kGameCodeSize);
    h.maker_code = read_ascii(data, kMakerCodeOffset, kMakerCodeSize);
    h.main_unit_code = data[kMainUnitCodeOffset];
    h.device_type = data[kDeviceTypeOffset];
    h.version = data[kVersionOffset];
    h.complement_check = data[kComplementOffset];
    h.computed_complement = compute_header_complement(data);
    h.complement_ok = h.computed_complement == h.complement_check;
    return h;
}

bool Cart::load_from_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());
    return load_from_bytes(std::move(bytes));
}

bool Cart::load_from_bytes(std::vector<std::uint8_t> bytes) {
    const auto parsed = parse_cart_header(bytes.data(), bytes.size());
    if (!parsed) return false;
    rom_ = std::move(bytes);
    header_ = *parsed;
    content_id_ = fnv1a_64(rom_.data(), rom_.size());
    return true;
}

void Cart::clear() {
    rom_.clear();
    header_ = {};
    content_id_ = 0;
}

} // namespace kairo::backend
