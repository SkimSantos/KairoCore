#pragma once

#include <cstdint>
#include <string>

#include "emulator/audio_sink.hpp"
#include "emulator/input_state.hpp"
#include "emulator/video_frame.hpp"
#include "emulator/video_sink.hpp"

namespace kairo::core {

class EmulatorInstance {
public:
    EmulatorInstance(VideoSink* video_sink, AudioSink* audio_sink);

    bool load_rom(const std::string& path);
    void reset();

    void run();
    void pause();
    bool is_running() const;

    void set_input_state(const InputState& input);
    void step_one_frame();

    std::uint64_t frame_number() const;

private:
    void render_test_pattern();

    VideoSink* video_sink_;
    AudioSink* audio_sink_;
    bool running_ = false;
    std::uint64_t frame_number_ = 0;
    InputState current_input_{};
    VideoFrame video_buffer_{};
};

} // namespace kairo::core
