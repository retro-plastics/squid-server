set(squid_client_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/portable/client.c
    ${CMAKE_CURRENT_SOURCE_DIR}/portable/internal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/echo.c
    ${CMAKE_CURRENT_SOURCE_DIR}/filesystem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/retrovault.c
    ${CMAKE_CURRENT_SOURCE_DIR}/system.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp_proxy.c
    ${CMAKE_CURRENT_SOURCE_DIR}/time.c
)

add_library(squid_client STATIC ${squid_client_sources})
target_link_libraries(squid_client PUBLIC squid)
target_include_directories(
    squid_client
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_features(squid_client PUBLIC c_std_11)
target_compile_options(squid_client PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(
    squid_client
    PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${server_stage_root}/lib/client/portable
)

add_executable(
    squid-client-service-tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_services.c
)
target_link_libraries(squid-client-service-tests PRIVATE squid_client)
target_compile_options(squid-client-service-tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME squid_client_service_tests COMMAND squid-client-service-tests)

add_executable(
    squid-client-exchange-tests
    ${CMAKE_CURRENT_SOURCE_DIR}/portable/client.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_exchange.c
)
target_include_directories(
    squid-client-exchange-tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${libsquid_SOURCE_DIR}/include
)
target_compile_options(squid-client-exchange-tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME squid_client_exchange_tests COMMAND squid-client-exchange-tests)

# Optional cross-builds. They are deliberately outside the default target so
# a normal Linux server build does not require Docker or either xcc image.
set(SQUID_CLIENT_PARTNER_IMAGE "wischner/xcc-z80-idp:2.3.2" CACHE STRING
    "Docker image used for the Partner Z80 client library"
)
set(SQUID_CLIENT_SPECTRUM_IMAGE "wischner/xcc-z80-zx-spectrum:2.3.2" CACHE STRING
    "Docker image used for the Spectrum Z80 client library"
)

add_custom_target(
    squid-client-z80-partner
    COMMAND sh lib/client/scripts/run-z80-docker.sh
        "${SQUID_CLIENT_PARTNER_IMAGE}"
        cpm3
        build/client/z80/partner
        bin/opt/squid/lib/client/z80/partner
        1
        "${libsquid_SOURCE_DIR}"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    VERBATIM
)

add_custom_target(
    squid-client-z80-spectrum
    COMMAND sh lib/client/scripts/run-z80-docker.sh
        "${SQUID_CLIENT_SPECTRUM_IMAGE}"
        zx-ram
        build/client/z80/spectrum
        bin/opt/squid/lib/client/z80/spectrum
        0
        "${libsquid_SOURCE_DIR}"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    VERBATIM
)

add_custom_target(squid-client-z80)
add_dependencies(
    squid-client-z80
    squid-client-z80-partner
    squid-client-z80-spectrum
)
