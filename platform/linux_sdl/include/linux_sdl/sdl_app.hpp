#pragma once

#include <memory>
#include <string>

#include <SDL2/SDL.h>

#include "emulator/emulator_instance.hpp"
#include "linux_sdl/sdl_audio.hpp"
#include "linux_sdl/sdl_input.hpp"
#include "linux_sdl/sdl_renderer.hpp"

namespace kairo::linux_sdl {

class SdlApp {
public:
    SdlApp();
    ~SdlApp();

    SdlApp(const SdlApp&) = delete;
    SdlApp& operator=(const SdlApp&) = delete;

    int run(const std::string& rom_path);

private:
    SDL_Window* window_ = nullptr;
    std::unique_ptr<SdlRenderer> renderer_;
    std::unique_ptr<SdlAudio> audio_;
    std::unique_ptr<SdlInput> input_;
    std::unique_ptr<kairo::core::EmulatorInstance> emulator_;
};

} // namespace kairo::linux_sdl
