#pragma once

#include <array>
#include <cstdint>

namespace kairo::backend {

class Flash128K {
public:
    static constexpr std::size_t kSize = 128 * 1024;
    static constexpr std::size_t kBankSize = 64 * 1024;

    Flash128K();

    void reset();

    std::uint8_t read(std::uint32_t addr) const;
    void write(std::uint32_t addr, std::uint8_t value);

    const std::array<std::uint8_t, kSize>& data() const { return data_; }
    std::array<std::uint8_t, kSize>& data() { return data_; }

private:
    enum class State {
        Ready,
        Cmd1,    // 0xAA written to 0x5555
        Cmd2,    // 0x55 written to 0x2AAA
        IdMode,
        Write,
        Erase1,  // 0x80 received, waiting for 0xAA
        Erase2,  // 0xAA received at 0x5555
        Erase3,  // 0x55 received at 0x2AAA
        BankSelect,
    };

    static constexpr std::uint8_t kManufacturer = 0x62; // Sanyo
    static constexpr std::uint8_t kDevice = 0x13;       // LE39FW512

    State state_ = State::Ready;
    int bank_ = 0;
    std::array<std::uint8_t, kSize> data_{};
};

} // namespace kairo::backend
