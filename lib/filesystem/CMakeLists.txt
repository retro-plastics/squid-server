set(squid_filesystem_core_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/filesystem_core.c
)

add_library(filesystem SHARED
    ${squid_filesystem_core_sources}
    ${CMAKE_CURRENT_SOURCE_DIR}/filesystem_plugin.c
)

set_target_properties(
    filesystem
    PROPERTIES
        OUTPUT_NAME filesystem
        LIBRARY_OUTPUT_DIRECTORY ${server_stage_plugin_root}
)

target_include_directories(
    filesystem
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(filesystem PRIVATE c_std_11)
target_compile_options(filesystem PRIVATE -Wall -Wextra -Wpedantic)

add_executable(filesystem-tests
    ${squid_filesystem_core_sources}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_filesystem.c
)

target_include_directories(
    filesystem-tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(filesystem-tests PRIVATE c_std_11)
target_compile_options(filesystem-tests PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME filesystem_tests COMMAND filesystem-tests)
