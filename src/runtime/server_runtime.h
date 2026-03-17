#ifndef SERVER_RUNTIME_H
#define SERVER_RUNTIME_H

#include "config/server_config.h"
#include "config/server_plugin_config.h"
#include "plugin/server_plugin_loader.h"
#include "plugin/server_plugin_registry.h"

struct server_runtime {
    struct server_config config;
    struct server_plugin_config_file plugin_config;
    struct server_plugin_registry plugin_registry;
    struct server_plugin_loader plugin_loader;
};

void init_server_runtime(
    struct server_runtime *runtime,
    const struct server_config *config
);

int run_server_runtime(struct server_runtime *runtime);

#endif
