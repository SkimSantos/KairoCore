#include "linux_sdl/sdl_input.hpp"

#include <SDL2/SDL.h>

namespace kairo::linux_sdl {

SdlInput::SdlInput() {
    using kairo::platform::InputAction;
    mapper_.set_binding(InputAction::up,     SDL_SCANCODE_W);
    mapper_.set_binding(InputAction::down,   SDL_SCANCODE_S);
    mapper_.set_binding(InputAction::left,   SDL_SCANCODE_A);
    mapper_.set_binding(InputAction::right,  SDL_SCANCODE_D);
    mapper_.set_binding(InputAction::a,      SDL_SCANCODE_Z);
    mapper_.set_binding(InputAction::b,      SDL_SCANCODE_X);
    mapper_.set_binding(InputAction::l,      SDL_SCANCODE_Q);
    mapper_.set_binding(InputAction::r,      SDL_SCANCODE_E);
    mapper_.set_binding(InputAction::start,  SDL_SCANCODE_RETURN);
    mapper_.set_binding(InputAction::select, SDL_SCANCODE_BACKSPACE);
}

kairo::core::InputState SdlInput::poll() {
    int numkeys = 0;
    const Uint8* keys = SDL_GetKeyboardState(&numkeys);
    return mapper_.build_input_state(keys, numkeys);
}

} // namespace kairo::linux_sdl
