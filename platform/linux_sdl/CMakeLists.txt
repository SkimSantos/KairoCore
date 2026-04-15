find_package(SDL2 REQUIRED)

add_executable(kairocore
    src/main.cpp
    src/sdl_app.cpp
    src/sdl_renderer.cpp
    src/sdl_audio.cpp
    src/sdl_input.cpp
    src/keybind_ui.cpp
    src/debug_overlay.cpp
)

target_include_directories(kairocore PRIVATE include)
target_link_libraries(kairocore PRIVATE
    kairo::core
    kairo::platform_shared
    SDL2::SDL2
)
