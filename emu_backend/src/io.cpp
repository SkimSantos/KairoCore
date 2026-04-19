#include "backend/io.hpp"
#include "backend/memory.hpp"

namespace kairo::backend {

namespace {

std::uint16_t read16_le(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) |
           (static_cast<std::uint16_t>(p[1]) << 8);
}

void write16_le(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}

} // namespace

Io::Io() { reset(); }

void Io::reset() {
    raw_.fill(0);
    vcount_ = 0;
    scanline_cycles_ = 0;
    ime_ = false;
    ie_ = 0;
    if_ = 0;
    dispstat_cfg_ = 0;
    dma_ = {};
    timers_ = {};
    keyinput_ = 0x03FF;
    halted_ = false;
}

// ── Typed read/write for 16-bit I/O registers ────────────────────────

std::uint16_t Io::read_io16(std::uint32_t offset) const {
    switch (offset) {
        case io_reg::DISPSTAT: {
            // Bits 0-2: status flags derived from PPU state.
            std::uint16_t status = 0;
            if (vcount_ >= kVisibleLines && vcount_ < kTotalLines)
                status |= 1;   // VBLANK
            if (scanline_cycles_ >= kCyclesHDraw)
                status |= 2;   // HBLANK
            std::uint16_t match_target = (dispstat_cfg_ >> 8) & 0xFF;
            if (vcount_ == static_cast<int>(match_target))
                status |= 4;   // VCOUNT match
            return status | (dispstat_cfg_ & 0xFFF8);
        }
        case io_reg::VCOUNT:
            return static_cast<std::uint16_t>(vcount_);
        case io_reg::IE:
            return ie_;
        case io_reg::IF:
            return if_;
        case io_reg::IME:
            return ime_ ? 1u : 0u;
        case io_reg::KEYINPUT:
            return keyinput_;
        case io_reg::TM0CNT_L:     return timers_[0].counter;
        case io_reg::TM0CNT_L + 2: return timers_[0].control;
        case io_reg::TM0CNT_L + 4: return timers_[1].counter;
        case io_reg::TM0CNT_L + 6: return timers_[1].control;
        case io_reg::TM0CNT_L + 8: return timers_[2].counter;
        case io_reg::TM0CNT_L + 10: return timers_[2].control;
        case io_reg::TM0CNT_L + 12: return timers_[3].counter;
        case io_reg::TM0CNT_L + 14: return timers_[3].control;
        default:
            if (offset + 1 < raw_.size())
                return read16_le(&raw_[offset]);
            return 0;
    }
}

void Io::write_io16(std::uint32_t offset, std::uint16_t value) {
    switch (offset) {
        case io_reg::DISPSTAT:
            // Bits 3-7 and 8-15 are writable config; bits 0-2 are
            // read-only status.
            dispstat_cfg_ = value & 0xFFF8;
            break;
        case io_reg::VCOUNT:
            break; // read-only
        case io_reg::IE:
            ie_ = value;
            break;
        case io_reg::IF:
            // Write-1-to-acknowledge semantics.
            if_ &= ~value;
            break;
        case io_reg::IME:
            ime_ = (value & 1) != 0;
            break;
        case io_reg::HALTCNT:
            halted_ = true;
            break;
        default: {
            // DMA control writes: store raw AND flag for immediate transfer.
            bool is_dma_control = false;
            for (int ch = 0; ch < 4; ++ch) {
                std::uint32_t cnt_h = io_reg::DMA0SAD + static_cast<std::uint32_t>(ch) * 12 + 0xA;
                if (offset == cnt_h) {
                    bool was_enabled = (dma_[ch].control >> 15) & 1;
                    dma_[ch].control = value;
                    bool now_enabled = (value >> 15) & 1;
                    if (!was_enabled && now_enabled) {
                        dma_[ch].internal_sad = dma_[ch].sad;
                        dma_[ch].internal_dad = dma_[ch].dad;
                    }
                    is_dma_control = true;
                }
                std::uint32_t cnt_l = cnt_h - 2;
                if (offset == cnt_l) dma_[ch].count = value;
                std::uint32_t sad_lo = io_reg::DMA0SAD + static_cast<std::uint32_t>(ch) * 12;
                if (offset == sad_lo)
                    dma_[ch].sad = (dma_[ch].sad & 0xFFFF0000u) | value;
                if (offset == sad_lo + 2)
                    dma_[ch].sad = (dma_[ch].sad & 0x0000FFFFu) |
                                   (static_cast<std::uint32_t>(value) << 16);
                std::uint32_t dad_lo = sad_lo + 4;
                if (offset == dad_lo)
                    dma_[ch].dad = (dma_[ch].dad & 0xFFFF0000u) | value;
                if (offset == dad_lo + 2)
                    dma_[ch].dad = (dma_[ch].dad & 0x0000FFFFu) |
                                   (static_cast<std::uint32_t>(value) << 16);
            }

            // Timer control writes.
            for (int t = 0; t < 4; ++t) {
                std::uint32_t base = io_reg::TM0CNT_L + static_cast<std::uint32_t>(t) * 4;
                if (offset == base) timers_[t].reload = value;
                if (offset == base + 2) {
                    bool was_enabled = (timers_[t].control >> 7) & 1;
                    timers_[t].control = value;
                    bool now_enabled = (value >> 7) & 1;
                    if (!was_enabled && now_enabled)
                        timers_[t].counter = timers_[t].reload;
                }
            }

            if (!is_dma_control && offset + 1 < raw_.size())
                write16_le(&raw_[offset], value);
            break;
        }
    }
}

// ── Byte-wide I/O ────────────────────────────────────────────────────

std::uint8_t Io::read8(std::uint32_t offset) const {
    if (offset >= 0x400) return 0;
    std::uint16_t half = read_io16(offset & ~1u);
    return (offset & 1) ? static_cast<std::uint8_t>(half >> 8)
                        : static_cast<std::uint8_t>(half);
}

std::uint16_t Io::read16(std::uint32_t offset) const {
    return read_io16(offset & ~1u & 0x3FF);
}

std::uint32_t Io::read32(std::uint32_t offset) const {
    std::uint32_t lo = read_io16(offset & ~3u & 0x3FF);
    std::uint32_t hi = read_io16((offset & ~3u & 0x3FF) + 2);
    return lo | (hi << 16);
}

void Io::write8(std::uint32_t offset, std::uint8_t value) {
    if (offset >= 0x400) return;

    // HALTCNT is a byte-wide write-only register.
    if (offset == io_reg::HALTCNT) {
        halted_ = true;
        return;
    }

    // For most 16-bit registers, we must read-modify-write the pair.
    std::uint32_t aligned = offset & ~1u;
    std::uint16_t cur = read_io16(aligned);
    if (offset & 1)
        cur = (cur & 0x00FF) | (static_cast<std::uint16_t>(value) << 8);
    else
        cur = (cur & 0xFF00) | value;
    write_io16(aligned, cur);
}

void Io::write16(std::uint32_t offset, std::uint16_t value) {
    write_io16(offset & ~1u & 0x3FF, value);
}

void Io::write32(std::uint32_t offset, std::uint32_t value) {
    std::uint32_t base = offset & ~3u & 0x3FF;
    write_io16(base, static_cast<std::uint16_t>(value));
    write_io16(base + 2, static_cast<std::uint16_t>(value >> 16));
}

// ── PPU tick ─────────────────────────────────────────────────────────

std::uint16_t Io::tick(int cycles) {
    std::uint16_t raised = 0;
    scanline_cycles_ += cycles;

    while (scanline_cycles_ >= kCyclesPerScanline) {
        scanline_cycles_ -= kCyclesPerScanline;
        int prev = vcount_;
        vcount_ = (vcount_ + 1) % kTotalLines;

        // VBLANK start (entering line 160).
        if (prev == kVisibleLines - 1 && vcount_ == kVisibleLines) {
            raised |= irq::VBLANK;
        }

        // HBLANK fires at the end of every visible scanline draw period.
        if (dispstat_cfg_ & (1 << 4)) raised |= irq::HBLANK;

        // VCOUNT match.
        int match_target = (dispstat_cfg_ >> 8) & 0xFF;
        if (vcount_ == match_target) {
            if (dispstat_cfg_ & (1 << 5)) raised |= irq::VCOUNT;
        }
    }

    // Advance timers.
    static constexpr int kTimerPrescaler[4] = {1, 64, 256, 1024};
    for (int t = 0; t < 4; ++t) {
        if (!(timers_[t].control & (1 << 7))) continue;
        if (timers_[t].control & (1 << 2)) continue; // cascade

        int prescale = kTimerPrescaler[timers_[t].control & 3];
        timers_[t].prescale_accum += cycles;
        int ticks = timers_[t].prescale_accum / prescale;
        timers_[t].prescale_accum %= prescale;

        for (int k = 0; k < ticks; ++k) {
            if (++timers_[t].counter == 0) {
                timers_[t].counter = timers_[t].reload;
                if (timers_[t].control & (1 << 6))
                    raised |= static_cast<std::uint16_t>(irq::TIMER0 << t);
                for (int c = t + 1; c < 4; ++c) {
                    if (!(timers_[c].control & (1 << 7))) break;
                    if (!(timers_[c].control & (1 << 2))) break;
                    if (++timers_[c].counter == 0) {
                        timers_[c].counter = timers_[c].reload;
                        if (timers_[c].control & (1 << 6))
                            raised |= static_cast<std::uint16_t>(irq::TIMER0 << c);
                    } else {
                        break;
                    }
                }
            }
        }
    }

    return raised;
}

// ── DMA ──────────────────────────────────────────────────────────────

namespace {

void execute_dma_transfer(DmaChannel& dma, int ch, MemoryBus& bus,
                          std::uint32_t src, std::uint32_t dst,
                          std::uint16_t irq_mask, Io& io) {
    bool word32 = (dma.control >> 10) & 1;
    int dad_mode = (dma.control >> 5) & 3;
    int sad_mode = (dma.control >> 7) & 3;
    std::uint32_t count = dma.count;
    if (count == 0) count = (ch == 3) ? 0x10000u : 0x4000u;
    int step = word32 ? 4 : 2;

    for (std::uint32_t i = 0; i < count; ++i) {
        if (word32)
            bus.write32(dst, bus.read32(src));
        else
            bus.write16(dst, bus.read16(src));

        switch (sad_mode) {
            case 0: src += step; break;
            case 1: src -= step; break;
            case 2: break;
            default: break;
        }
        switch (dad_mode) {
            case 0: dst += step; break;
            case 1: dst -= step; break;
            case 2: break;
            case 3: dst += step; break;
        }
    }

    dma.internal_sad = src;
    dma.internal_dad = dst;

    if (dma.control & (1 << 14))
        io.raise_irq(irq_mask);

    if (!(dma.control & (1 << 9)))
        dma.control &= ~(1u << 15);
}

} // namespace

void Io::run_immediate_dma(MemoryBus& bus) {
    for (int ch = 0; ch < 4; ++ch) {
        auto& dma = dma_[ch];
        if (!(dma.control & (1 << 15))) continue;
        int timing = (dma.control >> 12) & 3;
        if (timing != 0) continue;

        dma.internal_sad = dma.sad;
        dma.internal_dad = dma.dad;
        execute_dma_transfer(dma, ch, bus, dma.sad, dma.dad,
                             static_cast<std::uint16_t>(irq::DMA0 << ch), *this);
    }
}

void Io::run_timed_dma(MemoryBus& bus, int timing) {
    for (int ch = 0; ch < 4; ++ch) {
        auto& dma = dma_[ch];
        if (!(dma.control & (1 << 15))) continue;
        int ch_timing = (dma.control >> 12) & 3;
        if (ch_timing != timing) continue;

        std::uint32_t src = dma.internal_sad;
        std::uint32_t dst = dma.internal_dad;

        // DAD mode 3 reloads destination on repeat triggers.
        int dad_mode = (dma.control >> 5) & 3;
        if (dad_mode == 3)
            dst = dma.dad;

        execute_dma_transfer(dma, ch, bus, src, dst,
                             static_cast<std::uint16_t>(irq::DMA0 << ch), *this);
    }
}

} // namespace kairo::backend
