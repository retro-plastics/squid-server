include(FetchContent)

FetchContent_Declare(
    libsquid
    GIT_REPOSITORY https://github.com/retro-plastics/libsquid.git
    GIT_TAG 8ea50fedb782d90168fc7e801f4dac872dbf34ec
    GIT_SHALLOW TRUE
)

FetchContent_GetProperties(libsquid)

if(NOT libsquid_POPULATED)
    FetchContent_Populate(libsquid)
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

add_subdirectory(echo)
add_subdirectory(squidsys)
