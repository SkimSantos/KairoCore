add_library(kairo_core STATIC
    src/emulator_core.cpp
    src/emulator_instance.cpp
    src/config.cpp
    src/save_state.cpp
    src/debug_snapshot.cpp
    src/fast_forward.cpp
    src/frame_step.cpp
    src/volume_control.cpp
)

target_include_directories(kairo_core PUBLIC include)
target_link_libraries(kairo_core PUBLIC kairo::emu_backend)
add_library(kairo::core ALIAS kairo_core)
