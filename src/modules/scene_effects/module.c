#include "../../module/module.h"

static const char *const provides[] = { "spatial.scene-effects", NULL };
static const char *const requires[] = { "spatial", NULL };

static const struct shady_module module = {
	.name = "scene-effects",
	.provides = provides,
	.requires = requires,
};

const struct shady_module *shady_scene_effects_module(void) { return &module; }
