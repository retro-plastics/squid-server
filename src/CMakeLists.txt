add_library(squid_server_core)

target_link_libraries(squid_server_core PUBLIC squid)
target_link_libraries(squid_server_core PRIVATE ${CMAKE_DL_LIBS})

target_include_directories(
    squid_server_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(squid_server_core PUBLIC c_std_11)
target_compile_options(squid_server_core PRIVATE -Wall -Wextra -Wpedantic)

add_subdirectory(app)
add_subdirectory(config)
add_subdirectory(log)
add_subdirectory(plugin)
add_subdirectory(runtime)
add_subdirectory(transport)

file(GLOB src_root_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

add_executable(squid-server ${src_root_sources})

target_link_libraries(squid-server PRIVATE squid_server_core)

target_include_directories(
    squid-server
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

target_compile_options(squid-server PRIVATE -Wall -Wextra -Wpedantic)
