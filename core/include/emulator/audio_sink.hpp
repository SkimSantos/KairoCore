#pragma once

#include "emulator/audio_frame.hpp"

namespace kairo::core {

class AudioSink {
public:
    virtual ~AudioSink() = default;
    virtual void submit_audio(const AudioFrame& frame) = 0;
};

} // namespace kairo::core
