#pragma once

#include "emulator/input_source.hpp"
#include "platform/input_mapper.hpp"

namespace kairo::linux_sdl {

class SdlInput : public kairo::core::InputSource {
public:
    SdlInput();
    kairo::core::InputState poll() override;

    kairo::platform::InputMapper& mapper() { return mapper_; }
    const kairo::platform::InputMapper& mapper() const { return mapper_; }

private:
    kairo::platform::InputMapper mapper_;
};

} // namespace kairo::linux_sdl
