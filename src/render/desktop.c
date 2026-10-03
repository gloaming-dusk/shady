#include "render.h"

#include <time.h>
#include <string.h>
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

void shady_render_schedule_output(struct shady_output *output) {
	if (!output || !output->wlr_output) return;
	output->frame_schedule_requests++;
	if (!output->wlr_output->enabled) {
		output->frame_scheduled = false;
		return;
	}
	if (output->frame_scheduled) {
		output->frame_schedule_coalesced++;
		return;
	}
	output->frame_scheduled = true;
	wlr_output_schedule_frame(output->wlr_output);
}

shady_shader_program shady_render_plugin_shader_create(struct shady_server *server,
		void *owner, const char *vertex_path, const char *fragment_path) {
	(void)server; (void)owner; (void)vertex_path; (void)fragment_path; return 0;
}
bool shady_render_plugin_shader_valid(struct shady_server *server, void *owner,
		shady_shader_program program) {
	(void)server; (void)owner; (void)program; return false;
}
bool shady_render_plugin_shader_destroy(struct shady_server *server, void *owner,
		shady_shader_program program) {
	(void)server; (void)owner; (void)program; return false;
}
bool shady_render_plugin_shader_uniform_float(struct shady_server *server, void *owner,
		shady_shader_program program, const char *name, float value) {
	(void)server; (void)owner; (void)program; (void)name; (void)value; return false;
}
bool shady_render_plugin_shader_uniform_int(struct shady_server *server, void *owner,
		shady_shader_program program, const char *name, int value) {
	(void)server; (void)owner; (void)program; (void)name; (void)value; return false;
}
bool shady_render_plugin_shader_uniform_vec2(struct shady_server *server, void *owner,
		shady_shader_program program, const char *name, float x, float y) {
	(void)server; (void)owner; (void)program; (void)name; (void)x; (void)y; return false;
}
bool shady_render_plugin_shader_uniform_vec4(struct shady_server *server, void *owner,
		shady_shader_program program, const char *name, float x, float y, float z, float w) {
	(void)server; (void)owner; (void)program; (void)name; (void)x; (void)y; (void)z; (void)w; return false;
}
bool shady_render_plugin_shader_draw_fullscreen(struct shady_server *server, void *owner,
		shady_shader_program program) {
	(void)server; (void)owner; (void)program; return false;
}
shady_render_hook_id shady_render_plugin_hook_add(struct shady_server *server, void *owner,
		uint32_t stage, shady_render_callback callback, void *user_data) {
	(void)server; (void)owner; (void)stage; (void)callback; (void)user_data; return 0;
}
bool shady_render_plugin_hook_remove(struct shady_server *server, void *owner,
		shady_render_hook_id hook) {
	(void)server; (void)owner; (void)hook; return false;
}
void shady_render_plugin_cleanup_owner(struct shady_server *server, void *owner) {
	if (!server || !owner) return;
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->all_toplevels, all_link) {
		if (toplevel->plugin_shader_owner == owner) {
			toplevel->plugin_shader_program = 0;
			toplevel->plugin_shader_owner = NULL;
		}
		if (toplevel->plugin_shader_source_owner == owner) {
			toplevel->plugin_shader_source = NULL;
			toplevel->plugin_shader_source_owner = NULL;
		}

	}
}

void shady_render_schedule_all_outputs(struct shady_server *server) {
	struct shady_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		shady_render_schedule_output(output);
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
