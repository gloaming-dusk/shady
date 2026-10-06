#ifndef SHADY_PLUGIN_MANAGER_LUA_H
#define SHADY_PLUGIN_MANAGER_LUA_H

#include <lua.h>

struct shady_server;

enum shady_plugin_lua_phase {
	/* config.lua: before modules resolve. path/load/enable/disable/list. */
	SHADY_PLUGIN_LUA_BOOTSTRAP,
	/* init.lua and later: list/reload/unload. */
	SHADY_PLUGIN_LUA_RUNTIME,
};

/* Set `plugins` on the table at the top of the stack. */
void shady_plugin_manager_lua_install(lua_State *L, struct shady_server *server,
	enum shady_plugin_lua_phase phase);

#endif
