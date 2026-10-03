#ifndef SHADY_RENDER_H
#define SHADY_RENDER_H

#include <stdbool.h>
#include <shady/plugin.h>

struct shady_output;
struct shady_server;
struct wlr_renderer;
struct shady_toplevel;

bool shady_render_init(struct wlr_renderer *renderer);
void shady_render_fini(void);
void shady_render_output_frame(struct shady_output *output);
void shady_render_schedule_output(struct shady_output *output);
void shady_render_schedule_all_outputs(struct shady_server *server);

shady_shader_program shady_render_plugin_shader_create(struct shady_server *server,
	void *owner, const char *vertex_path, const char *fragment_path);
bool shady_render_plugin_shader_destroy(struct shady_server *server, void *owner,
	shady_shader_program program);
bool shady_render_plugin_shader_valid(struct shady_server *server, void *owner,
	shady_shader_program program);
bool shady_render_plugin_shader_uniform_float(struct shady_server *server, void *owner,
	shady_shader_program program, const char *name, float value);
bool shady_render_plugin_shader_uniform_int(struct shady_server *server, void *owner,
	shady_shader_program program, const char *name, int value);
bool shady_render_plugin_shader_uniform_vec2(struct shady_server *server, void *owner,
	shady_shader_program program, const char *name, float x, float y);
bool shady_render_plugin_shader_uniform_vec4(struct shady_server *server, void *owner,
	shady_shader_program program, const char *name, float x, float y, float z, float w);
bool shady_render_plugin_shader_draw_fullscreen(struct shady_server *server, void *owner,
	shady_shader_program program);
shady_render_hook_id shady_render_plugin_hook_add(struct shady_server *server, void *owner,
	uint32_t stage, shady_render_callback callback, void *user_data);
bool shady_render_plugin_hook_remove(struct shady_server *server, void *owner,
	shady_render_hook_id hook);
void shady_render_plugin_cleanup_owner(struct shady_server *server, void *owner);

/* Shared with pick3d: build view/proj for the given output size. */
void shady_render_camera_matrices(struct shady_server *server,
	int buf_w, int buf_h, float view[16], float proj[16]);

void shady_render_toplevel_commit(
	struct shady_toplevel *toplevel
);

void shady_render_toplevel_unmap(
	struct shady_toplevel *toplevel
);

void shady_render_toplevel_destroy(
	struct shady_toplevel *toplevel
);

#endif
