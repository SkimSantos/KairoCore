#include "emulator/emulator_instance.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <system_error>
#include <utility>

namespace kairo::core {

EmulatorInstance::EmulatorInstance(VideoSink* video_sink, AudioSink* audio_sink)
    : video_sink_(video_sink), audio_sink_(audio_sink) {}

bool EmulatorInstance::load_rom(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    // Phase 2: identify ROMs by path hash. Phase 3+ will hash the
    // actual ROM content so renaming a file doesn't break save states.
    rom_id_ = std::hash<std::string>{}(path);
    reset();
    return true;
}

void EmulatorInstance::reset() {
    // "reset" is a hardware reset of the cartridge — it clears game
    // state but not system-level settings (volume, fast-forward, bindings).
    frame_number_ = 0;
    current_input_ = {};
    video_buffer_ = {};
    latched_input_.reset();
}

void EmulatorInstance::run() { running_ = true; }
void EmulatorInstance::pause() { running_ = false; }
bool EmulatorInstance::is_running() const { return running_; }

void EmulatorInstance::set_input_state(const InputState& input) {
    current_input_ = input;
}

void EmulatorInstance::step_one_frame() {
    // Latched input is consumed on every step regardless of frame-step
    // mode, so a stray submit_input_for_next_frame() can't linger across
    // a mode toggle. Phase 3 backend will read the applied input.
    latched_input_.reset();

    ++frame_number_;
    render_test_pattern();
    video_buffer_.frame_number = frame_number_;

    if (video_sink_) {
        video_sink_->submit_frame(video_buffer_);
    }

    if (audio_sink_) {
        constexpr int samples_per_frame = AudioFrame::sample_rate / 60;
        AudioFrame audio;
        audio.samples.assign(
            static_cast<std::size_t>(samples_per_frame * AudioFrame::channels),
            std::int16_t{0});

        const bool muted =
            fast_forward_multiplier_ > fast_forward_audio_mute_threshold;
        const float gain = muted ? 0.0f : volume_;
        if (gain != 1.0f) {
            for (auto& s : audio.samples) {
                s = static_cast<std::int16_t>(static_cast<float>(s) * gain);
            }
        }

        audio_sink_->submit_audio(audio);
    }
}

std::uint64_t EmulatorInstance::frame_number() const { return frame_number_; }

std::chrono::nanoseconds EmulatorInstance::target_frame_duration() const {
    // GBA refresh rate: 59.7275 Hz → 16 743 035 ns per frame.
    constexpr std::int64_t base_ns = 16'743'035;
    return std::chrono::nanoseconds(base_ns / fast_forward_multiplier_);
}

bool EmulatorInstance::wants_frame_advance() const {
    return running_ && !frame_step_mode_;
}

void EmulatorInstance::set_fast_forward_multiplier(int multiplier) {
    fast_forward_multiplier_ =
        std::clamp(multiplier, min_fast_forward, max_fast_forward);
}

int EmulatorInstance::get_fast_forward_multiplier() const {
    return fast_forward_multiplier_;
}

void EmulatorInstance::set_frame_step_mode(bool enabled) {
    frame_step_mode_ = enabled;
    if (!enabled) {
        latched_input_.reset();
    }
}

bool EmulatorInstance::is_frame_step_mode() const { return frame_step_mode_; }

void EmulatorInstance::submit_input_for_next_frame(const InputState& input) {
    latched_input_ = input;
}

bool EmulatorInstance::has_latched_input() const {
    return latched_input_.has_value();
}

void EmulatorInstance::save_state_to_slot(int slot_id) {
    const auto now_ns = static_cast<std::uint64_t>(
        std::chrono::system_clock::now().time_since_epoch().count());

    SaveStatePayload payload;
    payload.version = kSaveStateVersion;
    payload.rom_id = rom_id_;
    payload.frame_number = frame_number_;
    payload.timestamp = now_ns;
    payload.input = current_input_;
    payload.thumbnail = capture_thumbnail();

    SaveStateSlot slot;
    slot.slot_id = slot_id;
    slot.timestamp = now_ns;
    slot.state_data = serialize_state(payload);
    slot.thumbnail = payload.thumbnail;

    save_slots_[slot_id] = std::move(slot);
}

bool EmulatorInstance::load_state_from_slot(int slot_id) {
    const auto it = save_slots_.find(slot_id);
    if (it == save_slots_.end()) {
        return false;
    }
    const auto payload = deserialize_state(it->second.state_data);
    if (!payload) {
        return false;
    }
    // Refuse to restore a state from a different ROM.
    if (payload->rom_id != rom_id_) {
        return false;
    }
    frame_number_ = payload->frame_number;
    current_input_ = payload->input;
    return true;
}

std::vector<SaveStateSlot> EmulatorInstance::list_save_slots() const {
    std::vector<SaveStateSlot> out;
    out.reserve(save_slots_.size());
    for (const auto& entry : save_slots_) {
        out.push_back(entry.second);
    }
    return out;
}

bool EmulatorInstance::sync_slots_to_dir(
    const std::filesystem::path& dir) const {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return false;

    bool all_ok = true;
    for (const auto& [id, slot] : save_slots_) {
        const auto path = dir / ("slot_" + std::to_string(id) + ".kstate");
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) {
            all_ok = false;
            continue;
        }
        f.write(reinterpret_cast<const char*>(slot.state_data.data()),
                static_cast<std::streamsize>(slot.state_data.size()));
        if (!f) all_ok = false;
    }
    return all_ok;
}

bool EmulatorInstance::load_slots_from_dir(
    const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) return false;
    if (!std::filesystem::is_directory(dir, ec) || ec) return false;

    save_slots_.clear();

    bool all_ok = true;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) return false;
        if (!entry.is_regular_file()) continue;
        const auto name = entry.path().filename().string();
        if (name.rfind("slot_", 0) != 0) continue;
        if (entry.path().extension() != ".kstate") continue;

        // Extract slot id from "slot_<id>.kstate".
        const auto stem = entry.path().stem().string();
        int slot_id = 0;
        try {
            slot_id = std::stoi(stem.substr(5));
        } catch (...) {
            all_ok = false;
            continue;
        }

        std::ifstream f(entry.path(), std::ios::binary);
        if (!f) { all_ok = false; continue; }
        std::vector<std::uint8_t> bytes(
            (std::istreambuf_iterator<char>(f)),
            std::istreambuf_iterator<char>());

        const auto payload = deserialize_state(bytes);
        if (!payload) { all_ok = false; continue; }
        // Skip states belonging to a different ROM — a shared save dir
        // across multiple ROMs would otherwise silently cross-load.
        if (rom_id_ != 0 && payload->rom_id != rom_id_) continue;

        SaveStateSlot slot;
        slot.slot_id = slot_id;
        slot.timestamp = payload->timestamp;
        slot.state_data = std::move(bytes);
        slot.thumbnail = payload->thumbnail;
        save_slots_[slot_id] = std::move(slot);
    }
    return all_ok;
}

std::uint64_t EmulatorInstance::rom_id() const { return rom_id_; }

void EmulatorInstance::set_volume(float normalized) {
    volume_ = std::clamp(normalized, 0.0f, 1.0f);
}

float EmulatorInstance::get_volume() const { return volume_; }

void EmulatorInstance::set_debug_mode(bool enabled) { debug_mode_ = enabled; }
bool EmulatorInstance::is_debug_mode() const { return debug_mode_; }

void EmulatorInstance::add_memory_watch(const std::string& name,
                                        std::uint32_t address,
                                        std::uint8_t byte_count) {
    // byte_count must be 1/2/4; clamp silently rather than reject.
    if (byte_count != 1 && byte_count != 2 && byte_count != 4) {
        byte_count = 4;
    }
    // Adding an address that already has a watch replaces it — callers
    // shouldn't have to manually remove-then-add when retitling a watch.
    for (auto& w : memory_watches_) {
        if (w.address == address) {
            w.name = name;
            w.byte_count = byte_count;
            return;
        }
    }
    memory_watches_.push_back(MemoryWatch{name, address, byte_count});
}

bool EmulatorInstance::remove_memory_watch(std::uint32_t address) {
    for (auto it = memory_watches_.begin(); it != memory_watches_.end(); ++it) {
        if (it->address == address) {
            memory_watches_.erase(it);
            return true;
        }
    }
    return false;
}

void EmulatorInstance::clear_memory_watches() { memory_watches_.clear(); }

std::vector<MemoryWatch> EmulatorInstance::list_memory_watches() const {
    return memory_watches_;
}

DebugSnapshot EmulatorInstance::capture_debug_snapshot() const {
    DebugSnapshot snap;
    snap.frame_number = frame_number_;

    // CPU registers: ARM7TDMI has r0..r15 + CPSR. The real backend will
    // fill these from the CPU core; for now we expose the shape so the
    // UI can be built against it.
    snap.cpu_registers.reserve(18);
    for (int i = 0; i < 16; ++i) {
        snap.cpu_registers.push_back(
            {"r" + std::to_string(i), format_hex(0, 8)});
    }
    snap.cpu_registers.push_back({"CPSR", format_hex(0, 8)});
    snap.cpu_registers.push_back(
        {"frame", std::to_string(frame_number_)});

    // Hardware registers: a few GBA MMIO regs the UI will want first.
    // Values are placeholders until the PPU/DMA/IRQ backends exist.
    snap.hardware_registers = {
        {"DISPCNT",  format_hex(0, 4)},
        {"DISPSTAT", format_hex(0, 4)},
        {"VCOUNT",   format_hex(0, 4)},
        {"IE",       format_hex(0, 4)},
        {"IF",       format_hex(0, 4)},
        {"IME",      format_hex(0, 4)},
    };

    // Memory watches: no bus yet, so show "??" as the unread value.
    // Preserving the user-configured name/address/size is what matters.
    snap.memory_watch.reserve(memory_watches_.size());
    for (const auto& w : memory_watches_) {
        DebugEntry e;
        e.name = w.name + " @ " + format_hex(w.address, 8) +
                 " [" + std::to_string(w.byte_count) + "b]";
        e.value = "??";
        snap.memory_watch.push_back(std::move(e));
    }

    return snap;
}

SaveStateThumbnail EmulatorInstance::capture_thumbnail() const {
    // Nearest-neighbor 4x downsample of the most recent video frame.
    // Thumbnail dims (60x40) are chosen to evenly divide 240x160.
    constexpr int scale = 4;
    static_assert(VideoFrame::width / scale == kSaveStateThumbnailWidth, "");
    static_assert(VideoFrame::height / scale == kSaveStateThumbnailHeight, "");

    SaveStateThumbnail out{};
    for (int y = 0; y < kSaveStateThumbnailHeight; ++y) {
        for (int x = 0; x < kSaveStateThumbnailWidth; ++x) {
            const int src_x = x * scale;
            const int src_y = y * scale;
            out[y * kSaveStateThumbnailWidth + x] =
                video_buffer_.pixels[src_y * VideoFrame::width + src_x];
        }
    }
    return out;
}

void EmulatorInstance::render_test_pattern() {
    const auto fn = static_cast<std::uint32_t>(frame_number_);
    for (int y = 0; y < VideoFrame::height; ++y) {
        for (int x = 0; x < VideoFrame::width; ++x) {
            const std::uint32_t r =
                static_cast<std::uint32_t>(x + static_cast<int>(fn)) & 0xFFu;
            const std::uint32_t g =
                static_cast<std::uint32_t>(y + static_cast<int>(fn)) & 0xFFu;
            const std::uint32_t b = fn & 0xFFu;
            video_buffer_.pixels[y * VideoFrame::width + x] =
                (0xFFu << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

} // namespace kairo::core
