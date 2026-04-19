#pragma once

#include <array>
#include <cstdint>

namespace kairo::backend {

class MemoryBus;

// GBA I/O register offsets from 0x04000000.
namespace io_reg {
    inline constexpr std::uint32_t DISPCNT   = 0x000;
    inline constexpr std::uint32_t GREENSWAP = 0x002;
    inline constexpr std::uint32_t DISPSTAT  = 0x004;
    inline constexpr std::uint32_t VCOUNT    = 0x006;

    inline constexpr std::uint32_t BG0CNT  = 0x008;
    inline constexpr std::uint32_t BG1CNT  = 0x00A;
    inline constexpr std::uint32_t BG2CNT  = 0x00C;
    inline constexpr std::uint32_t BG3CNT  = 0x00E;
    inline constexpr std::uint32_t BG0HOFS = 0x010;
    inline constexpr std::uint32_t BG0VOFS = 0x012;
    inline constexpr std::uint32_t BG1HOFS = 0x014;
    inline constexpr std::uint32_t BG1VOFS = 0x016;
    inline constexpr std::uint32_t BG2HOFS = 0x018;
    inline constexpr std::uint32_t BG2VOFS = 0x01A;
    inline constexpr std::uint32_t BG3HOFS = 0x01C;
    inline constexpr std::uint32_t BG3VOFS = 0x01E;
    inline constexpr std::uint32_t BG2PA   = 0x020;
    inline constexpr std::uint32_t BG2PB   = 0x022;
    inline constexpr std::uint32_t BG2PC   = 0x024;
    inline constexpr std::uint32_t BG2PD   = 0x026;
    inline constexpr std::uint32_t BG2X    = 0x028;
    inline constexpr std::uint32_t BG2Y    = 0x02C;
    inline constexpr std::uint32_t BG3PA   = 0x030;
    inline constexpr std::uint32_t BG3PB   = 0x032;
    inline constexpr std::uint32_t BG3PC   = 0x034;
    inline constexpr std::uint32_t BG3PD   = 0x036;
    inline constexpr std::uint32_t BG3X    = 0x038;
    inline constexpr std::uint32_t BG3Y    = 0x03C;

    inline constexpr std::uint32_t WIN0H   = 0x040;
    inline constexpr std::uint32_t WIN1H   = 0x042;
    inline constexpr std::uint32_t WIN0V   = 0x044;
    inline constexpr std::uint32_t WIN1V   = 0x046;
    inline constexpr std::uint32_t WININ   = 0x048;
    inline constexpr std::uint32_t WINOUT  = 0x04A;
    inline constexpr std::uint32_t MOSAIC  = 0x04C;
    inline constexpr std::uint32_t BLDCNT  = 0x050;
    inline constexpr std::uint32_t BLDALPHA= 0x052;
    inline constexpr std::uint32_t BLDY    = 0x054;

    inline constexpr std::uint32_t DMA0SAD  = 0x0B0;
    inline constexpr std::uint32_t DMA3CNT_H = 0x0DE;

    inline constexpr std::uint32_t TM0CNT_L = 0x100;
    inline constexpr std::uint32_t TM3CNT_H = 0x10E;

    inline constexpr std::uint32_t KEYINPUT = 0x130;
    inline constexpr std::uint32_t KEYCNT   = 0x132;

    inline constexpr std::uint32_t IE       = 0x200;
    inline constexpr std::uint32_t IF       = 0x202;
    inline constexpr std::uint32_t WAITCNT  = 0x204;
    inline constexpr std::uint32_t IME      = 0x208;
    inline constexpr std::uint32_t POSTFLG  = 0x300;
    inline constexpr std::uint32_t HALTCNT  = 0x301;
}

// DISPCNT bit fields.
namespace dispcnt {
    inline constexpr std::uint16_t MODE_MASK      = 0x0007;
    inline constexpr std::uint16_t FRAME_SELECT   = 1 << 4;
    inline constexpr std::uint16_t HBLANK_FREE    = 1 << 5;
    inline constexpr std::uint16_t OBJ_1D_MAPPING = 1 << 6;
    inline constexpr std::uint16_t FORCED_BLANK    = 1 << 7;
    inline constexpr std::uint16_t BG0_ENABLE     = 1 << 8;
    inline constexpr std::uint16_t BG1_ENABLE     = 1 << 9;
    inline constexpr std::uint16_t BG2_ENABLE     = 1 << 10;
    inline constexpr std::uint16_t BG3_ENABLE     = 1 << 11;
    inline constexpr std::uint16_t OBJ_ENABLE     = 1 << 12;
    inline constexpr std::uint16_t WIN0_ENABLE    = 1 << 13;
    inline constexpr std::uint16_t WIN1_ENABLE    = 1 << 14;
    inline constexpr std::uint16_t WINOBJ_ENABLE  = 1 << 15;
}

// IRQ bit positions (shared by IE and IF).
namespace irq {
    inline constexpr std::uint16_t VBLANK  = 1 << 0;
    inline constexpr std::uint16_t HBLANK  = 1 << 1;
    inline constexpr std::uint16_t VCOUNT  = 1 << 2;
    inline constexpr std::uint16_t TIMER0  = 1 << 3;
    inline constexpr std::uint16_t TIMER1  = 1 << 4;
    inline constexpr std::uint16_t TIMER2  = 1 << 5;
    inline constexpr std::uint16_t TIMER3  = 1 << 6;
    inline constexpr std::uint16_t SERIAL  = 1 << 7;
    inline constexpr std::uint16_t DMA0    = 1 << 8;
    inline constexpr std::uint16_t DMA1    = 1 << 9;
    inline constexpr std::uint16_t DMA2    = 1 << 10;
    inline constexpr std::uint16_t DMA3    = 1 << 11;
    inline constexpr std::uint16_t KEYPAD  = 1 << 12;
    inline constexpr std::uint16_t GAMEPAK = 1 << 13;
}

// PPU timing constants.
inline constexpr int kCyclesPerDot      = 4;
inline constexpr int kDotsPerScanline   = 308;   // 240 visible + 68 hblank
inline constexpr int kCyclesPerScanline = 1232;   // 308 × 4
inline constexpr int kVisibleLines      = 160;
inline constexpr int kTotalLines        = 228;
inline constexpr int kHBlankStart       = 240;    // dot at which hblank begins
inline constexpr int kCyclesHDraw       = kHBlankStart * kCyclesPerDot; // 960
inline constexpr int kCyclesPerFrame    = kCyclesPerScanline * kTotalLines;

// DMA channel state (4 channels).
struct DmaChannel {
    std::uint32_t sad = 0;
    std::uint32_t dad = 0;
    std::uint16_t count = 0;
    std::uint16_t control = 0;
    // Internal running addresses for repeat/timed transfers.
    std::uint32_t internal_sad = 0;
    std::uint32_t internal_dad = 0;
};

// Timer state (4 timers).
struct TimerChannel {
    std::uint16_t reload = 0;
    std::uint16_t counter = 0;
    std::uint16_t control = 0;
    int prescale_accum = 0;
};

class Io {
public:
    Io();
    void reset();

    // I/O read/write — called by MemoryBus for region 0x04.
    std::uint8_t  read8(std::uint32_t offset) const;
    std::uint16_t read16(std::uint32_t offset) const;
    std::uint32_t read32(std::uint32_t offset) const;
    void write8(std::uint32_t offset, std::uint8_t value);
    void write16(std::uint32_t offset, std::uint16_t value);
    void write32(std::uint32_t offset, std::uint32_t value);

    // PPU timing: advance by `cycles` and update VCOUNT/DISPSTAT.
    // Returns a mask of IRQs to raise (0 if none).
    std::uint16_t tick(int cycles);

    // Current scanline.
    int vcount() const { return vcount_; }

    // True during visible scanlines (0..159).
    bool in_vdraw() const { return vcount_ < kVisibleLines; }

    // DMA: run any pending immediate-mode transfers.
    // Called by GbaSystem after a DMA control write.
    void run_immediate_dma(MemoryBus& bus);

    // DMA: run channels configured for a specific timing trigger.
    // timing: 1 = VBLANK, 2 = HBLANK, 3 = special (audio FIFO, video capture).
    void run_timed_dma(MemoryBus& bus, int timing);

    // IRQ state (exposed so GbaSystem can check/fire IRQs).
    bool ime() const { return ime_; }
    std::uint16_t ie() const { return ie_; }
    std::uint16_t if_pending() const { return if_; }
    void raise_irq(std::uint16_t mask) { if_ |= mask; }
    void acknowledge_irq(std::uint16_t mask) { if_ &= ~mask; }

    // Raw register read from backing store (no special handling).
    std::uint16_t raw16(std::uint32_t offset) const {
        return static_cast<std::uint16_t>(raw_[offset]) |
               (static_cast<std::uint16_t>(raw_[offset + 1]) << 8);
    }
    std::uint32_t raw32(std::uint32_t offset) const {
        return static_cast<std::uint32_t>(raw16(offset)) |
               (static_cast<std::uint32_t>(raw16(offset + 2)) << 16);
    }

    std::uint16_t dispcnt() const { return raw16(io_reg::DISPCNT); }

    // Keypad input (directly set by the platform).
    void set_keyinput(std::uint16_t keys) { keyinput_ = keys; }

    bool halted() const { return halted_; }
    void set_halted(bool h) { halted_ = h; }

private:
    void write_io16(std::uint32_t offset, std::uint16_t value);
    std::uint16_t read_io16(std::uint32_t offset) const;

    // Raw backing store for registers without special handling.
    std::array<std::uint8_t, 0x400> raw_{};

    // PPU timing state.
    int vcount_ = 0;
    int scanline_cycles_ = 0;

    // IRQ controller.
    bool ime_ = false;
    std::uint16_t ie_ = 0;
    std::uint16_t if_ = 0;

    // DISPSTAT (bits 3-7 are R/W config, bits 0-2 are read-only status).
    std::uint16_t dispstat_cfg_ = 0;

    // DMA channels.
    std::array<DmaChannel, 4> dma_{};

    // Timers.
    std::array<TimerChannel, 4> timers_{};

    // Keypad: bits are active-low (0 = pressed).
    std::uint16_t keyinput_ = 0x03FF;

    // CPU halt state (set by HALTCNT write, cleared by IRQ).
    bool halted_ = false;
};

} // namespace kairo::backend
