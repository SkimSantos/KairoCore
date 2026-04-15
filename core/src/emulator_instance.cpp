#include "emulator/emulator_instance.hpp"

#include <fstream>

namespace kairo::core {

EmulatorInstance::EmulatorInstance(VideoSink* video_sink, AudioSink* audio_sink)
    : video_sink_(video_sink), audio_sink_(audio_sink) {}

bool EmulatorInstance::load_rom(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    reset();
    return true;
}

void EmulatorInstance::reset() {
    frame_number_ = 0;
    current_input_ = {};
    video_buffer_ = {};
}

void EmulatorInstance::run() { running_ = true; }
void EmulatorInstance::pause() { running_ = false; }
bool EmulatorInstance::is_running() const { return running_; }

void EmulatorInstance::set_input_state(const InputState& input) {
    current_input_ = input;
}

void EmulatorInstance::step_one_frame() {
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
            static_cast<std::size_t>(samples_per_frame * AudioFrame::channels), 0);
        audio_sink_->submit_audio(audio);
    }
}

std::uint64_t EmulatorInstance::frame_number() const { return frame_number_; }

void EmulatorInstance::render_test_pattern() {
    const auto fn = static_cast<std::uint32_t>(frame_number_);
    for (int y = 0; y < VideoFrame::height; ++y) {
        for (int x = 0; x < VideoFrame::width; ++x) {
            const std::uint32_t r = static_cast<std::uint32_t>(x + fn) & 0xFFu;
            const std::uint32_t g = static_cast<std::uint32_t>(y + fn) & 0xFFu;
            const std::uint32_t b = fn & 0xFFu;
            video_buffer_.pixels[y * VideoFrame::width + x] =
                (0xFFu << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

} // namespace kairo::core
