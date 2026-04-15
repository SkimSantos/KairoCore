#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "emulator/audio_sink.hpp"
#include "emulator/debug_snapshot.hpp"
#include "emulator/input_state.hpp"
#include "emulator/save_state.hpp"
#include "emulator/video_frame.hpp"
#include "emulator/video_sink.hpp"

namespace kairo::core {

class EmulatorInstance {
public:
    static constexpr int min_fast_forward = 1;
    static constexpr int max_fast_forward = 16;
    // Audio is muted when emulation runs faster than this multiplier;
    // above this speed, sound is noise rather than useful feedback.
    static constexpr int fast_forward_audio_mute_threshold = 4;

    EmulatorInstance(VideoSink* video_sink, AudioSink* audio_sink);

    // Lifecycle
    bool load_rom(const std::string& path);
    void reset();
    void run();
    void pause();
    bool is_running() const;

    // Input
    void set_input_state(const InputState& input);

    // Frame execution
    void step_one_frame();
    std::uint64_t frame_number() const;
    std::chrono::nanoseconds target_frame_duration() const;
    bool wants_frame_advance() const;

    // Fast-forward (clamped to [min_fast_forward, max_fast_forward])
    void set_fast_forward_multiplier(int multiplier);
    int get_fast_forward_multiplier() const;

    // Frame-step mode
    void set_frame_step_mode(bool enabled);
    bool is_frame_step_mode() const;
    void submit_input_for_next_frame(const InputState& input);
    bool has_latched_input() const;

    // Save states (in-memory slots + optional disk persistence)
    void save_state_to_slot(int slot_id);
    bool load_state_from_slot(int slot_id);
    std::vector<SaveStateSlot> list_save_slots() const;

    // Phase 4: persistence. `sync_slots_to_dir` writes every in-memory
    // slot to `<dir>/slot_<id>.kstate`, overwriting existing files.
    // `load_slots_from_dir` reads every `slot_*.kstate` in `dir`,
    // replacing the in-memory slot table; states whose rom_id doesn't
    // match the currently loaded ROM are skipped. Both return `false`
    // on any filesystem error — partial success is possible either way.
    bool sync_slots_to_dir(const std::filesystem::path& dir) const;
    bool load_slots_from_dir(const std::filesystem::path& dir);

    // Identifier of the currently loaded ROM (0 before load_rom).
    std::uint64_t rom_id() const;

    // Volume (clamped to [0, 1])
    void set_volume(float normalized);
    float get_volume() const;

    // Debug (Phase 3). Debug mode is purely a hint to the frontend that
    // inspection UI should be visible; capture_debug_snapshot() works
    // regardless of the flag so a frontend can poll silently if it wants.
    void set_debug_mode(bool enabled);
    bool is_debug_mode() const;

    void add_memory_watch(const std::string& name,
                          std::uint32_t address,
                          std::uint8_t byte_count);
    bool remove_memory_watch(std::uint32_t address);
    void clear_memory_watches();
    std::vector<MemoryWatch> list_memory_watches() const;

    DebugSnapshot capture_debug_snapshot() const;

private:
    void render_test_pattern();
    SaveStateThumbnail capture_thumbnail() const;

    VideoSink* video_sink_;
    AudioSink* audio_sink_;
    bool running_ = false;
    std::uint64_t frame_number_ = 0;
    InputState current_input_{};
    VideoFrame video_buffer_{};

    int fast_forward_multiplier_ = 1;
    bool frame_step_mode_ = false;
    std::optional<InputState> latched_input_;
    float volume_ = 1.0f;
    std::uint64_t rom_id_ = 0;
    std::map<int, SaveStateSlot> save_slots_;

    bool debug_mode_ = false;
    std::vector<MemoryWatch> memory_watches_;
};

} // namespace kairo::core
