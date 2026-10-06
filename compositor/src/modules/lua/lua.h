#ifndef SHADY_MODULE_LUA_H
#define SHADY_MODULE_LUA_H
#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>
struct shady_toplevel;
struct shady_server;
bool shady_lua_init(struct shady_server *server);
bool shady_lua_start(struct shady_server *server);
void shady_lua_fini(struct shady_server *server);
bool shady_lua_handle_key(struct shady_server *server,xkb_keysym_t sym,uint32_t modifiers);
void shady_lua_apply_window_rules(struct shady_toplevel *toplevel);
#endif
