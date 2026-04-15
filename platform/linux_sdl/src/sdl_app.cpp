#include "linux_sdl/sdl_app.hpp"

#include <stdexcept>
#include <string>

namespace kairo::linux_sdl {

namespace {
constexpr int kInitialWindowScale = 3;
constexpr int kInitialWindowWidth = kairo::core::VideoFrame::width * kInitialWindowScale;
constexpr int kInitialWindowHeight = kairo::core::VideoFrame::height * kInitialWindowScale;
} // namespace

SdlApp::SdlApp() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());
    }

    window_ = SDL_CreateWindow(
        "KairoCore",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kInitialWindowWidth, kInitialWindowHeight,
        SDL_WINDOW_RESIZABLE);
    if (!window_) {
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());
    }

    renderer_ = std::make_unique<SdlRenderer>(window_);
    audio_ = std::make_unique<SdlAudio>();
    input_ = std::make_unique<SdlInput>();
    emulator_ = std::make_unique<kairo::core::EmulatorInstance>(
        renderer_.get(), audio_.get());
}

SdlApp::~SdlApp() {
    emulator_.reset();
    input_.reset();
    audio_.reset();
    renderer_.reset();
    if (window_) {
        SDL_DestroyWindow(window_);
    }
    SDL_Quit();
}

int SdlApp::run(const std::string& rom_path) {
    if (!rom_path.empty() && !emulator_->load_rom(rom_path)) {
        SDL_Log("Failed to load ROM: %s", rom_path.c_str());
    }

    emulator_->run();

    bool quit = false;
    while (!quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                quit = true;
            } else if (ev.type == SDL_KEYDOWN) {
                const SDL_Keycode k = ev.key.keysym.sym;
                if (k == SDLK_ESCAPE) {
                    quit = true;
                } else if (k == SDLK_p) {
                    if (emulator_->is_running()) {
                        emulator_->pause();
                    } else {
                        emulator_->run();
                    }
                }
            }
        }

        emulator_->set_input_state(input_->poll());

        if (emulator_->is_running()) {
            emulator_->step_one_frame();
        } else {
            SDL_Delay(16);
        }
    }

    return 0;
}

} // namespace kairo::linux_sdl
