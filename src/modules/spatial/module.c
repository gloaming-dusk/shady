#include "../../module/module.h"

#include <wlr/util/log.h>

#include "../../shady.h"
#include "../../render/render.h"
#include "../environment/environment.h"
#include "../fps/fps.h"
#include "../physics/physics.h"

static bool spatial_enabled(struct shady_server *server) {
	return server->config.spatial_mode;
}

static bool spatial_init(struct shady_server *server) {
	server->experimental.world = shady_world_default();
	if (!shady_environment_load_colliders(server)) {
		wlr_log(WLR_ERROR, "spatial: failed to load environment collision groups");
	}
	shady_physics_init(server);
	shady_camera_reset(&server->experimental.camera);
	return shady_render_init(server->renderer);
}

static void spatial_destroy(struct shady_server *server) {
	(void)server;
	shady_render_fini();
}

static void spatial_toplevel_unmap(struct shady_toplevel *toplevel) {
	shady_fps_toplevel_gone(toplevel->server, toplevel);
	shady_render_toplevel_unmap(toplevel);
}

static void spatial_toplevel_commit(struct shady_toplevel *toplevel) {
	shady_render_toplevel_commit(toplevel);
}

static void spatial_toplevel_destroy(struct shady_toplevel *toplevel) {
	shady_fps_toplevel_gone(toplevel->server, toplevel);
	shady_render_toplevel_destroy(toplevel);
}

static const struct shady_module spatial_module = {
	.name = "spatial",
	.enabled = spatial_enabled,
	.init = spatial_init,
	.destroy = spatial_destroy,
	.toplevel_unmap = spatial_toplevel_unmap,
	.toplevel_commit = spatial_toplevel_commit,
	.toplevel_destroy = spatial_toplevel_destroy,
};

const struct shady_module *shady_spatial_module(void) {
	return &spatial_module;
}
