#pragma once

#include "emulator/video_frame.hpp"

namespace kairo::core {

class VideoSink {
public:
    virtual ~VideoSink() = default;
    virtual void submit_frame(const VideoFrame& frame) = 0;
};

} // namespace kairo::core
