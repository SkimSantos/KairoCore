#include <cassert>
#include <cstdint>
#include <vector>

#include "emulator/emulator_instance.hpp"
#include "emulator/video_frame.hpp"
#include "emulator/video_sink.hpp"

namespace {

class RecordingVideoSink : public kairo::core::VideoSink {
public:
    void submit_frame(const kairo::core::VideoFrame& frame) override {
        frame_numbers.push_back(frame.frame_number);
    }
    std::vector<std::uint64_t> frame_numbers;
};

} // namespace

int main() {
    RecordingVideoSink sink;
    kairo::core::EmulatorInstance emu(&sink, nullptr);

    assert(emu.frame_number() == 0);
    assert(sink.frame_numbers.empty());

    emu.step_one_frame();
    assert(emu.frame_number() == 1);
    assert(sink.frame_numbers.size() == 1);
    assert(sink.frame_numbers.back() == 1);

    for (int i = 0; i < 5; ++i) {
        emu.step_one_frame();
    }
    assert(emu.frame_number() == 6);
    assert(sink.frame_numbers.size() == 6);
    assert(sink.frame_numbers.back() == 6);

    emu.reset();
    assert(emu.frame_number() == 0);

    emu.step_one_frame();
    assert(emu.frame_number() == 1);
    assert(sink.frame_numbers.back() == 1);

    return 0;
}
