include(FetchContent)

set(LIBSQUID_SOURCE_DIR "" CACHE PATH
    "Use a local libsquid source tree instead of downloading a release"
)
set(LIBSQUID_GIT_TAG
    "50039278d4f74f24891e6e78166f2cee96f3a316"
    CACHE STRING
    "libsquid revision to fetch when no local source tree is available"
)

if(LIBSQUID_SOURCE_DIR STREQUAL "")
    get_filename_component(libsquid_sibling_dir
        "${CMAKE_SOURCE_DIR}/../libsquid" ABSOLUTE
    )
    if(EXISTS "${libsquid_sibling_dir}/include/squid/snet.h")
        set(LIBSQUID_SOURCE_DIR "${libsquid_sibling_dir}")
    endif()
endif()

if(NOT LIBSQUID_SOURCE_DIR STREQUAL "")
    if(NOT EXISTS "${LIBSQUID_SOURCE_DIR}/include/squid/snet.h")
        message(FATAL_ERROR
            "LIBSQUID_SOURCE_DIR is not a libsquid source tree: "
            "${LIBSQUID_SOURCE_DIR}"
        )
    endif()
    get_filename_component(libsquid_SOURCE_DIR
        "${LIBSQUID_SOURCE_DIR}" ABSOLUTE
    )
    message(STATUS "Using local libsquid source: ${libsquid_SOURCE_DIR}")
else()
    message(STATUS "Using libsquid revision: ${LIBSQUID_GIT_TAG}")

    FetchContent_Declare(
        libsquid
        GIT_REPOSITORY https://github.com/retro-plastics/libsquid.git
        GIT_TAG "${LIBSQUID_GIT_TAG}"
        GIT_SHALLOW TRUE
    )

    FetchContent_GetProperties(libsquid)

    if(NOT libsquid_POPULATED)
        FetchContent_Populate(libsquid)
    endif()
endif()

file(GLOB libsquid_sources CONFIGURE_DEPENDS
    "${libsquid_SOURCE_DIR}/lib/squid/portable/*.c"
)

if(NOT libsquid_sources)
    message(FATAL_ERROR
        "The selected libsquid source does not provide the wire-v2 portable backend"
    )
endif()

# Stage the matching public libsquid headers with the client SDK. The client
# header for Spectrum includes squid/snet.h, and consumers should not have to
# find a second include prefix built from a different revision.
set(server_stage_squid_include_root ${server_stage_root}/include/squid)
file(MAKE_DIRECTORY ${server_stage_squid_include_root})
file(GLOB libsquid_public_headers CONFIGURE_DEPENDS
    "${libsquid_SOURCE_DIR}/include/squid/*.h"
)
foreach(libsquid_public_header IN LISTS libsquid_public_headers)
    get_filename_component(libsquid_public_header_name
        ${libsquid_public_header} NAME
    )
    configure_file(
        ${libsquid_public_header}
        ${server_stage_squid_include_root}/${libsquid_public_header_name}
        COPYONLY
    )
endforeach()

add_library(squid ${libsquid_sources})

target_include_directories(squid
    PUBLIC
        ${libsquid_SOURCE_DIR}/include
    PRIVATE
        ${libsquid_SOURCE_DIR}/lib/squid/portable
        ${libsquid_SOURCE_DIR}/lib/squid/contract
)

target_compile_features(squid PUBLIC c_std_11)
target_compile_options(squid PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(squid PRIVATE SQUID_MAX_SOCKETS=15)
set_target_properties(
    squid
    PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${server_stage_root}/lib/client/portable
)

add_subdirectory(client)
add_subdirectory(echo)
add_subdirectory(filesystem)
add_subdirectory(retrovault)
add_subdirectory(squidsys)
add_subdirectory(tcp_proxy)
add_subdirectory(time)
