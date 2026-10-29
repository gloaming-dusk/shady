#ifndef SHADY_MODULE_LUA_H
#define SHADY_MODULE_LUA_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <xkbcommon/xkbcommon.h>
struct shady_toplevel;
struct shady_server;
struct json_buf;
/* Execute in the runtime state. Results are a JSON array; restores the stack. */
bool shady_lua_eval(struct shady_server *server, const char *code, const char *name,
	struct json_buf *result, char *error, size_t error_size);
bool shady_lua_init(struct shady_server *server);
bool shady_lua_start(struct shady_server *server);
void shady_lua_fini(struct shady_server *server);
bool shady_lua_handle_key(struct shady_server *server,xkb_keysym_t sym,uint32_t modifiers);
void shady_lua_apply_window_rules(struct shady_toplevel *toplevel);
#endif
