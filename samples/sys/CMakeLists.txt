add_executable(sys_sample
    ${CMAKE_CURRENT_SOURCE_DIR}/sys_sample.c
)

target_link_libraries(sys_sample PRIVATE squid_transport_local)

target_include_directories(sys_sample
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(sys_sample PRIVATE c_std_11)
target_compile_options(sys_sample PRIVATE -Wall -Wextra -Wpedantic)

set_target_properties(sys_sample PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${server_stage_bin_root}
)
