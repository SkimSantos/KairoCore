#pragma once

#include <cstdint>
#include <string>

#include "backend/cart.hpp"
#include "backend/cpu.hpp"
#include "backend/gpu.hpp"
#include "backend/io.hpp"
#include "backend/memory.hpp"

namespace kairo::backend {

class GbaSystem {
public:
    static constexpr int cycles_per_frame = kCyclesPerFrame; // 280896

    GbaSystem();

    bool load_rom(const std::string& path);
    bool has_rom() const { return cart_.loaded(); }
    const Cart& cart() const { return cart_; }
    std::uint64_t content_id() const { return cart_.content_id(); }

    void reset();

    // Advance the system by one frame (228 scanlines). Returns the
    // number of CPU cycles consumed.
    int run_frame();

    Cpu& cpu() { return cpu_; }
    const Cpu& cpu() const { return cpu_; }
    MemoryBus& bus() { return bus_; }
    const MemoryBus& bus() const { return bus_; }
    Io& io() { return io_; }
    const Io& io() const { return io_; }
    Gpu& gpu() { return gpu_; }
    const Gpu& gpu() const { return gpu_; }

    const std::array<std::uint32_t, Gpu::kPixelCount>& framebuffer() const {
        return gpu_.framebuffer();
    }

private:
    void patch_bios_stubs();
    void check_irq();
    void run_cpu(int cycles);

    Cart cart_;
    Io io_;
    MemoryBus bus_;
    Cpu cpu_;
    Gpu gpu_;
};

} // namespace kairo::backend
