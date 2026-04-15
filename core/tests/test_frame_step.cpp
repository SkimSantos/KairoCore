#include <cassert>
#include <cstdint>
#include <vector>

#include "emulator/emulator_instance.hpp"
#include "emulator/input_state.hpp"
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
    using kairo::core::EmulatorInstance;
    using kairo::core::InputState;

    // Basic step counting (carried over from Phase 1).
    {
        RecordingVideoSink sink;
        EmulatorInstance emu(&sink, nullptr);

        assert(emu.frame_number() == 0);
        assert(sink.frame_numbers.empty());

        emu.step_one_frame();
        assert(emu.frame_number() == 1);
        assert(sink.frame_numbers.size() == 1);
        assert(sink.frame_numbers.back() == 1);

        for (int i = 0; i < 5; ++i) emu.step_one_frame();
        assert(emu.frame_number() == 6);
        assert(sink.frame_numbers.size() == 6);
        assert(sink.frame_numbers.back() == 6);

        emu.reset();
        assert(emu.frame_number() == 0);

        emu.step_one_frame();
        assert(emu.frame_number() == 1);
        assert(sink.frame_numbers.back() == 1);
    }

    // Frame-step mode: wants_frame_advance flips, step_one_frame still works.
    {
        EmulatorInstance emu(nullptr, nullptr);
        assert(!emu.is_frame_step_mode());

        emu.run();
        assert(emu.wants_frame_advance());

        emu.set_frame_step_mode(true);
        assert(emu.is_frame_step_mode());
        assert(!emu.wants_frame_advance());

        // Manual advance still works while frame-stepped.
        emu.step_one_frame();
        assert(emu.frame_number() == 1);
        emu.step_one_frame();
        assert(emu.frame_number() == 2);

        // Toggling off restores auto-advance.
        emu.set_frame_step_mode(false);
        assert(emu.wants_frame_advance());

        // Pause also disables auto-advance; re-enabling frame-step
        // keeps auto-advance off.
        emu.pause();
        assert(!emu.wants_frame_advance());
        emu.set_frame_step_mode(true);
        assert(!emu.wants_frame_advance());
    }

    // Latched input: submit → has_latched_input → step consumes it.
    {
        EmulatorInstance emu(nullptr, nullptr);
        emu.set_frame_step_mode(true);
        assert(!emu.has_latched_input());

        InputState in{};
        in.a = true;
        in.up = true;
        emu.submit_input_for_next_frame(in);
        assert(emu.has_latched_input());

        emu.step_one_frame();
        assert(!emu.has_latched_input());
        assert(emu.frame_number() == 1);

        // Can submit again for the next step.
        emu.submit_input_for_next_frame(in);
        assert(emu.has_latched_input());

        // Leaving frame-step mode drops any pending latched input so
        // it can't unexpectedly apply to a later auto-advance.
        emu.set_frame_step_mode(false);
        assert(!emu.has_latched_input());
    }

    return 0;
}
