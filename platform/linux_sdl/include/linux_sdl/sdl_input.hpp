#pragma once

#include "emulator/input_source.hpp"

namespace kairo::linux_sdl {

class SdlInput : public kairo::core::InputSource {
public:
    kairo::core::InputState poll() override;
};

} // namespace kairo::linux_sdl
