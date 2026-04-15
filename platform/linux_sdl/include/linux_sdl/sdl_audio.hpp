#pragma once

#include <SDL2/SDL.h>

#include "emulator/audio_sink.hpp"

namespace kairo::linux_sdl {

class SdlAudio : public kairo::core::AudioSink {
public:
    SdlAudio();
    ~SdlAudio() override;

    SdlAudio(const SdlAudio&) = delete;
    SdlAudio& operator=(const SdlAudio&) = delete;

    void submit_audio(const kairo::core::AudioFrame& frame) override;

private:
    SDL_AudioDeviceID device_ = 0;
};

} // namespace kairo::linux_sdl
