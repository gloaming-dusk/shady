#include "module.h"

#include "../shady.h"
#include "../plugin/plugin.h"
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
#if SHADY_HAS_WINDOW_MOTION
static bool register_motion_plugin(struct shady_server *server) {
    char path[4096];
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n > 0 && (size_t)n < sizeof(path) - 1) {
        path[n] = '\0';
        char *slash = strrchr(path, '/');
        if (slash) {
            *slash = '\0';
            size_t dir_size = strlen(path);
            const char *name = "/libshady-plugin-window-motion.so";
            if (dir_size + strlen(name) < sizeof(path)) {
                strcat(path, name);
                if (access(path, R_OK) == 0) return shady_plugin_load(server, path);
            }
        }
    }
    return shady_plugin_load(server, SHADY_PLUGIN_DIR "/libshady-plugin-window-motion.so");
}
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
#if SHADY_HAS_WINDOW_MOTION
	if (!register_motion_plugin(server)) return false;
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
