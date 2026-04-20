#include "backend/gpu.hpp"
#include "backend/io.hpp"
#include "backend/memory.hpp"

#include <cstring>

namespace kairo::backend {

Gpu::Gpu() { reset(); }

void Gpu::reset() {
    fb_.fill(0xFF000000u);
    for (auto& layer : layers_)
        for (auto& p : layer) p = {};
}

std::uint32_t Gpu::rgb555_to_argb(std::uint16_t c) {
    std::uint32_t r = (c & 0x1F) << 3;
    std::uint32_t g = ((c >> 5) & 0x1F) << 3;
    std::uint32_t b = ((c >> 10) & 0x1F) << 3;
    r |= r >> 5;
    g |= g >> 5;
    b |= b >> 5;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

// ── Helpers ──────────────────────────────────────────────────────────

namespace {

inline std::uint16_t read_vram16(const MemoryBus& bus, std::uint32_t offset) {
    const auto& v = bus.vram();
    offset &= 0x1FFFFu; // 128KB mirror
    if (offset >= 0x18000u) offset -= 0x8000u; // 96KB wrap
    return static_cast<std::uint16_t>(v[offset]) |
           (static_cast<std::uint16_t>(v[offset + 1]) << 8);
}

inline std::uint8_t read_vram8(const MemoryBus& bus, std::uint32_t offset) {
    const auto& v = bus.vram();
    offset &= 0x1FFFFu;
    if (offset >= 0x18000u) offset -= 0x8000u;
    return v[offset];
}

inline std::uint16_t read_palette16(const MemoryBus& bus, std::uint32_t index) {
    const auto& p = bus.palette();
    std::uint32_t off = (index * 2) & 0x3FFu;
    return static_cast<std::uint16_t>(p[off]) |
           (static_cast<std::uint16_t>(p[off + 1]) << 8);
}

inline std::uint16_t read_oam16(const MemoryBus& bus, std::uint32_t offset) {
    const auto& o = bus.oam();
    offset &= 0x3FFu;
    return static_cast<std::uint16_t>(o[offset]) |
           (static_cast<std::uint16_t>(o[offset + 1]) << 8);
}

} // namespace

// ── Text background (Mode 0/1) ──────────────────────────────────────

void Gpu::render_text_bg(int bg, int line, const Io& io, const MemoryBus& bus) {
    auto& layer = layers_[bg];
    for (auto& p : layer) p = {};

    std::uint16_t bgcnt = io.raw16(io_reg::BG0CNT + bg * 2);
    std::uint8_t  priority   = bgcnt & 3;
    std::uint32_t tile_base  = ((bgcnt >> 2) & 3) * 0x4000;
    bool          mosaic     = (bgcnt >> 6) & 1;
    bool          color_256  = (bgcnt >> 7) & 1;
    std::uint32_t map_base   = ((bgcnt >> 8) & 0x1F) * 0x800;
    std::uint8_t  screen_size = (bgcnt >> 14) & 3;
    (void)mosaic;

    int map_w = (screen_size & 1) ? 512 : 256;
    int map_h = (screen_size & 2) ? 512 : 256;

    std::uint16_t hofs = io.raw16(io_reg::BG0HOFS + bg * 4) & 0x1FF;
    std::uint16_t vofs = io.raw16(io_reg::BG0VOFS + bg * 4) & 0x1FF;

    int y = (line + vofs) % map_h;
    int tile_row = y / 8;
    int pixel_y  = y % 8;

    for (int screen_x = 0; screen_x < kScreenWidth; ++screen_x) {
        int x = (screen_x + hofs) % map_w;
        int tile_col = x / 8;
        int pixel_x  = x % 8;

        // Determine which 32x32-tile screen block we're in.
        int block = 0;
        int local_row = tile_row;
        int local_col = tile_col;
        if (map_w == 512 && tile_col >= 32) {
            block += 1;
            local_col -= 32;
        }
        if (map_h == 512 && tile_row >= 32) {
            block += (map_w == 512) ? 2 : 1;
            local_row -= 32;
        }

        std::uint32_t entry_addr = map_base + block * 0x800 +
                                   (local_row * 32 + local_col) * 2;
        std::uint16_t entry = read_vram16(bus, entry_addr);

        std::uint16_t tile_num = entry & 0x3FF;
        bool hflip  = (entry >> 10) & 1;
        bool vflip  = (entry >> 11) & 1;
        std::uint8_t pal_bank = (entry >> 12) & 0xF;

        int tx = hflip ? (7 - pixel_x) : pixel_x;
        int ty = vflip ? (7 - pixel_y) : pixel_y;

        std::uint8_t color_idx;
        if (color_256) {
            std::uint32_t addr = tile_base + tile_num * 64 + ty * 8 + tx;
            color_idx = read_vram8(bus, addr);
            if (color_idx == 0) continue;
            layer[screen_x].color = rgb555_to_argb(read_palette16(bus, color_idx));
        } else {
            std::uint32_t addr = tile_base + tile_num * 32 + ty * 4 + tx / 2;
            std::uint8_t byte = read_vram8(bus, addr);
            color_idx = (tx & 1) ? (byte >> 4) : (byte & 0xF);
            if (color_idx == 0) continue;
            std::uint16_t pal_index = pal_bank * 16 + color_idx;
            layer[screen_x].color = rgb555_to_argb(read_palette16(bus, pal_index));
        }
        layer[screen_x].priority = priority;
    }
}

// ── Affine background (Mode 1/2) ────────────────────────────────────

void Gpu::render_affine_bg(int bg, int line, const Io& io, const MemoryBus& bus) {
    auto& layer = layers_[bg];
    for (auto& p : layer) p = {};

    std::uint16_t bgcnt = io.raw16(io_reg::BG0CNT + bg * 2);
    std::uint8_t  priority  = bgcnt & 3;
    std::uint32_t tile_base = ((bgcnt >> 2) & 3) * 0x4000;
    std::uint32_t map_base  = ((bgcnt >> 8) & 0x1F) * 0x800;
    std::uint8_t  size_bits = (bgcnt >> 14) & 3;
    bool wraparound = (bgcnt >> 13) & 1;

    int map_size = 128 << size_bits; // 128, 256, 512, 1024
    int tiles_per_row = map_size / 8;

    std::uint32_t pa_off = (bg == 2) ? io_reg::BG2PA : io_reg::BG3PA;
    std::uint32_t ref_off = (bg == 2) ? io_reg::BG2X : io_reg::BG3X;

    auto pa = static_cast<std::int16_t>(io.raw16(pa_off));
    auto pb = static_cast<std::int16_t>(io.raw16(pa_off + 2));
    auto pc = static_cast<std::int16_t>(io.raw16(pa_off + 4));
    auto pd = static_cast<std::int16_t>(io.raw16(pa_off + 6));

    // Reference point is 28-bit signed fixed-point (8 fractional bits).
    auto ref_x = static_cast<std::int32_t>(io.raw32(ref_off));
    auto ref_y = static_cast<std::int32_t>(io.raw32(ref_off + 4));
    if (ref_x & 0x08000000) ref_x |= static_cast<std::int32_t>(0xF0000000u);
    if (ref_y & 0x08000000) ref_y |= static_cast<std::int32_t>(0xF0000000u);

    std::int32_t cx = ref_x + pb * line;
    std::int32_t cy = ref_y + pd * line;

    for (int screen_x = 0; screen_x < kScreenWidth; ++screen_x) {
        int tex_x = cx >> 8;
        int tex_y = cy >> 8;
        cx += pa;
        cy += pc;

        if (wraparound) {
            tex_x = ((tex_x % map_size) + map_size) % map_size;
            tex_y = ((tex_y % map_size) + map_size) % map_size;
        } else {
            if (tex_x < 0 || tex_x >= map_size ||
                tex_y < 0 || tex_y >= map_size) continue;
        }

        int tile_col = tex_x / 8;
        int tile_row = tex_y / 8;
        int px = tex_x % 8;
        int py = tex_y % 8;

        std::uint32_t map_addr = map_base + tile_row * tiles_per_row + tile_col;
        std::uint8_t tile_num = read_vram8(bus, map_addr);

        std::uint32_t tile_addr = tile_base + tile_num * 64 + py * 8 + px;
        std::uint8_t color_idx = read_vram8(bus, tile_addr);
        if (color_idx == 0) continue;

        layer[screen_x].color = rgb555_to_argb(read_palette16(bus, color_idx));
        layer[screen_x].priority = priority;
    }
}

// ── Bitmap modes ─────────────────────────────────────────────────────

void Gpu::render_bitmap_mode3(int line, const MemoryBus& bus) {
    auto& layer = layers_[2];
    for (auto& p : layer) p = {};

    for (int x = 0; x < kScreenWidth; ++x) {
        std::uint32_t addr = (line * kScreenWidth + x) * 2;
        std::uint16_t pixel = read_vram16(bus, addr);
        layer[x].color = rgb555_to_argb(pixel);
        layer[x].priority = 0;
    }
}

void Gpu::render_bitmap_mode4(int line, const Io& io, const MemoryBus& bus) {
    auto& layer = layers_[2];
    for (auto& p : layer) p = {};

    std::uint32_t base = (io.dispcnt() & dispcnt::FRAME_SELECT) ? 0xA000u : 0u;
    for (int x = 0; x < kScreenWidth; ++x) {
        std::uint32_t addr = base + line * kScreenWidth + x;
        std::uint8_t idx = read_vram8(bus, addr);
        if (idx == 0) continue;
        layer[x].color = rgb555_to_argb(read_palette16(bus, idx));
        layer[x].priority = 0;
    }
}

void Gpu::render_bitmap_mode5(int line, const Io& io, const MemoryBus& bus) {
    auto& layer = layers_[2];
    for (auto& p : layer) p = {};

    if (line >= 128) return;
    std::uint32_t base = (io.dispcnt() & dispcnt::FRAME_SELECT) ? 0xA000u : 0u;
    int width = 160;
    for (int x = 0; x < width && x < kScreenWidth; ++x) {
        std::uint32_t addr = base + (line * width + x) * 2;
        std::uint16_t pixel = read_vram16(bus, addr);
        layer[x].color = rgb555_to_argb(pixel);
        layer[x].priority = 0;
    }
}

// ── Sprites (OBJ) ───────────────────────────────────────────────────

void Gpu::render_sprites(int line, const Io& io, const MemoryBus& bus) {
    auto& layer = layers_[4]; // OBJ layer
    for (auto& p : layer) p = {};

    std::uint16_t dc = io.dispcnt();
    bool obj_1d = (dc & dispcnt::OBJ_1D_MAPPING) != 0;

    // OAM has 128 entries, each 8 bytes (3 attributes + rotation data).
    // Process in reverse order so lower-numbered sprites have higher priority.
    for (int i = 127; i >= 0; --i) {
        std::uint32_t oam_off = i * 8;
        std::uint16_t attr0 = read_oam16(bus, oam_off);
        std::uint16_t attr1 = read_oam16(bus, oam_off + 2);
        std::uint16_t attr2 = read_oam16(bus, oam_off + 4);

        std::uint8_t obj_mode = (attr0 >> 8) & 3;
        if (obj_mode == 2) continue; // hidden

        bool is_affine = (attr0 >> 8) & 1;
        bool double_size = is_affine && ((attr0 >> 9) & 1);

        std::uint8_t shape = (attr0 >> 14) & 3;
        std::uint8_t size_bits = (attr1 >> 14) & 3;

        // Sprite dimensions lookup table [shape][size].
        static constexpr int kWidths[3][4]  = {
            {8, 16, 32, 64}, // square
            {16, 32, 32, 64}, // horizontal
            {8, 8, 16, 32},  // vertical
        };
        static constexpr int kHeights[3][4] = {
            {8, 16, 32, 64},
            {8, 8, 16, 32},
            {16, 32, 32, 64},
        };
        if (shape == 3) continue;

        int w = kWidths[shape][size_bits];
        int h = kHeights[shape][size_bits];
        int render_w = double_size ? w * 2 : w;
        int render_h = double_size ? h * 2 : h;

        int obj_y = attr0 & 0xFF;
        if (obj_y >= 160) obj_y -= 256;
        int obj_x = attr1 & 0x1FF;
        if (obj_x >= 240) obj_x -= 512;

        // Check if this sprite is on the current scanline.
        int local_y = line - obj_y;
        if (local_y < 0 || local_y >= render_h) continue;

        bool color_256 = (attr0 >> 13) & 1;
        std::uint16_t tile_num = attr2 & 0x3FF;
        std::uint8_t  priority = (attr2 >> 10) & 3;
        std::uint8_t  pal_bank = (attr2 >> 12) & 0xF;

        bool hflip = false, vflip = false;
        if (!is_affine) {
            hflip = (attr1 >> 12) & 1;
            vflip = (attr1 >> 13) & 1;
        }

        // For non-affine sprites:
        int tex_y = local_y;
        if (double_size) tex_y -= h / 2;
        if (!is_affine && vflip) tex_y = h - 1 - tex_y;

        if (tex_y < 0 || tex_y >= h) {
            if (!is_affine) continue;
        }

        // Affine parameters.
        std::int16_t pa_s = 0x100, pb_s = 0, pc_s = 0, pd_s = 0x100;
        if (is_affine) {
            int rot_group = (attr1 >> 9) & 0x1F;
            std::uint32_t rot_off = rot_group * 32;
            pa_s = static_cast<std::int16_t>(read_oam16(bus, rot_off + 6));
            pb_s = static_cast<std::int16_t>(read_oam16(bus, rot_off + 14));
            pc_s = static_cast<std::int16_t>(read_oam16(bus, rot_off + 22));
            pd_s = static_cast<std::int16_t>(read_oam16(bus, rot_off + 30));
        }

        int half_w = render_w / 2;
        int half_h = render_h / 2;

        for (int sx = 0; sx < render_w; ++sx) {
            int px = obj_x + sx;
            if (px < 0 || px >= kScreenWidth) continue;

            int tex_px, tex_py;
            if (is_affine) {
                int dx = sx - half_w;
                int dy = local_y - half_h;
                tex_px = ((pa_s * dx + pb_s * dy) >> 8) + w / 2;
                tex_py = ((pc_s * dx + pd_s * dy) >> 8) + h / 2;
                if (tex_px < 0 || tex_px >= w || tex_py < 0 || tex_py >= h)
                    continue;
            } else {
                tex_px = hflip ? (w - 1 - sx) : sx;
                tex_py = tex_y;
            }

            std::uint8_t color_idx;
            // OBJ tiles start at 0x10000 in VRAM.
            if (color_256) {
                int tile_row_offset = tex_py / 8;
                int tile_col_offset = tex_px / 8;
                int in_tile_y = tex_py % 8;
                int in_tile_x = tex_px % 8;
                std::uint32_t t;
                if (obj_1d) {
                    t = tile_num + tile_row_offset * (w / 8) * 2 + tile_col_offset * 2;
                } else {
                    t = tile_num + tile_row_offset * 32 + tile_col_offset * 2;
                }
                std::uint32_t addr = 0x10000 + t * 32 + in_tile_y * 8 + in_tile_x;
                color_idx = read_vram8(bus, addr);
                if (color_idx == 0) continue;
                layer[px].color = rgb555_to_argb(read_palette16(bus, 256 + color_idx));
            } else {
                int tile_row_offset = tex_py / 8;
                int tile_col_offset = tex_px / 8;
                int in_tile_y = tex_py % 8;
                int in_tile_x = tex_px % 8;
                std::uint32_t t;
                if (obj_1d) {
                    t = tile_num + tile_row_offset * (w / 8) + tile_col_offset;
                } else {
                    t = tile_num + tile_row_offset * 32 + tile_col_offset;
                }
                std::uint32_t addr = 0x10000 + t * 32 + in_tile_y * 4 + in_tile_x / 2;
                std::uint8_t byte = read_vram8(bus, addr);
                color_idx = (in_tile_x & 1) ? (byte >> 4) : (byte & 0xF);
                if (color_idx == 0) continue;
                std::uint16_t pal_index = 256 + pal_bank * 16 + color_idx;
                layer[px].color = rgb555_to_argb(read_palette16(bus, pal_index));
            }
            layer[px].priority = priority;
        }
    }
}

// ── Windowing helpers ────────────────────────────────────────────────

namespace {

inline bool in_window_range(int coord, int lo, int hi) {
    if (lo <= hi)
        return coord >= lo && coord < hi;
    return coord >= lo || coord < hi;
}

} // namespace

// Returns a 6-bit mask of which layers/effects are enabled for pixel (x, line).
// Bits: 0=BG0, 1=BG1, 2=BG2, 3=BG3, 4=OBJ, 5=ColorFX
std::uint8_t Gpu::window_mask(int x, int line, std::uint16_t dc, const Io& io) const {
    bool win0_on = dc & dispcnt::WIN0_ENABLE;
    bool win1_on = dc & dispcnt::WIN1_ENABLE;
    bool obj_win = dc & dispcnt::WINOBJ_ENABLE;

    if (!win0_on && !win1_on && !obj_win)
        return 0x3F; // no windowing → everything visible

    if (win0_on) {
        std::uint16_t h = io.raw16(io_reg::WIN0H);
        std::uint16_t v = io.raw16(io_reg::WIN0V);
        int x1 = h >> 8, x2 = h & 0xFF;
        int y1 = v >> 8, y2 = v & 0xFF;
        if (in_window_range(x, x1, x2) &&
            in_window_range(line, y1, y2)) {
            return io.raw16(io_reg::WININ) & 0x3F;
        }
    }

    if (win1_on) {
        std::uint16_t h = io.raw16(io_reg::WIN1H);
        std::uint16_t v = io.raw16(io_reg::WIN1V);
        int x1 = h >> 8, x2 = h & 0xFF;
        int y1 = v >> 8, y2 = v & 0xFF;
        if (in_window_range(x, x1, x2) &&
            in_window_range(line, y1, y2)) {
            return (io.raw16(io_reg::WININ) >> 8) & 0x3F;
        }
    }

    // TODO: OBJ window (requires per-pixel OBJ window flag from sprite renderer)

    // Outside all windows
    return io.raw16(io_reg::WINOUT) & 0x3F;
}

// ── Compositing ──────────────────────────────────────────────────────

void Gpu::composite_scanline(int line, std::uint16_t dc, const Io& io,
                             const MemoryBus& bus) {
    std::uint32_t* out = &fb_[line * kScreenWidth];
    std::uint32_t backdrop = rgb555_to_argb(read_palette16(bus, 0));

    bool use_windows = (dc & (dispcnt::WIN0_ENABLE | dispcnt::WIN1_ENABLE |
                              dispcnt::WINOBJ_ENABLE)) != 0;

    std::uint16_t bldcnt   = io.raw16(io_reg::BLDCNT);
    std::uint16_t bldalpha = io.raw16(io_reg::BLDALPHA);
    std::uint16_t bldy_reg = io.raw16(io_reg::BLDY);

    int blend_mode = (bldcnt >> 6) & 3;
    int eva = bldalpha & 0x1F;
    int evb = (bldalpha >> 8) & 0x1F;
    int evy = bldy_reg & 0x1F;
    if (eva > 16) eva = 16;
    if (evb > 16) evb = 16;
    if (evy > 16) evy = 16;

    for (int x = 0; x < kScreenWidth; ++x) {
        std::uint8_t wmask = use_windows ? window_mask(x, line, dc, io) : 0x3F;

        // Find top two layers by priority for alpha blending.
        // layer_id: 0-3 = BG0-BG3, 4 = OBJ, 5 = backdrop
        std::uint32_t top_color = backdrop;
        std::uint8_t  top_prio = 5;
        int           top_id = 5;

        std::uint32_t bot_color = backdrop;
        int           bot_id = 5;

        // Check OBJ layer
        if (wmask & (1 << 4)) {
            const auto& obj = layers_[4][x];
            if (obj.priority < top_prio) {
                top_prio = obj.priority;
                top_color = obj.color;
                top_id = 4;
            }
        }

        // Check BG layers 0-3
        for (int bg = 0; bg < 4; ++bg) {
            if (!(wmask & (1 << bg))) continue;
            const auto& pe = layers_[bg][x];
            if (pe.priority < top_prio) {
                bot_color = top_color;
                bot_id = top_id;
                top_prio = pe.priority;
                top_color = pe.color;
                top_id = bg;
            } else if (pe.priority < 5) {
                bot_color = pe.color;
                bot_id = bg;
            }
        }

        // If OBJ was not the winner, it could be the second layer.
        if (top_id != 4 && (wmask & (1 << 4))) {
            const auto& obj = layers_[4][x];
            if (obj.priority < 5 && obj.priority >= top_prio) {
                bot_color = obj.color;
                bot_id = 4;
            }
        }

        bool bld_enabled = (wmask & (1 << 5)) != 0;
        bool top_is_1st = (bldcnt & (1 << top_id)) != 0;
        bool bot_is_2nd = (bldcnt & (1 << (8 + bot_id))) != 0;

        std::uint32_t color = top_color;

        if (bld_enabled && top_is_1st) {
            if (blend_mode == 1 && bot_is_2nd) {
                int r1 = (top_color >> 16) & 0xFF, g1 = (top_color >> 8) & 0xFF, b1 = top_color & 0xFF;
                int r2 = (bot_color >> 16) & 0xFF, g2 = (bot_color >> 8) & 0xFF, b2 = bot_color & 0xFF;
                int r = (r1 * eva + r2 * evb) / 16; if (r > 255) r = 255;
                int g = (g1 * eva + g2 * evb) / 16; if (g > 255) g = 255;
                int b = (b1 * eva + b2 * evb) / 16; if (b > 255) b = 255;
                color = 0xFF000000u | (static_cast<std::uint32_t>(r) << 16) |
                        (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
            } else if (blend_mode == 2) {
                int r = (top_color >> 16) & 0xFF, g = (top_color >> 8) & 0xFF, b = top_color & 0xFF;
                r += (255 - r) * evy / 16;
                g += (255 - g) * evy / 16;
                b += (255 - b) * evy / 16;
                color = 0xFF000000u | (static_cast<std::uint32_t>(r) << 16) |
                        (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
            } else if (blend_mode == 3) {
                int r = (top_color >> 16) & 0xFF, g = (top_color >> 8) & 0xFF, b = top_color & 0xFF;
                r -= r * evy / 16;
                g -= g * evy / 16;
                b -= b * evy / 16;
                color = 0xFF000000u | (static_cast<std::uint32_t>(r) << 16) |
                        (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
            }
        }

        out[x] = color;
    }
}

// ── Main entry point ─────────────────────────────────────────────────

void Gpu::render_scanline(int line, const Io& io, const MemoryBus& bus) {
    if (line < 0 || line >= kScreenHeight) return;

    // Clear all layers for this scanline.
    for (auto& layer : layers_)
        for (auto& p : layer) p = {};

    std::uint16_t dc = io.dispcnt();

    if (dc & dispcnt::FORCED_BLANK) {
        std::uint32_t* out = &fb_[line * kScreenWidth];
        for (int x = 0; x < kScreenWidth; ++x)
            out[x] = 0xFFFFFFFFu; // white
        return;
    }

    int mode = dc & dispcnt::MODE_MASK;

    switch (mode) {
        case 0:
            if (dc & dispcnt::BG0_ENABLE) render_text_bg(0, line, io, bus);
            if (dc & dispcnt::BG1_ENABLE) render_text_bg(1, line, io, bus);
            if (dc & dispcnt::BG2_ENABLE) render_text_bg(2, line, io, bus);
            if (dc & dispcnt::BG3_ENABLE) render_text_bg(3, line, io, bus);
            break;
        case 1:
            if (dc & dispcnt::BG0_ENABLE) render_text_bg(0, line, io, bus);
            if (dc & dispcnt::BG1_ENABLE) render_text_bg(1, line, io, bus);
            if (dc & dispcnt::BG2_ENABLE) render_affine_bg(2, line, io, bus);
            break;
        case 2:
            if (dc & dispcnt::BG2_ENABLE) render_affine_bg(2, line, io, bus);
            if (dc & dispcnt::BG3_ENABLE) render_affine_bg(3, line, io, bus);
            break;
        case 3:
            if (dc & dispcnt::BG2_ENABLE) render_bitmap_mode3(line, bus);
            break;
        case 4:
            if (dc & dispcnt::BG2_ENABLE) render_bitmap_mode4(line, io, bus);
            break;
        case 5:
            if (dc & dispcnt::BG2_ENABLE) render_bitmap_mode5(line, io, bus);
            break;
        default:
            break;
    }

    if (dc & dispcnt::OBJ_ENABLE) render_sprites(line, io, bus);

    composite_scanline(line, dc, io, bus);
}

} // namespace kairo::backend
