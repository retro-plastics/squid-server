find_package(CURL QUIET)

# Some development environments expose libcurl through curl-config without
# adding its installation prefix to CMake's search path.
if(NOT CURL_FOUND)
    find_program(retro_vault_curl_config curl-config)
    if(retro_vault_curl_config)
        execute_process(
            COMMAND ${retro_vault_curl_config} --prefix
            OUTPUT_VARIABLE retro_vault_curl_prefix
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE retro_vault_curl_config_result
        )
        if(retro_vault_curl_config_result EQUAL 0)
            list(PREPEND CMAKE_PREFIX_PATH ${retro_vault_curl_prefix})
            find_package(CURL QUIET)
        endif()
    endif()
endif()

if(NOT CURL_FOUND)
    message(FATAL_ERROR
        "The Retro Vault plugin requires the libcurl development package"
    )
endif()

set(retro_vault_core_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/retrovault_core.c
    ${CMAKE_CURRENT_SOURCE_DIR}/retrovault_json.c
)

add_library(retrovault SHARED
    ${retro_vault_core_sources}
    ${CMAKE_CURRENT_SOURCE_DIR}/retrovault_curl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/retrovault_plugin.c
)

set_target_properties(
    retrovault
    PROPERTIES
        OUTPUT_NAME retrovault
        LIBRARY_OUTPUT_DIRECTORY ${server_stage_plugin_root}
)

target_include_directories(
    retrovault
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(retrovault PRIVATE CURL::libcurl)
target_compile_features(retrovault PRIVATE c_std_11)
target_compile_options(retrovault PRIVATE -Wall -Wextra -Wpedantic)

add_executable(retrovault-tests
    ${retro_vault_core_sources}
    ${CMAKE_CURRENT_SOURCE_DIR}/retrovault_curl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_retrovault.c
)

target_include_directories(
    retrovault-tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(retrovault-tests PRIVATE c_std_11)
target_compile_options(retrovault-tests PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(retrovault-tests PRIVATE CURL::libcurl)

add_test(NAME retrovault_tests COMMAND retrovault-tests)
