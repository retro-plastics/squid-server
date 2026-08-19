set(squid_tcp_proxy_core_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp_proxy_core.c
)

add_library(tcp_proxy SHARED
    ${squid_tcp_proxy_core_sources}
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp_proxy_plugin.c
)

set_target_properties(
    tcp_proxy
    PROPERTIES
        OUTPUT_NAME tcp_proxy
        LIBRARY_OUTPUT_DIRECTORY ${server_stage_plugin_root}
)

target_include_directories(
    tcp_proxy
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(tcp_proxy PRIVATE c_std_11)
target_compile_options(tcp_proxy PRIVATE -Wall -Wextra -Wpedantic)

add_executable(tcp-proxy-tests
    ${squid_tcp_proxy_core_sources}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tcp_proxy.c
)

target_include_directories(
    tcp-proxy-tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(tcp-proxy-tests PRIVATE c_std_11)
target_compile_options(tcp-proxy-tests PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME tcp_proxy_tests COMMAND tcp-proxy-tests)
