cmake_minimum_required(VERSION 3.20)
project(KairoCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(KAIROCORE_BUILD_LINUX_SDL "Build the Linux SDL frontend" ON)
option(KAIROCORE_BUILD_TESTS "Build core unit tests" ON)

add_subdirectory(emu_backend)
add_subdirectory(core)
add_subdirectory(platform/shared)

if(KAIROCORE_BUILD_LINUX_SDL)
    add_subdirectory(platform/linux_sdl)
endif()

if(KAIROCORE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(core/tests)
endif()
