set(CORE_TESTS
    test_save_state
    test_input_mapping
    test_fast_forward
    test_frame_step
    test_debug_snapshot
    test_game_profile
    test_cart_header
    test_memory_bus
    test_emulator_lifecycle
    test_cpu)

foreach(test_name ${CORE_TESTS})
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE kairo::core)
    # Tests rely on assert(); keep it live even under Release builds.
    target_compile_options(${test_name} PRIVATE -UNDEBUG)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# test_input_mapping and test_game_profile exercise platform-shared
# code and need the extra link dependency.
target_link_libraries(test_input_mapping PRIVATE kairo::platform_shared)
target_link_libraries(test_game_profile PRIVATE kairo::platform_shared)
