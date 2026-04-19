#include "backend/flash.hpp"
#include <algorithm>

namespace kairo::backend {

Flash128K::Flash128K() { reset(); }

void Flash128K::reset() {
    state_ = State::Ready;
    bank_ = 0;
    data_.fill(0xFF);
}

std::uint8_t Flash128K::read(std::uint32_t addr) const {
    addr &= 0xFFFF;
    if (state_ == State::IdMode) {
        if (addr == 0x0000) return kManufacturer;
        if (addr == 0x0001) return kDevice;
        return 0;
    }
    std::size_t offset = static_cast<std::size_t>(bank_) * kBankSize + addr;
    return data_[offset];
}

void Flash128K::write(std::uint32_t addr, std::uint8_t value) {
    addr &= 0xFFFF;

    switch (state_) {
        case State::Ready:
            if (addr == 0x5555 && value == 0xAA) state_ = State::Cmd1;
            break;

        case State::Cmd1:
            if (addr == 0x2AAA && value == 0x55)
                state_ = State::Cmd2;
            else
                state_ = State::Ready;
            break;

        case State::Cmd2:
            if (addr == 0x5555) {
                switch (value) {
                    case 0x90: state_ = State::IdMode; break;
                    case 0x80: state_ = State::Erase1; break;
                    case 0xA0: state_ = State::Write; break;
                    case 0xB0: state_ = State::BankSelect; break;
                    case 0xF0: state_ = State::Ready; break;
                    default:   state_ = State::Ready; break;
                }
            } else {
                state_ = State::Ready;
            }
            break;

        case State::IdMode:
            if (value == 0xF0) state_ = State::Ready;
            break;

        case State::Write: {
            std::size_t offset = static_cast<std::size_t>(bank_) * kBankSize + addr;
            data_[offset] = value;
            state_ = State::Ready;
            break;
        }

        case State::Erase1:
            if (addr == 0x5555 && value == 0xAA)
                state_ = State::Erase2;
            else
                state_ = State::Ready;
            break;

        case State::Erase2:
            if (addr == 0x2AAA && value == 0x55)
                state_ = State::Erase3;
            else
                state_ = State::Ready;
            break;

        case State::Erase3:
            if (addr == 0x5555 && value == 0x10) {
                // Chip erase
                data_.fill(0xFF);
            } else if (value == 0x30) {
                // Sector erase (4KB)
                std::size_t sector = (static_cast<std::size_t>(bank_) * kBankSize) +
                                     (addr & 0xF000u);
                std::fill_n(data_.begin() + sector, 0x1000, std::uint8_t{0xFF});
            }
            state_ = State::Ready;
            break;

        case State::BankSelect:
            if (addr == 0x0000) {
                bank_ = value & 1;
            }
            state_ = State::Ready;
            break;
    }
}

} // namespace kairo::backend
