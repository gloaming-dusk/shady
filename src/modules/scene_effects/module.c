#include "../../module/module.h"
#include "../../shady.h"

static bool module_enabled(struct shady_server *server) {
	return server->config.spatial_mode;
}

static const char *const provides[] = { "spatial.scene-effects", NULL };
static const char *const requires[] = { "spatial", NULL };

static const struct shady_module module = {
	.name = "scene-effects",
	.provides = provides,
	.enabled = module_enabled,
	.requires = requires,
};

const struct shady_module *shady_scene_effects_module(void) { return &module; }
