#include "config_lua.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <wlr/util/log.h>

#include "module/module.h"
#include "plugin/manager.h"
#include "plugin/manager_lua.h"
#include "shady.h"

static struct shady_server *config_server;

bool shady_rice_file(const char *rice, const char *file, char *out, size_t size) {
	if (!rice[0] || rice[0] == '.' || strchr(rice, '/')) return false;
	const char *root = getenv("SHADY_ROOT");
	if (!root || !*root) root = SHADY_DATA_ROOT;
	int written = snprintf(out, size, "%s/rices/%s/%s", root, rice, file);
	return written > 0 && (size_t)written < size && access(out, R_OK) == 0;
}

int shady_rice_lua_base(lua_State *L, const char *file) {
	const char *rice = luaL_checkstring(L, 1);
	char path[4096];
	if (!shady_rice_file(rice, "", path, sizeof(path)))
		return luaL_error(L, "no such rice: %s", rice);
	/* A rice without this stage (no init.lua, say) has nothing to add. */
	if (!shady_rice_file(rice, file, path, sizeof(path))) return 0;
	if (luaL_loadfile(L, path) != LUA_OK) return lua_error(L);
	lua_call(L, 0, 0);
	return 0;
}

static int l_base(lua_State *L) {
	return shady_rice_lua_base(L, "config.lua");
}

static const char *lua_value_string(lua_State *L, int index,
		char *buffer, size_t size) {
	if (lua_isboolean(L, index)) {
		snprintf(buffer, size, "%s", lua_toboolean(L, index) ? "true" : "false");
		return buffer;
	}
	if (lua_isnumber(L, index)) {
		snprintf(buffer, size, "%.17g", lua_tonumber(L, index));
		return buffer;
	}
	return luaL_checkstring(L, index);
}

static int l_set(lua_State *L) {
	const char *key = luaL_checkstring(L, 1);
	char value_buf[64];
	const char *value = lua_value_string(L, 2, value_buf, sizeof(value_buf));
	if (!shady_config_set(&config_server->config, key, value)) {
		return luaL_error(L, "invalid config option: %s", key);
	}
	return 0;
}

static int l_bind(lua_State *L) {
	const char *name = luaL_checkstring(L, 1);
	const char *binding = luaL_checkstring(L, 2);
	char key[128];
	snprintf(key, sizeof(key), "bind.%s", name);
	if (!shady_config_set(&config_server->config, key, binding)) {
		return luaL_error(L, "invalid key binding: %s = %s", name, binding);
	}
	return 0;
}

/* Default plugins are not loaded yet while config.lua runs, so a module name
 * the module manager does not know may still be a pending default plugin. */
static bool set_module_enabled(const char *name, bool enabled) {
	if (!shady_modules_set_enabled(&config_server->modules, name, enabled) &&
			!shady_plugin_manager_set_enabled(config_server, name, enabled))
		return false;
	if (strcmp(name, "spatial") == 0) {
		config_server->config.spatial_mode = enabled;
	}
	return true;
}

static int l_module(lua_State *L) {
	const char *name = luaL_checkstring(L, 1);
	if (!set_module_enabled(name, lua_toboolean(L, 2))) {
		return luaL_error(L, "unknown module: %s", name);
	}
	return 0;
}

static int l_has_module(lua_State *L) {
	const char *name = luaL_checkstring(L, 1);
	lua_pushboolean(L, shady_modules_has_registered(&config_server->modules, name) ||
		shady_plugin_manager_knows(config_server, name));
	return 1;
}

static int l_modules(lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_pushnil(L);
	while (lua_next(L, 1) != 0) {
		const char *name = luaL_checkstring(L, -2);
		if (!set_module_enabled(name, lua_toboolean(L, -1))) {
			return luaL_error(L, "unknown module: %s", name);
		}
		lua_pop(L, 1);
	}
	return 0;
}

/* Kept for existing configs; same as shady.plugins.load(). */
static int l_plugin(lua_State *L) {
	const char *path = luaL_checkstring(L, 1);
	if (!shady_plugin_manager_load(config_server, path)) {
		return luaL_error(L, "failed to load plugin: %s", path);
	}
	return 0;
}

static int l_log(lua_State *L) {
	wlr_log(WLR_INFO, "[config.lua] %s", luaL_checkstring(L, 1));
	return 0;
}

static void install_config_api(lua_State *L) {
	lua_newtable(L);
	lua_pushcfunction(L, l_base); lua_setfield(L, -2, "base");
	lua_pushcfunction(L, l_set); lua_setfield(L, -2, "set");
	lua_pushcfunction(L, l_set); lua_setfield(L, -2, "config");
	lua_pushcfunction(L, l_bind); lua_setfield(L, -2, "bind");
	lua_pushcfunction(L, l_module); lua_setfield(L, -2, "module");
	lua_pushcfunction(L, l_modules); lua_setfield(L, -2, "modules");
	lua_pushcfunction(L, l_has_module); lua_setfield(L, -2, "has_module");
	lua_pushcfunction(L, l_plugin); lua_setfield(L, -2, "plugin");
	lua_pushcfunction(L, l_log); lua_setfield(L, -2, "log");
	shady_plugin_manager_lua_install(L, config_server, SHADY_PLUGIN_LUA_BOOTSTRAP);
	lua_setglobal(L, "shady");
}

bool shady_config_lua_load(struct shady_server *server, const char *path) {
	lua_State *L = luaL_newstate();
	if (!L) {
		wlr_log(WLR_ERROR, "config.lua: failed to create Lua state");
		return false;
	}

	config_server = server;
	luaL_openlibs(L);
	install_config_api(L);

	if (luaL_loadfile(L, path) != LUA_OK) {
		const char *error = lua_tostring(L, -1);
		if (error && strstr(error, "No such file or directory")) {
			wlr_log(WLR_INFO, "config.lua: %s not found, using defaults", path);
			lua_close(L);
			config_server = NULL;
			return true;
		}
		wlr_log(WLR_ERROR, "config.lua: load error: %s",
			error ? error : "unknown error");
		lua_close(L);
		config_server = NULL;
		return false;
	}
	if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
		wlr_log(WLR_ERROR, "config.lua: runtime error: %s",
			lua_tostring(L, -1));
		lua_close(L);
		config_server = NULL;
		return false;
	}

	wlr_log(WLR_INFO, "config.lua: loaded %s", path);
	lua_close(L);
	config_server = NULL;
	return true;
}
