#include "render.h"

#include <time.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "../shady.h"

bool shady_render_init(struct wlr_renderer *renderer) {
	(void)renderer;
	return true;
}

void shady_render_fini(void) {
}

void shady_render_schedule_all_outputs(struct shady_server *server) {
	struct shady_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		wlr_output_schedule_frame(output->wlr_output);
	}
}

void shady_render_output_frame(struct shady_output *output) {
	struct shady_server *server = output->server;
	struct wlr_scene_output *scene_output =
		wlr_scene_get_scene_output(server->scene, output->wlr_output);
	if (!scene_output) {
		return;
	}
	if (!wlr_scene_output_commit(scene_output, NULL)) {
		wlr_log(WLR_ERROR, "failed to commit desktop scene output");
	}
}

void shady_render_toplevel_commit(struct shady_toplevel *toplevel) {
	(void)toplevel;
}

void shady_render_toplevel_unmap(struct shady_toplevel *toplevel) {
	(void)toplevel;
}

void shady_render_toplevel_destroy(struct shady_toplevel *toplevel) {
	(void)toplevel;
}
