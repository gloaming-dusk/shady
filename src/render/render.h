#ifndef SHADY_RENDER_H
#define SHADY_RENDER_H

#include <stdbool.h>
#include <shady/plugin.h>

struct shady_output;
struct shady_server;
struct wlr_renderer;
struct shady_toplevel;
struct wlr_surface;

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
/* Only inside a render hook: snapshot the output, bind it as u_scene, draw. */
bool shady_render_plugin_shader_draw_fullscreen_scene(struct shady_server *server,
	void *owner, shady_shader_program program);
shady_render_hook_id shady_render_plugin_hook_add(struct shady_server *server, void *owner,
	uint32_t stage, shady_render_callback callback, void *user_data);
bool shady_render_plugin_hook_remove(struct shady_server *server, void *owner,
	shady_render_hook_id hook);
void shady_render_plugin_cleanup_owner(struct shady_server *server, void *owner);

/* Backdrop effects for layer-shell surfaces (shady.layer_effect in Lua).
 * Before a layer surface whose namespace matches is drawn, the scene behind
 * it is captured and drawn through the built-in frosted-glass shader (blur,
 * saturation, tint) or a custom fragment shader. Spatial mode only. */
#define SHADY_LAYER_EFFECT_UNIFORMS 8
struct shady_layer_effect_uniform {
	char name[40]; /* with the u_ prefix */
	int size; /* 1..4 */
	float value[4];
};
struct shady_layer_effect_desc {
	float blur; /* radius in logical px */
	float saturation; /* 1 = unchanged */
	float tint[4]; /* premultiplied wash over the backdrop; alpha 0 = none */
	const char *shader; /* custom fragment shader path, or NULL */
	struct shady_layer_effect_uniform uniforms[SHADY_LAYER_EFFECT_UNIFORMS];
	size_t uniform_count;
};
/* Set the effect for a layer namespace; desc NULL removes it. Returns false
 * when effects are unavailable (no spatial renderer) or the shader fails. */
bool shady_render_set_layer_effect(struct shady_server *server, const char *name_space,
	const struct shady_layer_effect_desc *desc);

/* 3D placement for layer-shell surfaces (shady.layer_transform in Lua): the
 * surface is drawn on a plane rotated about a pivot (by default the edge it
 * is anchored to) and pushed back in depth, seen through a screen-attached
 * perspective; pointer input is mapped back through the same plane.
 * Spatial mode only. */
struct shady_layer_transform_desc {
	float tilt, yaw, roll; /* degrees about the x, y and z axes */
	float depth; /* logical px away from the viewer (negative: closer) */
	float perspective; /* focal length in logical px; 0 = 1.2 x output height */
	bool pivot_center; /* false: pivot on the anchored edge */
};
bool shady_render_set_layer_transform(struct shady_server *server, const char *name_space,
	const struct shady_layer_transform_desc *desc);
/* Hit-test every layer surface at layout coordinates in stacking order,
 * transformed ones where they are drawn. Returns 1 with the surface (or a
 * subsurface) under the point, 0 for none, and -1 when the spatial renderer
 * is not drawing (callers then use the scene graph). */
int shady_render_layer_pick(struct shady_server *server, double lx, double ly,
	struct wlr_surface **surface, double *sx, double *sy);
/* Whether this surface belongs to a transformed layer (whose untransformed
 * scene position must not take input). */
bool shady_render_layer_is_transformed(struct shady_server *server, struct wlr_surface *surface);

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
