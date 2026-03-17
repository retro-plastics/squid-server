file(GLOB squidsys_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

add_library(squidsys SHARED ${squidsys_sources})

set_target_properties(
    squidsys
    PROPERTIES
        OUTPUT_NAME squidsys
        LIBRARY_OUTPUT_DIRECTORY "${server_stage_plugin_root}"
    )

target_include_directories(
    squidsys
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(squidsys PUBLIC c_std_11)
target_compile_options(squidsys PRIVATE -Wall -Wextra -Wpedantic)
