#ifndef SHADY_CONFIG_LUA_H
#define SHADY_CONFIG_LUA_H

#include <lua.h>
#include <stdbool.h>
#include <stddef.h>

struct shady_server;

bool shady_config_lua_load(struct shady_server *server, const char *path);
/* Finds $SHADY_ROOT/rices/<rice>/<file>; false when it is not there. */
bool shady_rice_file(const char *rice, const char *file, char *out, size_t size);
/* shady.base(rice): runs that rice's <file> (config.lua or init.lua) here, so
 * a config can start from a rice and change what it wants afterwards. */
int shady_rice_lua_base(lua_State *L, const char *file);

#endif
