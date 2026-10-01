#ifndef SHADY_PLUGIN_LOADER_H
#define SHADY_PLUGIN_LOADER_H

#include <stdbool.h>

struct shady_server;

bool shady_plugin_load(struct shady_server *server, const char *path);
bool shady_plugin_unload(struct shady_server *server, const char *name);
bool shady_plugin_reload(struct shady_server *server, const char *name);

#endif
