add_library(kairo_emu_backend STATIC
    src/cpu.cpp
    src/memory.cpp
    src/gpu.cpp
    src/apu.cpp
    src/dma.cpp
    src/timer.cpp
    src/registers.cpp
)

target_include_directories(kairo_emu_backend PUBLIC include)
add_library(kairo::emu_backend ALIAS kairo_emu_backend)
