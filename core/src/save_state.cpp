#include "emulator/save_state.hpp"

#include <cstring>

namespace kairo::core {

namespace {

void put_u32_le(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu));
    }
}

void put_u64_le(std::vector<std::uint8_t>& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFull));
    }
}

bool get_u32_le(const std::uint8_t* data, std::size_t size,
                std::size_t& pos, std::uint32_t& out) {
    if (pos + 4 > size) return false;
    out = 0;
    for (int i = 0; i < 4; ++i) {
        out |= static_cast<std::uint32_t>(data[pos + i]) << (i * 8);
    }
    pos += 4;
    return true;
}

bool get_u64_le(const std::uint8_t* data, std::size_t size,
                std::size_t& pos, std::uint64_t& out) {
    if (pos + 8 > size) return false;
    out = 0;
    for (int i = 0; i < 8; ++i) {
        out |= static_cast<std::uint64_t>(data[pos + i]) << (i * 8);
    }
    pos += 8;
    return true;
}

std::uint8_t to_byte(bool b) { return b ? std::uint8_t{1} : std::uint8_t{0}; }

constexpr std::size_t kInputStateBytes = 10;
constexpr std::size_t kThumbnailBytes =
    static_cast<std::size_t>(kSaveStateThumbnailPixelCount) * 4;
constexpr std::size_t kMinPayloadSize =
    sizeof(kSaveStateMagic) + 4 + 8 + 8 + 8 + kInputStateBytes + kThumbnailBytes;

} // namespace

std::vector<std::uint8_t> serialize_state(const SaveStatePayload& payload) {
    std::vector<std::uint8_t> out;
    out.reserve(kMinPayloadSize);

    out.insert(out.end(), kSaveStateMagic,
               kSaveStateMagic + sizeof(kSaveStateMagic));
    put_u32_le(out, payload.version);
    put_u64_le(out, payload.rom_id);
    put_u64_le(out, payload.frame_number);
    put_u64_le(out, payload.timestamp);

    out.push_back(to_byte(payload.input.a));
    out.push_back(to_byte(payload.input.b));
    out.push_back(to_byte(payload.input.l));
    out.push_back(to_byte(payload.input.r));
    out.push_back(to_byte(payload.input.start));
    out.push_back(to_byte(payload.input.select));
    out.push_back(to_byte(payload.input.up));
    out.push_back(to_byte(payload.input.down));
    out.push_back(to_byte(payload.input.left));
    out.push_back(to_byte(payload.input.right));

    for (const auto px : payload.thumbnail) {
        put_u32_le(out, px);
    }

    return out;
}

std::optional<SaveStatePayload> deserialize_state(
    const std::vector<std::uint8_t>& data) {
    if (data.size() < kMinPayloadSize) {
        return std::nullopt;
    }
    if (std::memcmp(data.data(), kSaveStateMagic, sizeof(kSaveStateMagic)) != 0) {
        return std::nullopt;
    }

    std::size_t pos = sizeof(kSaveStateMagic);
    SaveStatePayload p{};
    if (!get_u32_le(data.data(), data.size(), pos, p.version)) return std::nullopt;
    if (p.version != kSaveStateVersion) return std::nullopt;
    if (!get_u64_le(data.data(), data.size(), pos, p.rom_id)) return std::nullopt;
    if (!get_u64_le(data.data(), data.size(), pos, p.frame_number)) return std::nullopt;
    if (!get_u64_le(data.data(), data.size(), pos, p.timestamp)) return std::nullopt;

    if (pos + kInputStateBytes > data.size()) return std::nullopt;
    p.input.a      = data[pos + 0] != 0;
    p.input.b      = data[pos + 1] != 0;
    p.input.l      = data[pos + 2] != 0;
    p.input.r      = data[pos + 3] != 0;
    p.input.start  = data[pos + 4] != 0;
    p.input.select = data[pos + 5] != 0;
    p.input.up     = data[pos + 6] != 0;
    p.input.down   = data[pos + 7] != 0;
    p.input.left   = data[pos + 8] != 0;
    p.input.right  = data[pos + 9] != 0;
    pos += kInputStateBytes;

    for (std::size_t i = 0; i < static_cast<std::size_t>(kSaveStateThumbnailPixelCount); ++i) {
        if (!get_u32_le(data.data(), data.size(), pos, p.thumbnail[i])) {
            return std::nullopt;
        }
    }

    return p;
}

} // namespace kairo::core
