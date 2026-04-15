add_library(kairo_platform_shared STATIC
    src/input_mapper.cpp
    src/logger.cpp
)

target_include_directories(kairo_platform_shared PUBLIC include)
target_link_libraries(kairo_platform_shared PUBLIC kairo::core)
add_library(kairo::platform_shared ALIAS kairo_platform_shared)
