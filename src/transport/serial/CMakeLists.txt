add_library(squid_transport_serial STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/serial_transport.c
)

target_link_libraries(squid_transport_serial PUBLIC squid)

target_include_directories(squid_transport_serial
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(squid_transport_serial PUBLIC c_std_11)
target_compile_options(squid_transport_serial PRIVATE -Wall -Wextra -Wpedantic)

# Make the serial transport available to the server core as well.
target_link_libraries(squid_server_core PUBLIC squid_transport_serial)
