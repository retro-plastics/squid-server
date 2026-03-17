#include "plugin/server_plugin_registry.h"

void init_server_plugin_registry(struct server_plugin_registry *registry)
{
    size_t index = 0;

    if (registry == NULL) {
        return;
    }

    for (index = 0; index < server_port_count; ++index) {
        registry->slots[index].port_number = (uint8_t)(index + server_port_min);
        registry->slots[index].plugin = NULL;
    }
}

int is_valid_server_port(uint8_t port_number)
{
    return port_number <= server_port_max;
}

int is_valid_assignable_server_port(uint8_t port_number)
{
    return (port_number >= server_plugin_port_min) && (port_number <= server_plugin_port_max);
}

int register_server_plugin(
    struct server_plugin_registry *registry,
    uint8_t port_number,
    const struct server_plugin *plugin
)
{
    size_t index = 0;

    if ((registry == NULL) || (plugin == NULL)) {
        return -1;
    }

    if (!is_valid_server_port(port_number)) {
        return -1;
    }

    index = (size_t)(port_number - server_port_min);

    if (registry->slots[index].plugin != NULL) {
        return -1;
    }

    registry->slots[index].plugin = plugin;
    return 0;
}

const struct server_plugin *find_server_plugin(
    struct server_plugin_registry *registry,
    uint8_t port_number
)
{
    size_t index = 0;

    if ((registry == NULL) || !is_valid_server_port(port_number)) {
        return NULL;
    }

    index = (size_t)(port_number - server_port_min);
    return registry->slots[index].plugin;
}

size_t count_registered_server_plugins(const struct server_plugin_registry *registry)
{
    size_t count = 0;
    size_t index = 0;

    if (registry == NULL) {
        return 0;
    }

    for (index = 0; index < server_port_count; ++index) {
        if (registry->slots[index].plugin != NULL) {
            ++count;
        }
    }

    return count;
}
