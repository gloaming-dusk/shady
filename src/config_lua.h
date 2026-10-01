#ifndef SHADY_CONFIG_LUA_H
#define SHADY_CONFIG_LUA_H

#include <stdbool.h>

struct shady_server;

bool shady_config_lua_load(struct shady_server *server, const char *path);

#endif
