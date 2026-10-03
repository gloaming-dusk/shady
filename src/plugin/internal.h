#ifndef SHADY_PLUGIN_INTERNAL_H
#define SHADY_PLUGIN_INTERNAL_H
#include <shady/plugin.h>
extern const struct shady_plugin_api_v1 shady_plugin_api;
void *shady_plugin_owner_from_address(void *address);
bool shady_plugin_window_valid(shady_host host, shady_window window);
const void *shady_plugin_query_api(shady_host host, const char *name, uint32_t version);
void shady_plugin_motion_cleanup_owner(struct shady_server *server, void *owner);
extern const struct shady_motion_api_v1 shady_motion_api;
#endif
