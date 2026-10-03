#ifndef SHADY_PUBLIC_PLUGIN_H
#define SHADY_PUBLIC_PLUGIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <shady/module.h>
#include <shady/types.h>
#include <shady/representation.h>

/* Test append-only members before accessing a table from an older host. */
#define SHADY_API_HAS(api, member) \
    ((api) && (api)->struct_size >= offsetof(struct shady_plugin_api_v1, member) + \
        sizeof((api)->member) && (api)->member)

#define SHADY_PLUGIN_ABI_V1 1u
#define SHADY_PLUGIN_ENTRY_V1 "shady_plugin_entry_v1"
#define SHADY_PLUGIN_ABI_V2 2u
#define SHADY_PLUGIN_ENTRY_V2 "shady_plugin_entry_v2"

enum shady_plugin_log_level {
	SHADY_PLUGIN_LOG_DEBUG = 0,
	SHADY_PLUGIN_LOG_INFO = 1,
	SHADY_PLUGIN_LOG_ERROR = 2,
};

enum shady_close_effect_style {
	SHADY_CLOSE_EFFECT_CRUMPLE = 0,
	SHADY_CLOSE_EFFECT_SLIDE_FADE = 1,
};

enum shady_render_stage {
	SHADY_RENDER_STAGE_AFTER_BACKGROUND = 0,
	SHADY_RENDER_STAGE_BEFORE_WINDOWS = 1,
	SHADY_RENDER_STAGE_AFTER_WINDOWS = 2,
	SHADY_RENDER_STAGE_OVERLAY = 3,
};

struct shady_render_context {
	uint32_t struct_size;
	uint32_t stage;
	shady_output output;
	int width;
	int height;
	float logical_width;
	float logical_height;
	float time_seconds;
	/* Append-only camera description for view-dependent effects such as
	 * procedural skies. All vectors are world space; forward/right/up are
	 * unit length. A fragment at NDC (x, y) of the *output image* looks along
	 * forward + right * x * tan_half_fov_y * aspect - up * y * tan_half_fov_y
	 * (Shady renders with a flipped Y). Check with SHADY_RENDER_CONTEXT_HAS. */
	float camera_position[3];
	float camera_forward[3];
	float camera_right[3];
	float camera_up[3];
	float tan_half_fov_y;
	float aspect;
};

#define SHADY_RENDER_CONTEXT_HAS(ctx, member) \
	((ctx) && (ctx)->struct_size >= offsetof(struct shady_render_context, member) + \
		sizeof((ctx)->member))

typedef void (*shady_render_callback)(shady_host host,
	const struct shady_render_context *context, void *user_data);

struct shady_close_effect {
	uint32_t style;
	float duration;
	float strength;
	float direction_x;
	float direction_y;
};

struct shady_plugin_api_v1 {
	uint32_t abi_version;
	uint32_t struct_size;

	void (*log)(enum shady_plugin_log_level level, const char *message);
	bool (*has_capability)(shady_host host, const char *capability);
	bool (*config_set)(shady_host host, const char *key, const char *value);
	void *(*module_state)(shady_host host, const char *module_name);
	void *(*window_state)(shady_window window, const char *module_name);

	size_t (*window_count)(shady_host host);
	shady_window (*window_at)(shady_host host, size_t index);
	const char *(*window_title)(shady_window window);
	const char *(*window_app_id)(shady_window window);
	bool (*window_valid)(shady_host host, shady_window window);
	bool (*window_mapped)(shady_window window);
	bool (*window_focus)(shady_host host, shady_window window);
	bool (*window_close)(shady_host host, shady_window window);
	bool (*window_size)(shady_window window, int *width, int *height);
	bool (*window_position)(shady_window window, double *x, double *y, float *z);
	bool (*window_set_position)(shady_host host, shady_window window,
		double x, double y, float z);

	size_t (*output_count)(shady_host host);
	shady_output (*output_at)(shady_host host, size_t index);
	const char *(*output_name)(shady_output output);
	bool (*output_valid)(shady_host host, shady_output output);
	void (*output_size)(shady_output output, int *width, int *height);
	float (*output_scale)(shady_output output);

	shady_seat (*seat)(shady_host host);
	const char *(*seat_name)(shady_seat seat);

	size_t (*module_count)(shady_host host);
	shady_module_handle (*module_at)(shady_host host, size_t index);
	const char *(*module_name)(shady_module_handle module);
	bool (*module_active)(shady_host host, shady_module_handle module);

	const char *(*event_name)(uint32_t event_type);
	bool (*subscribe_event)(shady_host host, uint32_t event_type,
		shady_event_callback callback, void *user_data);
	shady_subscription_id (*subscribe_event_handle)(shady_host host,
		uint32_t event_type, shady_event_callback callback, void *user_data);
	bool (*unsubscribe_event)(shady_host host, shady_subscription_id subscription);
	void (*schedule_render)(shady_host host);
	void (*terminate)(shady_host host);

	/* ABI v1 append-only extension fields. Never reorder earlier entries. */
	shady_window (*focused_window)(shady_host host);
	bool (*window_visible)(shady_window window);
	bool (*window_maximized)(shady_window window);
	bool (*window_fullscreen)(shady_window window);
	bool (*window_set_maximized)(shady_host host, shady_window window, bool enabled);
	bool (*window_set_fullscreen)(shady_host host, shady_window window, bool enabled);
	size_t (*workspace_count)(shady_host host);
	const char *(*workspace_at)(shady_host host, size_t index);
	const char *(*current_workspace)(shady_host host);
	bool (*workspace_switch)(shady_host host, const char *name);
	const char *(*window_workspace)(shady_window window);
	bool (*window_move_to_workspace)(shady_host host, shady_window window,
		const char *name);
	void (*output_schedule_render)(shady_host host, shady_output output);
	/* Per-window animated surface wave. Amplitude <= 0 disables the effect. */
	bool (*window_set_water_effect)(shady_host host, shady_window window,
		float amplitude, float frequency, float speed, float phase);
	bool (*window_water_effect)(shady_window window,
		float *amplitude, float *frequency, float *speed, float *phase);
	bool (*window_set_water_surface)(shady_host host, shady_window window,
		float fresnel, float specular, float caustic, float tint);
	bool (*window_water_surface)(shady_window window,
		float *fresnel, float *specular, float *caustic, float *tint);
	bool (*window_set_border)(shady_host host, shady_window window,
		float width, float r, float g, float b, float a);
	bool (*window_border)(shady_window window,
		float *width, float color[4], bool *overridden);
	bool (*window_reset_border)(shady_host host, shady_window window);
	bool (*window_set_close_effect)(shady_host host, shady_window window,
		const struct shady_close_effect *effect);
	bool (*window_close_effect)(shady_window window,
		struct shady_close_effect *effect, bool *overridden);
	bool (*window_reset_close_effect)(shady_host host, shady_window window);

	/* Host-owned GLES2 shader resources for plugin render effects. Shader files
	 * are loaded from the paths supplied by the plugin. Fullscreen programs must
	 * expose an `a_pos` attribute; uniforms can be set by name from callbacks. */
	shady_shader_program (*shader_program_create)(shady_host host,
		const char *vertex_path, const char *fragment_path);
	bool (*shader_program_destroy)(shady_host host, shady_shader_program program);
	bool (*shader_uniform_float)(shady_host host, shady_shader_program program,
		const char *name, float value);
	bool (*shader_uniform_int)(shady_host host, shady_shader_program program,
		const char *name, int value);
	bool (*shader_uniform_vec2)(shady_host host, shady_shader_program program,
		const char *name, float x, float y);
	bool (*shader_uniform_vec4)(shady_host host, shady_shader_program program,
		const char *name, float x, float y, float z, float w);
	bool (*shader_draw_fullscreen)(shady_host host, shady_shader_program program);
	shady_render_hook_id (*render_hook_add)(shady_host host, uint32_t stage,
		shady_render_callback callback, void *user_data);
	bool (*render_hook_remove)(shady_host host, shady_render_hook_id hook);

	/* Associate a host-owned shader program with one window. The program is
	 * executed on Shady's subdivided window mesh. The host binds the current
	 * window texture to sampler `u_tex` and supplies standard uniforms when
	 * present: u_mvp, u_model, u_frame_rect, u_time, u_resolution,
	 * u_window_size, u_has_alpha, u_wobble, u_water, u_water_surface,
	 * u_border_color, u_border_width, u_close_progress, u_close_effect,
	 * u_tint, u_effect_strength, u_brightness, u_light_dir and optional
	 * live-source uniforms u_portal_tex, u_portal_available, u_portal_size. */
	bool (*window_set_shader)(shady_host host, shady_window window,
		shady_shader_program program);
	bool (*window_reset_shader)(shady_host host, shady_window window);
	shady_shader_program (*window_shader)(shady_window window);

	/* Optional spatial representation used while an FPS window is folded.
	 * BOX dimensions are world-space units and are shared by rendering,
	 * picking and physics so the visible shape and collision body agree. */
	bool (*window_set_representation)(shady_host host, shady_window window,
		const struct shady_window_representation *representation);
	bool (*window_reset_representation)(shady_host host, shady_window window);
	bool (*window_representation)(shady_window window,
		struct shady_window_representation *representation, bool *overridden);

	/* Dynamic representation provider. The base field is the validated fallback
	 * shape. model may change the visible box transform per frame; collision may
	 * override the authoritative axis-aligned collision box. Providers are
	 * cleared automatically when their owning plugin unloads. */
	bool (*window_set_representation_provider)(shady_host host,
		shady_window window,
		const struct shady_window_representation_provider *provider);
	bool (*window_reset_representation_provider)(shady_host host,
		shady_window window,
		const struct shady_window_representation_provider *provider);
	void *(*window_representation_state)(shady_host host, shady_window window,
		const struct shady_window_representation_provider *provider,
		size_t *state_size);

	/* Optional live auxiliary window sampler for custom window shaders. The host
	 * exposes the source surface as `u_portal_tex` on texture unit 1 and sets
	 * `u_portal_available`/`u_portal_size` when the source is renderable. */
	bool (*window_set_shader_source)(shady_host host, shady_window target,
		shady_window source);
	bool (*window_reset_shader_source)(shady_host host, shady_window target);
	shady_window (*window_shader_source)(shady_window target);
	/* Feature APIs are separately versioned immutable host tables. Unknown names
	 * or unsupported versions return NULL. Check this field with SHADY_API_HAS first. */
	const void *(*query_api)(shady_host host, const char *name, uint32_t version);
};

typedef const struct shady_module *(*shady_plugin_entry_v1_fn)(
	uint32_t host_abi,
	const struct shady_plugin_api_v1 *api,
	shady_host host);

/*
 * V2 adds explicit state migration without changing struct shady_module.
 * Snapshot buffers are allocated and freed by the host. Plugins only write
 * into/read from those buffers, so snapshots remain valid across dlclose().
 */
struct shady_plugin_v2 {
	uint32_t struct_size;
	const struct shady_module *module;
	uint32_t state_schema_version;

	size_t (*module_snapshot_size)(shady_host host, const void *state);
	bool (*save_module_state)(shady_host host, const void *state,
		void *snapshot, size_t snapshot_size);
	bool (*restore_module_state)(shady_host host, void *state,
		const void *snapshot, size_t snapshot_size,
		uint32_t previous_schema_version);

	size_t (*window_snapshot_size)(shady_host host, shady_window window,
		const void *state);
	bool (*save_window_state)(shady_host host, shady_window window,
		const void *state, void *snapshot, size_t snapshot_size);
	bool (*restore_window_state)(shady_host host, shady_window window,
		void *state, const void *snapshot, size_t snapshot_size,
		uint32_t previous_schema_version);
};

typedef const struct shady_plugin_v2 *(*shady_plugin_entry_v2_fn)(
	uint32_t host_abi,
	const struct shady_plugin_api_v1 *api,
	shady_host host);

#endif
