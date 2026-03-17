file(GLOB test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

add_executable(squid-server-tests ${test_sources})

target_link_libraries(squid-server-tests PRIVATE squid_server_core)
target_include_directories(
    squid-server-tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
)

target_compile_options(squid-server-tests PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME squid_server_tests COMMAND squid-server-tests)
