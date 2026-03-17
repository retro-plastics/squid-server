file(GLOB config_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

target_sources(squid_server_core PRIVATE ${config_sources})
