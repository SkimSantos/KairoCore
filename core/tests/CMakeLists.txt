foreach(test_name
    test_save_state
    test_input_mapping
    test_fast_forward
    test_frame_step)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE kairo::core)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
