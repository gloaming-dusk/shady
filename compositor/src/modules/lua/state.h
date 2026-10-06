#ifndef SHADY_MODULE_LUA_STATE_H
#define SHADY_MODULE_LUA_STATE_H

#include "../../module/module.h"

struct lua_State;
struct shady_server;
struct shady_lua_state { struct lua_State *L; };

static inline struct shady_lua_state *shady_lua_state_for(struct shady_server *server) {
	return shady_module_state(server, "lua");
}

#endif
