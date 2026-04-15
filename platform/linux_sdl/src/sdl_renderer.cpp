#include "linux_sdl/sdl_renderer.hpp"

#include <stdexcept>
#include <string>

namespace kairo::linux_sdl {

SdlRenderer::SdlRenderer(SDL_Window* window) {
    renderer_ = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        throw std::runtime_error(std::string("SDL_CreateRenderer: ") + SDL_GetError());
    }

    texture_ = SDL_CreateTexture(
        renderer_,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        kairo::core::VideoFrame::width,
        kairo::core::VideoFrame::height);
    if (!texture_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
        throw std::runtime_error(std::string("SDL_CreateTexture: ") + SDL_GetError());
    }

    SDL_RenderSetLogicalSize(
        renderer_, kairo::core::VideoFrame::width, kairo::core::VideoFrame::height);
}

SdlRenderer::~SdlRenderer() {
    if (texture_) {
        SDL_DestroyTexture(texture_);
    }
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
    }
}

void SdlRenderer::submit_frame(const kairo::core::VideoFrame& frame) {
    SDL_UpdateTexture(
        texture_,
        nullptr,
        frame.pixels.data(),
        kairo::core::VideoFrame::width * static_cast<int>(sizeof(std::uint32_t)));
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}

} // namespace kairo::linux_sdl
