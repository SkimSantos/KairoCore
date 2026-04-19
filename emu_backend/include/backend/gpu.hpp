#pragma once

#include <array>
#include <cstdint>

namespace kairo::backend {

class Io;
class MemoryBus;

class Gpu {
public:
    static constexpr int kScreenWidth  = 240;
    static constexpr int kScreenHeight = 160;
    static constexpr int kPixelCount   = kScreenWidth * kScreenHeight;

    Gpu();

    void reset();

    // Render one scanline into the internal framebuffer.
    // Called by GbaSystem at each visible scanline (0..159).
    void render_scanline(int line, const Io& io, const MemoryBus& bus);

    // The completed framebuffer in ARGB8888 format.
    const std::array<std::uint32_t, kPixelCount>& framebuffer() const {
        return fb_;
    }

private:
    // Convert GBA BGR555 to ARGB8888.
    static std::uint32_t rgb555_to_argb(std::uint16_t c);

    // Layer rendering — each writes into line_buf_ with priority info.
    void render_text_bg(int bg, int line, const Io& io, const MemoryBus& bus);
    void render_affine_bg(int bg, int line, const Io& io, const MemoryBus& bus);
    void render_bitmap_mode3(int line, const MemoryBus& bus);
    void render_bitmap_mode4(int line, const Io& io, const MemoryBus& bus);
    void render_bitmap_mode5(int line, const Io& io, const MemoryBus& bus);
    void render_sprites(int line, const Io& io, const MemoryBus& bus);

    // Windowing: returns a bitmask of which layers are visible at (x, line).
    std::uint8_t window_mask(int x, int line, std::uint16_t dc, const Io& io) const;

    // Compositing: merge layers by priority into the scanline.
    void composite_scanline(int line, std::uint16_t dc, const Io& io,
                            const MemoryBus& bus);

    // Per-pixel entry for priority sorting.
    struct PixelEntry {
        std::uint32_t color = 0;   // ARGB8888
        std::uint8_t  priority = 5; // 0=highest, 3=lowest, 5=not drawn
    };

    // 4 BG layers + 1 OBJ layer, each 240 pixels wide.
    static constexpr int kLayerCount = 5; // BG0..BG3 + OBJ
    std::array<std::array<PixelEntry, kScreenWidth>, kLayerCount> layers_{};

    std::array<std::uint32_t, kPixelCount> fb_{};
};

} // namespace kairo::backend
