#pragma once

#include <SDL2/SDL.h>

#include "emulator/video_sink.hpp"

namespace kairo::linux_sdl {

class SdlRenderer : public kairo::core::VideoSink {
public:
    explicit SdlRenderer(SDL_Window* window);
    ~SdlRenderer() override;

    SdlRenderer(const SdlRenderer&) = delete;
    SdlRenderer& operator=(const SdlRenderer&) = delete;

    void submit_frame(const kairo::core::VideoFrame& frame) override;

private:
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
};

} // namespace kairo::linux_sdl
