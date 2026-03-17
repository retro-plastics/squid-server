add_executable(echo_sample
    ${CMAKE_CURRENT_SOURCE_DIR}/echo_sample.c
)

target_link_libraries(echo_sample PRIVATE squid_transport_local)

target_include_directories(echo_sample
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(echo_sample PRIVATE c_std_11)
target_compile_options(echo_sample PRIVATE -Wall -Wextra -Wpedantic)

set_target_properties(echo_sample PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${server_stage_bin_root}
)
