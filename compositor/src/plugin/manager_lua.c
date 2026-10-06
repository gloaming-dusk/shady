#include "manager_lua.h"

#include <lauxlib.h>

#include "manager.h"
#include "plugin.h"
#include "../shady.h"

static struct shady_server *upvalue_server(lua_State *L) {
	return lua_touserdata(L, lua_upvalueindex(1));
}

static int l_path(lua_State *L) {
	const char *path = luaL_checkstring(L, 1);
	if (!shady_plugin_manager_add_search_path(upvalue_server(L), path))
		return luaL_error(L, "invalid plugin search path: %s", path);
	return 0;
}

static int l_load(lua_State *L) {
	const char *spec = luaL_checkstring(L, 1);
	if (!shady_plugin_manager_load(upvalue_server(L), spec))
		return luaL_error(L, "failed to load plugin: %s", spec);
	lua_pushboolean(L, 1);
	return 1;
}

static int set_enabled(lua_State *L, bool enabled) {
	const char *name = luaL_checkstring(L, 1);
	if (!shady_plugin_manager_set_enabled(upvalue_server(L), name, enabled))
		return luaL_error(L, "unknown plugin: %s", name);
	return 0;
}

static int l_enable(lua_State *L) { return set_enabled(L, true); }
static int l_disable(lua_State *L) { return set_enabled(L, false); }

static void set_string(lua_State *L, const char *key, const char *value) {
	if (!value) return;
	lua_pushstring(L, value);
	lua_setfield(L, -2, key);
}

static int l_list(lua_State *L) {
	struct shady_server *server = upvalue_server(L);
	struct shady_plugin_manager *pm = &server->plugins;
	lua_createtable(L, (int)pm->record_count, 0);
	for (size_t i = 0; i < pm->record_count; ++i) {
		const struct shady_plugin_record *r = &pm->records[i];
		lua_createtable(L, 0, 5);
		set_string(L, "name", r->module_name ? r->module_name : r->spec);
		set_string(L, "spec", r->spec);
		set_string(L, "path", r->path);
		set_string(L, "state",
			shady_plugin_status_name(shady_plugin_manager_status(server, r)));
		lua_pushboolean(L, r->builtin);
		lua_setfield(L, -2, "builtin");
		lua_rawseti(L, -2, (lua_Integer)i + 1);
	}
	return 1;
}

static int l_reload(lua_State *L) {
	struct shady_server *server = upvalue_server(L);
	const char *name = luaL_checkstring(L, 1);
	lua_pushboolean(L, shady_plugin_reload(server,
		shady_plugin_manager_module_name(server, name)));
	return 1;
}

static int l_unload(lua_State *L) {
	struct shady_server *server = upvalue_server(L);
	const char *name = luaL_checkstring(L, 1);
	lua_pushboolean(L, shady_plugin_unload(server,
		shady_plugin_manager_module_name(server, name)));
	return 1;
}

static void set_function(lua_State *L, struct shady_server *server,
		const char *name, lua_CFunction fn) {
	lua_pushlightuserdata(L, server);
	lua_pushcclosure(L, fn, 1);
	lua_setfield(L, -2, name);
}

void shady_plugin_manager_lua_install(lua_State *L, struct shady_server *server,
		enum shady_plugin_lua_phase phase) {
	lua_newtable(L);
	set_function(L, server, "list", l_list);
	if (phase == SHADY_PLUGIN_LUA_BOOTSTRAP) {
		set_function(L, server, "path", l_path);
		set_function(L, server, "load", l_load);
		set_function(L, server, "enable", l_enable);
		set_function(L, server, "disable", l_disable);
	} else {
		set_function(L, server, "reload", l_reload);
		set_function(L, server, "unload", l_unload);
	}
	lua_setfield(L, -2, "plugins");
}
