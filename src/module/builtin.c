#include "module.h"

#include "../shady.h"

const struct shady_module *shady_spatial_module(void);
const struct shady_module *shady_lua_module(void);
const struct shady_module *shady_desktop_protocols_module(void);

void shady_register_builtin_modules(struct shady_server *server) {
	shady_modules_register(&server->modules, shady_lua_module());
	shady_modules_register(&server->modules, shady_desktop_protocols_module());
	shady_modules_register(&server->modules, shady_spatial_module());
}
