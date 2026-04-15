#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

#include "emulator/audio_sink.hpp"
#include "emulator/emulator_instance.hpp"
#include "emulator/input_state.hpp"
#include "emulator/video_sink.hpp"

namespace {

class CountingVideoSink : public kairo::core::VideoSink {
public:
    void submit_frame(const kairo::core::VideoFrame& frame) override {
        ++count;
        last_frame_number = frame.frame_number;
    }
    int count = 0;
    std::uint64_t last_frame_number = 0;
};

class CountingAudioSink : public kairo::core::AudioSink {
public:
    void submit_audio(const kairo::core::AudioFrame& frame) override {
        ++count;
        last_sample_count = frame.samples.size();
    }
    int count = 0;
    std::size_t last_sample_count = 0;
};

std::string make_temp_rom() {
    char path_buf[] = "/tmp/kairocore_test_rom_XXXXXX";
    const int fd = mkstemp(path_buf);
    assert(fd >= 0);
    ::close(fd);
    const std::string path(path_buf);
    std::ofstream out(path, std::ios::binary);
    out.write("fake rom", 8);
    return path;
}

} // namespace

int main() {
    using kairo::core::EmulatorInstance;
    using kairo::core::InputState;

    // Default state.
    {
        EmulatorInstance emu(nullptr, nullptr);
        assert(!emu.is_running());
        assert(emu.frame_number() == 0);
    }

    // run / pause toggling, idempotent calls.
    {
        EmulatorInstance emu(nullptr, nullptr);
        emu.run();
        assert(emu.is_running());
        emu.run();
        assert(emu.is_running());
        emu.pause();
        assert(!emu.is_running());
        emu.pause();
        assert(!emu.is_running());
        emu.run();
        assert(emu.is_running());
    }

    // Stepping pushes both video and non-empty audio when sinks exist.
    {
        CountingVideoSink vsink;
        CountingAudioSink asink;
        EmulatorInstance emu(&vsink, &asink);
        for (int i = 0; i < 10; ++i) {
            emu.step_one_frame();
        }
        assert(vsink.count == 10);
        assert(asink.count == 10);
        assert(vsink.last_frame_number == 10);
        assert(asink.last_sample_count > 0);
        assert(emu.frame_number() == 10);
    }

    // Stepping with both sinks null must not crash.
    {
        EmulatorInstance emu(nullptr, nullptr);
        for (int i = 0; i < 5; ++i) {
            emu.step_one_frame();
        }
        assert(emu.frame_number() == 5);
    }

    // Only-video sink: stepping should not require an audio sink.
    {
        CountingVideoSink vsink;
        EmulatorInstance emu(&vsink, nullptr);
        emu.step_one_frame();
        assert(vsink.count == 1);
        assert(emu.frame_number() == 1);
    }

    // load_rom on a missing path returns false and does not reset state.
    {
        EmulatorInstance emu(nullptr, nullptr);
        emu.step_one_frame();
        assert(emu.frame_number() == 1);
        assert(!emu.load_rom("/this/path/should/not/exist/kairo.gba"));
        assert(emu.frame_number() == 1);
    }

    // load_rom on a real file returns true and resets the frame counter.
    {
        const auto path = make_temp_rom();
        EmulatorInstance emu(nullptr, nullptr);
        emu.step_one_frame();
        emu.step_one_frame();
        assert(emu.frame_number() == 2);
        assert(emu.load_rom(path));
        assert(emu.frame_number() == 0);
        std::remove(path.c_str());
    }

    // set_input_state accepts arbitrary states without affecting frame counter.
    {
        EmulatorInstance emu(nullptr, nullptr);
        InputState in{};
        in.a = true;
        in.b = true;
        in.up = true;
        in.start = true;
        emu.set_input_state(in);
        emu.step_one_frame();
        emu.set_input_state(InputState{});
        emu.step_one_frame();
        assert(emu.frame_number() == 2);
    }

    // reset() zeros the frame counter and stepping resumes from 1.
    {
        CountingVideoSink vsink;
        EmulatorInstance emu(&vsink, nullptr);
        for (int i = 0; i < 7; ++i) {
            emu.step_one_frame();
        }
        assert(emu.frame_number() == 7);
        emu.reset();
        assert(emu.frame_number() == 0);
        emu.step_one_frame();
        assert(emu.frame_number() == 1);
        assert(vsink.last_frame_number == 1);
    }

    return 0;
}
