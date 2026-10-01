#include "module.h"

#include "../shady.h"

#if SHADY_HAS_SPATIAL
const struct shady_module *shady_spatial_module(void);
#endif
#if SHADY_HAS_LUA
const struct shady_module *shady_lua_module(void);
#endif
const struct shady_module *shady_desktop_protocols_module(void);

void shady_register_builtin_modules(struct shady_server *server) {
#if SHADY_HAS_LUA
	shady_modules_register(&server->modules, shady_lua_module());
#endif
	shady_modules_register(&server->modules, shady_desktop_protocols_module());
#if SHADY_HAS_SPATIAL
	shady_modules_register(&server->modules, shady_spatial_module());
#endif
}
