add_library(kairo_emu_backend STATIC
    src/cart.cpp
    src/cpu.cpp
    src/cpu_arm.cpp
    src/cpu_thumb.cpp
    src/memory.cpp
    src/gpu.cpp
    src/apu.cpp
    src/dma.cpp
    src/timer.cpp
    src/registers.cpp
    src/io.cpp
    src/hle_bios.cpp
    src/gba_system.cpp
    src/flash.cpp
)

target_include_directories(kairo_emu_backend PUBLIC include)
add_library(kairo::emu_backend ALIAS kairo_emu_backend)
