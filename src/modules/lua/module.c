#include "../../module/module.h"

#include "../../shady.h"
#include "lua.h"

static bool lua_init_module(struct shady_server *server) {
	return shady_lua_init(server);
}

static void lua_destroy_module(struct shady_server *server) {
	shady_lua_fini(server);
}

static void lua_toplevel_map(struct shady_toplevel *toplevel) {
	shady_lua_emit(toplevel->server, "window_map", toplevel);
}

static void lua_toplevel_unmap(struct shady_toplevel *toplevel) {
	shady_lua_emit(toplevel->server, "window_unmap", toplevel);
}

static const struct shady_module lua_module = {
	.name = "lua",
	.init = lua_init_module,
	.destroy = lua_destroy_module,
	.toplevel_map = lua_toplevel_map,
	.toplevel_unmap = lua_toplevel_unmap,
};

const struct shady_module *shady_lua_module(void) {
	return &lua_module;
}
