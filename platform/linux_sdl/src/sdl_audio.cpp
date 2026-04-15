#include "linux_sdl/sdl_audio.hpp"

#include <stdexcept>
#include <string>

namespace kairo::linux_sdl {

SdlAudio::SdlAudio() {
    SDL_AudioSpec want{};
    want.freq = kairo::core::AudioFrame::sample_rate;
    want.format = AUDIO_S16SYS;
    want.channels = kairo::core::AudioFrame::channels;
    want.samples = 1024;

    SDL_AudioSpec have{};
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (device_ == 0) {
        throw std::runtime_error(std::string("SDL_OpenAudioDevice: ") + SDL_GetError());
    }
    SDL_PauseAudioDevice(device_, 0);
}

SdlAudio::~SdlAudio() {
    if (device_ != 0) {
        SDL_CloseAudioDevice(device_);
    }
}

void SdlAudio::submit_audio(const kairo::core::AudioFrame& frame) {
    if (device_ == 0 || frame.samples.empty()) {
        return;
    }
    SDL_QueueAudio(
        device_,
        frame.samples.data(),
        static_cast<Uint32>(frame.samples.size() * sizeof(std::int16_t)));
}

} // namespace kairo::linux_sdl
