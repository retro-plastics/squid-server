file(GLOB echo_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

add_library(echo SHARED ${echo_sources})

set_target_properties(
    echo
    PROPERTIES
        OUTPUT_NAME echo
        LIBRARY_OUTPUT_DIRECTORY "${server_stage_plugin_root}"
    )

target_include_directories(
    echo
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(echo PUBLIC c_std_11)
target_compile_options(echo PRIVATE -Wall -Wextra -Wpedantic)
