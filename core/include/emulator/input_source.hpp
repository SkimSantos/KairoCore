#pragma once

#include "emulator/input_state.hpp"

namespace kairo::core {

class InputSource {
public:
    virtual ~InputSource() = default;
    virtual InputState poll() = 0;
};

} // namespace kairo::core
