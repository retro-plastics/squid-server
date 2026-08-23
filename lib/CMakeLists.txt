include(FetchContent)

set(LIBSQUID_SOURCE_DIR "" CACHE PATH
    "Use a local libsquid source tree instead of downloading a release"
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
set(libsquid_release_api_url
    "https://api.github.com/repos/retro-plastics/libsquid/releases/latest"
)
set(libsquid_release_metadata
    "${CMAKE_BINARY_DIR}/libsquid-latest-release.json"
)

file(
    DOWNLOAD
    "${libsquid_release_api_url}"
    "${libsquid_release_metadata}"
    HTTPHEADER "Accept: application/vnd.github+json"
    HTTPHEADER "User-Agent: squid-server-cmake"
    HTTPHEADER "X-GitHub-Api-Version: 2022-11-28"
    STATUS libsquid_release_status
    TLS_VERIFY ON
)

list(GET libsquid_release_status 0 libsquid_release_status_code)
list(GET libsquid_release_status 1 libsquid_release_status_message)

if(NOT libsquid_release_status_code EQUAL 0)
    message(FATAL_ERROR
        "Failed to resolve the latest libsquid release: "
        "${libsquid_release_status_message}"
    )
endif()

file(READ "${libsquid_release_metadata}" libsquid_release_json)
string(REGEX MATCH
    "\"tag_name\"[ \t\r\n]*:[ \t\r\n]*\"([^\"]+)\""
    libsquid_release_tag_match
    "${libsquid_release_json}"
)
set(libsquid_release_tag "${CMAKE_MATCH_1}")

if(libsquid_release_tag STREQUAL "")
    message(FATAL_ERROR
        "The latest libsquid release metadata did not contain a tag_name"
    )
endif()

message(STATUS "Using latest libsquid release: ${libsquid_release_tag}")

FetchContent_Declare(
    libsquid
    GIT_REPOSITORY https://github.com/retro-plastics/libsquid.git
    GIT_TAG "${libsquid_release_tag}"
    GIT_SHALLOW TRUE
)

FetchContent_GetProperties(libsquid)

if(NOT libsquid_POPULATED)
    FetchContent_Populate(libsquid)
endif()
endif()

file(GLOB libsquid_sources CONFIGURE_DEPENDS "${libsquid_SOURCE_DIR}/lib/squid/*.c")

add_library(squid ${libsquid_sources})

target_include_directories(squid
    PUBLIC
        ${libsquid_SOURCE_DIR}/include
    PRIVATE
        ${libsquid_SOURCE_DIR}/lib
)

target_compile_features(squid PUBLIC c_std_11)
target_compile_options(squid PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(squid PRIVATE SQUID_MAX_SOCKETS=15)

add_subdirectory(echo)
add_subdirectory(filesystem)
add_subdirectory(retrovault)
add_subdirectory(squidsys)
add_subdirectory(tcp_proxy)
add_subdirectory(time)
