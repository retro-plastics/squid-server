cmake_minimum_required(VERSION 3.16)

project(squid_server C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(server_stage_root ${CMAKE_SOURCE_DIR}/bin/opt/squid)
set(server_stage_bin_root ${server_stage_root}/bin)
set(server_stage_etc_root ${server_stage_root}/etc)
set(server_stage_include_root ${server_stage_root}/include/squid_server)
set(server_stage_plugin_root ${server_stage_root}/lib/plugins)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${server_stage_bin_root})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

file(MAKE_DIRECTORY ${server_stage_root})
file(MAKE_DIRECTORY ${server_stage_bin_root})
file(MAKE_DIRECTORY ${server_stage_etc_root})
file(MAKE_DIRECTORY ${server_stage_include_root})
file(MAKE_DIRECTORY ${server_stage_plugin_root})

configure_file(
    ${CMAKE_SOURCE_DIR}/opt/squid/etc/squid_server.conf
    ${server_stage_etc_root}/squid_server.conf
    COPYONLY
)

configure_file(
    ${CMAKE_SOURCE_DIR}/opt/squid/etc/squid_server_local.conf
    ${server_stage_etc_root}/squid_server_local.conf
    COPYONLY
)

file(GLOB server_public_headers CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/include/squid_server/*.h")

foreach(server_public_header IN LISTS server_public_headers)
    get_filename_component(server_public_header_name ${server_public_header} NAME)
    configure_file(
        ${server_public_header}
        ${server_stage_include_root}/${server_public_header_name}
        COPYONLY
    )
endforeach()

enable_testing()

add_subdirectory(lib)
add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(samples)
