#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "emulator/input_state.hpp"

namespace kairo::core {

// Thumbnails are a fixed-size downsample of the GBA frame (240x160 / 4).
// Kept fixed so the on-disk format stays simple; a future version can
// bump kSaveStateVersion and change the dimensions.
inline constexpr int kSaveStateThumbnailWidth = 60;
inline constexpr int kSaveStateThumbnailHeight = 40;
inline constexpr int kSaveStateThumbnailPixelCount =
    kSaveStateThumbnailWidth * kSaveStateThumbnailHeight;

using SaveStateThumbnail =
    std::array<std::uint32_t, kSaveStateThumbnailPixelCount>;

struct SaveStateSlot {
    int slot_id = 0;
    std::string label;
    std::uint64_t timestamp = 0;
    std::vector<std::uint8_t> state_data;
    SaveStateThumbnail thumbnail{};
};

struct SaveStatePayload {
    std::uint32_t version = 0;
    std::uint64_t rom_id = 0;
    std::uint64_t frame_number = 0;
    std::uint64_t timestamp = 0;
    InputState input{};
    SaveStateThumbnail thumbnail{};
};

inline constexpr std::uint32_t kSaveStateVersion = 2;
inline constexpr char kSaveStateMagic[8] = {'K','A','I','R','O','_','S','T'};

std::vector<std::uint8_t> serialize_state(const SaveStatePayload& payload);
std::optional<SaveStatePayload> deserialize_state(
    const std::vector<std::uint8_t>& data);

} // namespace kairo::core
