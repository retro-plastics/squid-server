set(squid_time_core_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/time_core.c
)

add_library(time_plugin SHARED
    ${squid_time_core_sources}
    ${CMAKE_CURRENT_SOURCE_DIR}/time_plugin.c
)

set_target_properties(
    time_plugin
    PROPERTIES
        OUTPUT_NAME time
        LIBRARY_OUTPUT_DIRECTORY ${server_stage_plugin_root}
)

target_include_directories(
    time_plugin
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(time_plugin PRIVATE c_std_11)
target_compile_options(time_plugin PRIVATE -Wall -Wextra -Wpedantic)

add_executable(time-tests
    ${squid_time_core_sources}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_time.c
)

target_include_directories(
    time-tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(time-tests PRIVATE c_std_11)
target_compile_options(time-tests PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME time_tests COMMAND time-tests)
