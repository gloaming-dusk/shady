#ifndef SHADY_PLUGIN_LOADER_H
#define SHADY_PLUGIN_LOADER_H

#include <stdbool.h>

struct shady_server;
struct shady_toplevel;

bool shady_plugin_load(struct shady_server *server, const char *path);
bool shady_plugin_unload(struct shady_server *server, const char *name);
bool shady_plugin_reload(struct shady_server *server, const char *name);
void shady_plugin_representation_cleanup_owner(struct shady_server *server, void *owner);
void shady_plugin_window_cleanup(struct shady_toplevel *toplevel);
void shady_plugin_representation_tick(struct shady_server *server, float dt);

#endif
