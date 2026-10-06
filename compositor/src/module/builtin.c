#include "module.h"

#include "../shady.h"
#include "../plugin/manager.h"

#if SHADY_HAS_SPATIAL
const struct shady_module *shady_spatial_module(void);
#endif
#if SHADY_HAS_PHYSICS
const struct shady_module *shady_physics_module(void);
#endif
#if SHADY_HAS_FPS
const struct shady_module *shady_fps_module(void);
#endif
#if SHADY_HAS_CLOSE_ANIMATION
const struct shady_module *shady_close_animation_module(void);
#endif
#if SHADY_HAS_SCENE_EFFECTS
const struct shady_module *shady_scene_effects_module(void);
#endif
#if SHADY_HAS_LUA
const struct shady_module *shady_lua_module(void);
#endif
const struct shady_module *shady_desktop_protocols_module(void);
const struct shady_module *shady_workspace_module(void);

bool shady_register_builtin_modules(struct shady_server *server) {
#if SHADY_HAS_LUA
	shady_modules_register(&server->modules, shady_lua_module());
#endif
	shady_modules_register(&server->modules, shady_desktop_protocols_module());
	shady_modules_register(&server->modules, shady_workspace_module());
#if SHADY_HAS_SPATIAL
	shady_modules_register(&server->modules, shady_spatial_module());
#endif
	/* Plugins shipped with Shady. The plugin manager loads them after
	 * config.lua unless the config disabled them; see src/plugin/manager.c. */
#if SHADY_HAS_WINDOW_MOTION
	if (!shady_plugin_manager_add_default(server, "window-motion")) return false;
#endif
#if SHADY_HAS_OBJ_LOADER
	if (!shady_plugin_manager_add_default(server, "obj-loader")) return false;
#endif
#if SHADY_HAS_PHYSICS
	shady_modules_register(&server->modules, shady_physics_module());
#endif
#if SHADY_HAS_FPS
	shady_modules_register(&server->modules, shady_fps_module());
#endif
#if SHADY_HAS_CLOSE_ANIMATION
	shady_modules_register(&server->modules, shady_close_animation_module());
#endif
#if SHADY_HAS_SCENE_EFFECTS
	shady_modules_register(&server->modules, shady_scene_effects_module());
#endif
	return true;
}
