#include "../../module/module.h"

#include "../../shady.h"
#include "lua.h"
#include "state.h"

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

static bool lua_key(struct shady_server *server, const xkb_keysym_t *syms,
		int nsyms, uint32_t state, uint32_t modifiers) {
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
		return false;
	}
	for (int i = 0; i < nsyms; i++) {
		if (shady_lua_handle_key(server, syms[i], modifiers)) {
			return true;
		}
	}
	return false;
}

static const char *const lua_provides[] = {
	"scripting.lua",
	NULL,
};

static const char *const lua_optional_requires[] = {
	"spatial",
	NULL,
};

static const struct shady_module lua_module = {
	.name = "lua",
	.provides = lua_provides,
	.optional_requires = lua_optional_requires,
	.state_size = sizeof(struct shady_lua_state),
	.init = lua_init_module,
	.destroy = lua_destroy_module,
	.toplevel_map = lua_toplevel_map,
	.toplevel_unmap = lua_toplevel_unmap,
	.key = lua_key,
};

const struct shady_module *shady_lua_module(void) {
	return &lua_module;
}
