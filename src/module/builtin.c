#include "module.h"

#include "../shady.h"
#include "../plugin/plugin.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

#if SHADY_HAS_WINDOW_MOTION || SHADY_HAS_OBJ_LOADER
/* Load a plugin shipped with Shady. The build-tree executable finds it beside
 * itself; installed executables use the configured plugin directory. */
static bool register_shipped_plugin(struct shady_server *server, const char *file) {
	char path[4096];
	ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
	if (n > 0 && (size_t)n < sizeof(path) - 1) {
		path[n] = '\0';
		char *slash = strrchr(path, '/');
		if (slash && (size_t)(slash - path) + 1 + strlen(file) < sizeof(path)) {
			strcpy(slash + 1, file);
			if (access(path, R_OK) == 0) return shady_plugin_load(server, path);
		}
	}
	char installed[4096];
	snprintf(installed, sizeof(installed), "%s/%s", SHADY_PLUGIN_DIR, file);
	return shady_plugin_load(server, installed);
}
#endif

bool shady_register_builtin_modules(struct shady_server *server) {
#if SHADY_HAS_LUA
	shady_modules_register(&server->modules, shady_lua_module());
#endif
	shady_modules_register(&server->modules, shady_desktop_protocols_module());
	shady_modules_register(&server->modules, shady_workspace_module());
#if SHADY_HAS_SPATIAL
	shady_modules_register(&server->modules, shady_spatial_module());
#endif
#if SHADY_HAS_WINDOW_MOTION
	if (!register_shipped_plugin(server, "libshady-plugin-window-motion.so")) return false;
#endif
#if SHADY_HAS_OBJ_LOADER
	if (!register_shipped_plugin(server, "libshady-plugin-obj-loader.so")) return false;
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
