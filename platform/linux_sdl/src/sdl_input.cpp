#include "linux_sdl/sdl_input.hpp"

#include <SDL2/SDL.h>

namespace kairo::linux_sdl {

kairo::core::InputState SdlInput::poll() {
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    kairo::core::InputState s{};
    s.up     = keys[SDL_SCANCODE_UP];
    s.down   = keys[SDL_SCANCODE_DOWN];
    s.left   = keys[SDL_SCANCODE_LEFT];
    s.right  = keys[SDL_SCANCODE_RIGHT];
    s.a      = keys[SDL_SCANCODE_Z];
    s.b      = keys[SDL_SCANCODE_X];
    s.l      = keys[SDL_SCANCODE_A];
    s.r      = keys[SDL_SCANCODE_S];
    s.start  = keys[SDL_SCANCODE_RETURN];
    s.select = keys[SDL_SCANCODE_BACKSPACE];
    return s;
}

} // namespace kairo::linux_sdl
