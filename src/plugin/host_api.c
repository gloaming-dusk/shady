#define _GNU_SOURCE
#include "plugin.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include <shady/plugin.h>

#include "../module/module.h"
#include "../event/event.h"
#include "../render/render.h"
#if SHADY_HAS_SPATIAL
#include "../render/math3d.h"
#endif
#include "../modules/spatial/state.h"
#include "../modules/close_animation/close_animation.h"
#include "../modules/workspace/workspace.h"
#include "../shady.h"
#include "internal.h"
#include "representation.h"

#define HOST(h) ((struct shady_server *)(h))
#define WINDOW(w) ((struct shady_toplevel *)(w))
#define OUTPUT(o) ((struct shady_output *)(o))
#define SEAT(s) ((struct wlr_seat *)(s))
#define MODULE(m) ((const struct shady_module *)(m))
static void host_log(enum shady_plugin_log_level level, const char *message) {
	enum wlr_log_importance importance = WLR_INFO;
	if (level == SHADY_PLUGIN_LOG_DEBUG) importance = WLR_DEBUG;
	else if (level == SHADY_PLUGIN_LOG_ERROR) importance = WLR_ERROR;
	wlr_log(importance, "[plugin] %s", message ? message : "");
}

static bool host_has_capability(shady_host host, const char *capability) {
	return shady_module_has_capability(HOST(host), capability);
}

static bool host_config_set(shady_host host, const char *key, const char *value) {
	struct shady_server *server = HOST(host);
	if (!shady_config_set(&server->config, key, value)) return false;
	if (!strncmp(key, "window_border_", 14) ||
			!strncmp(key, "window_titlebar", 15)) {
		struct shady_toplevel *toplevel;
		wl_list_for_each(toplevel, &server->all_toplevels, all_link) {
			shady_toplevel_refresh_border(toplevel);
			shady_titlebar_refresh(toplevel);
		}
	}
	if (server->renderer) shady_render_schedule_all_outputs(server);
	return true;
}

static void *host_module_state(shady_host host, const char *module_name) {
	return shady_module_state(HOST(host), module_name);
}

static void *host_window_state(shady_window window, const char *module_name) {
	return shady_toplevel_module_state(WINDOW(window), module_name);
}

static size_t host_window_count(shady_host host) {
	size_t count = 0;
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &HOST(host)->all_toplevels, all_link) count++;
	return count;
}

static shady_window host_window_at(shady_host host, size_t index) {
	size_t i = 0;
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &HOST(host)->all_toplevels, all_link) {
		if (i++ == index) return (shady_window)toplevel;
	}
	return NULL;
}

static shady_window host_focused_window(shady_host host) {
	struct shady_server *server = HOST(host);
	struct wlr_surface *surface = server->seat->keyboard_state.focused_surface;
	if (surface) {
		struct wlr_surface *root = wlr_surface_get_root_surface(surface);
		struct wlr_xdg_toplevel *xdg = wlr_xdg_toplevel_try_from_wlr_surface(root);
		if (xdg) {
			struct shady_toplevel *toplevel;
			wl_list_for_each(toplevel, &server->all_toplevels, all_link) {
				if (toplevel->xdg_toplevel == xdg) return (shady_window)toplevel;
			}
		}
	}

	/* Logical focus is tracked by server->toplevels even when no physical
	 * keyboard exists (notably headless/spatial automation). */
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel->mapped && toplevel->scene_tree &&
				toplevel->scene_tree->node.enabled)
			return (shady_window)toplevel;
	}
	return NULL;
}

static const char *host_window_title(shady_window window) {
	struct shady_toplevel *toplevel = WINDOW(window);
	return toplevel && toplevel->xdg_toplevel && toplevel->xdg_toplevel->title
		? toplevel->xdg_toplevel->title : "";
}

static const char *host_window_app_id(shady_window window) {
	struct shady_toplevel *toplevel = WINDOW(window);
	return toplevel && toplevel->xdg_toplevel && toplevel->xdg_toplevel->app_id
		? toplevel->xdg_toplevel->app_id : "";
}

bool shady_plugin_window_valid(shady_host host, shady_window window) {
	struct shady_toplevel *needle = WINDOW(window);
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &HOST(host)->all_toplevels, all_link) {
		if (toplevel == needle) return true;
	}
	return false;
}

static bool host_window_mapped(shady_window window) {
	struct shady_toplevel *toplevel = WINDOW(window);
	return toplevel && toplevel->xdg_toplevel &&
		toplevel->xdg_toplevel->base &&
		toplevel->xdg_toplevel->base->surface &&
		toplevel->xdg_toplevel->base->surface->mapped;
}

static bool host_window_visible(shady_window window) {
	struct shady_toplevel *toplevel = WINDOW(window);
	return toplevel && toplevel->scene_tree && toplevel->scene_tree->node.enabled;
}

static bool host_window_maximized(shady_window window) {
	struct shady_toplevel *toplevel = WINDOW(window);
	return toplevel && toplevel->maximized;
}

static bool host_window_fullscreen(shady_window window) {
	struct shady_toplevel *toplevel = WINDOW(window);
	return toplevel && toplevel->fullscreen;
}

static bool host_window_focus(shady_host host, shady_window window) {
	if (!shady_plugin_window_valid(host, window)) return false;
	struct shady_toplevel *toplevel = WINDOW(window);
	if (!toplevel->scene_tree || !toplevel->scene_tree->node.enabled) return false;
	focus_toplevel(toplevel);
	return true;
}

static bool host_window_close(shady_host host, shady_window window) {
	if (!shady_plugin_window_valid(host, window)) return false;
	struct shady_toplevel *toplevel = WINDOW(window);
	if (!toplevel->xdg_toplevel) return false;
	shady_close_animation_begin_window(HOST(host), toplevel);
	return true;
}

static bool host_window_set_maximized(shady_host host, shady_window window,
		bool enabled) {
	if (!shady_plugin_window_valid(host, window)) return false;
	shady_toplevel_set_maximized(WINDOW(window), enabled);
	return true;
}

static bool host_window_set_fullscreen(shady_host host, shady_window window,
		bool enabled) {
	if (!shady_plugin_window_valid(host, window)) return false;
	shady_toplevel_set_fullscreen(WINDOW(window), enabled);
	return true;
}

static bool host_window_size(shady_window window, int *width, int *height) {
	struct shady_toplevel *toplevel = WINDOW(window);
	if (!toplevel || !toplevel->xdg_toplevel || !toplevel->xdg_toplevel->base ||
			!toplevel->xdg_toplevel->base->surface) return false;
	struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
	if (width) *width = surface->current.width;
	if (height) *height = surface->current.height;
	return surface->current.width > 0 && surface->current.height > 0;
}

static bool host_window_position(shady_window window, double *x, double *y, float *z) {
	struct shady_toplevel *toplevel = WINDOW(window);
	if (!toplevel || !toplevel->scene_tree) return false;
	if (x) *x = toplevel->scene_tree->node.x;
	if (y) *y = toplevel->scene_tree->node.y;
	if (z) {
		const struct shady_toplevel_experimental_state *spatial =
			shady_spatial_toplevel_state_const(toplevel);
		*z = spatial ? spatial->z : 0.0f;
	}
	return true;
}

static bool host_window_set_position(shady_host host, shady_window window,
		double x, double y, float z) {
	if (!shady_plugin_window_valid(host, window)) return false;
	struct shady_toplevel *toplevel = WINDOW(window);
	if (!toplevel->scene_tree) return false;
	wlr_scene_node_set_position(&toplevel->scene_tree->node, (int)x, (int)y);
	struct shady_toplevel_experimental_state *spatial =
		shady_spatial_toplevel_state(toplevel);
	if (spatial) spatial->z = z;
	shady_modules_toplevel_moved(toplevel, x, y);
	if (HOST(host)->renderer) shady_render_schedule_all_outputs(HOST(host));
	return true;
}

static size_t host_output_count(shady_host host) {
	size_t count = 0;
	struct shady_output *output;
	wl_list_for_each(output, &HOST(host)->outputs, link) count++;
	return count;
}

static shady_output host_output_at(shady_host host, size_t index) {
	size_t i = 0;
	struct shady_output *output;
	wl_list_for_each(output, &HOST(host)->outputs, link) {
		if (i++ == index) return (shady_output)output;
	}
	return NULL;
}

static const char *host_output_name(shady_output output) {
	struct shady_output *o = OUTPUT(output);
	return o && o->wlr_output && o->wlr_output->name ? o->wlr_output->name : "";
}

static bool host_output_valid(shady_host host, shady_output output) {
	struct shady_output *needle = OUTPUT(output);
	struct shady_output *o;
	wl_list_for_each(o, &HOST(host)->outputs, link) {
		if (o == needle) return true;
	}
	return false;
}

static void host_output_size(shady_output output, int *width, int *height) {
	struct shady_output *o = OUTPUT(output);
	int w = 0, h = 0;
	if (o && o->wlr_output) wlr_output_effective_resolution(o->wlr_output, &w, &h);
	/* During early backend startup an output can exist before its effective
	 * mode is known. Preserve caller fallbacks instead of replacing them with
	 * an unusable 0x0 size. */
	if (w > 0 && width) *width = w;
	if (h > 0 && height) *height = h;
}

static float host_output_scale(shady_output output) {
	struct shady_output *o = OUTPUT(output);
	return o && o->wlr_output ? o->wlr_output->scale : 1.0f;
}

static shady_seat host_seat(shady_host host) {
	return (shady_seat)HOST(host)->seat;
}

static const char *host_seat_name(shady_seat seat) {
	struct wlr_seat *s = SEAT(seat);
	return s && s->name ? s->name : "";
}

static size_t host_module_count(shady_host host) {
	return HOST(host)->modules.count;
}

static shady_module_handle host_module_at(shady_host host, size_t index) {
	struct shady_server *server = HOST(host);
	if (index >= server->modules.count) return NULL;
	return (shady_module_handle)server->modules.modules[index];
}

static const char *host_module_name(shady_module_handle module) {
	const struct shady_module *m = MODULE(module);
	return m && m->name ? m->name : "";
}

static bool host_module_active(shady_host host, shady_module_handle module) {
	struct shady_server *server = HOST(host);
	for (size_t i = 0; i < server->modules.count; i++) {
		if (server->modules.modules[i] == MODULE(module)) return server->modules.active[i];
	}
	return false;
}

static size_t host_workspace_count(shady_host host) {
	return shady_workspace_count(HOST(host));
}

static const char *host_workspace_at(shady_host host, size_t index) {
	return shady_workspace_name_at(HOST(host), index);
}

static const char *host_current_workspace(shady_host host) {
	return shady_workspace_current_name(HOST(host));
}

static bool host_workspace_switch(shady_host host, const char *name) {
	return shady_workspace_switch(HOST(host), name);
}

static const char *host_window_workspace(shady_window window) {
	return shady_workspace_toplevel_name(WINDOW(window));
}

static bool host_window_move_to_workspace(shady_host host, shady_window window,
		const char *name) {
	if (!shady_plugin_window_valid(host, window)) return false;
	return shady_workspace_move_toplevel(WINDOW(window), name);
}

static const char *host_event_name(uint32_t event_type) {
	return shady_event_name((enum shady_event_type)event_type);
}

static void *event_callback_owner(shady_event_callback callback) {
	Dl_info info = {0};
	if (!callback || dladdr((void *)callback, &info) == 0) return NULL;
	return info.dli_fbase;
}

static shady_subscription_id host_subscribe_event_handle(shady_host host,
		uint32_t event_type, shady_event_callback callback, void *user_data) {
	return shady_event_subscribe_owned(HOST(host), event_type, callback,
		user_data, event_callback_owner(callback));
}

static bool host_subscribe_event(shady_host host, uint32_t event_type,
		shady_event_callback callback, void *user_data) {
	return host_subscribe_event_handle(host, event_type, callback, user_data) != 0;
}

static bool host_unsubscribe_event(shady_host host,
		shady_subscription_id subscription) {
	return shady_event_unsubscribe(HOST(host), subscription);
}

static void host_schedule_render(shady_host host) {
	struct shady_server *server = HOST(host);
	if (server->renderer) shady_render_schedule_all_outputs(server);
}

static void host_output_schedule_render(shady_host host, shady_output output) {
	if (!host_output_valid(host, output)) return;
	struct shady_server *server = HOST(host);
	if (server->renderer) shady_render_schedule_output(OUTPUT(output));
}

static bool host_window_set_water_effect(shady_host host, shady_window window,
		float amplitude, float frequency, float speed, float phase) {
	if (!shady_plugin_window_valid(host, window)) return false;
	struct shady_toplevel *toplevel = WINDOW(window);
	if (amplitude < 0.f) amplitude = 0.f;
	if (amplitude > 0.18f) amplitude = 0.18f;
	if (frequency < 0.1f) frequency = 0.1f;
	if (frequency > 30.f) frequency = 30.f;
	if (speed < -12.f) speed = -12.f;
	if (speed > 12.f) speed = 12.f;
	toplevel->water_amplitude = amplitude;
	toplevel->water_frequency = frequency;
	toplevel->water_speed = speed;
	toplevel->water_phase = phase;
	if (HOST(host)->renderer) shady_render_schedule_all_outputs(HOST(host));
	return true;
}

static bool host_window_water_effect(shady_window window,
		float *amplitude, float *frequency, float *speed, float *phase) {
	struct shady_toplevel *toplevel = WINDOW(window);
	if (!toplevel) return false;
	if (amplitude) *amplitude = toplevel->water_amplitude;
	if (frequency) *frequency = toplevel->water_frequency;
	if (speed) *speed = toplevel->water_speed;
	if (phase) *phase = toplevel->water_phase;
	return true;
}

static bool host_window_set_water_surface(shady_host host, shady_window window,
		float fresnel, float specular, float caustic, float tint) {
	if (!shady_plugin_window_valid(host, window)) return false;
	struct shady_toplevel *toplevel = WINDOW(window);
	if (fresnel < 0.f) fresnel = 0.f;
	if (fresnel > 2.f) fresnel = 2.f;
	if (specular < 0.f) specular = 0.f;
	if (specular > 2.f) specular = 2.f;
	if (caustic < 0.f) caustic = 0.f;
	if (caustic > 2.f) caustic = 2.f;
	if (tint < 0.f) tint = 0.f;
	if (tint > 1.f) tint = 1.f;
	toplevel->water_fresnel = fresnel;
	toplevel->water_specular = specular;
	toplevel->water_caustic = caustic;
	toplevel->water_tint = tint;
	if (HOST(host)->renderer) shady_render_schedule_all_outputs(HOST(host));
	return true;
}

static bool host_window_water_surface(shady_window window,
		float *fresnel, float *specular, float *caustic, float *tint) {
	struct shady_toplevel *toplevel = WINDOW(window);
	if (!toplevel) return false;
	if (fresnel) *fresnel = toplevel->water_fresnel;
	if (specular) *specular = toplevel->water_specular;
	if (caustic) *caustic = toplevel->water_caustic;
	if (tint) *tint = toplevel->water_tint;
	return true;
}

static bool host_window_set_border(shady_host host, shady_window window,
		float width, float r, float g, float b, float a) {
	if (!shady_plugin_window_valid(host, window)) return false;
	if (width < 0.f) width = 0.f;
	if (width > 32.f) width = 32.f;
	float color[4] = {r, g, b, a};
	for (size_t i = 0; i < 4; i++) {
		if (color[i] < 0.f) color[i] = 0.f;
		if (color[i] > 1.f) color[i] = 1.f;
	}
	struct shady_toplevel *toplevel = WINDOW(window);
	toplevel->border_override = true;
	toplevel->border_width = width;
	for (size_t i = 0; i < 4; i++) toplevel->border_color[i] = color[i];
	shady_toplevel_refresh_border(toplevel);
	if (HOST(host)->renderer) shady_render_schedule_all_outputs(HOST(host));
	return true;
}

static bool host_window_border(shady_window window,
		float *width, float color[4], bool *overridden) {
	struct shady_toplevel *toplevel = WINDOW(window);
	if (!toplevel) return false;
	shady_toplevel_get_border(toplevel, width, color);
	if (overridden) *overridden = toplevel->border_override;
	return true;
}

static bool host_window_reset_border(shady_host host, shady_window window) {
	if (!shady_plugin_window_valid(host, window)) return false;
	struct shady_toplevel *toplevel = WINDOW(window);
	toplevel->border_override = false;
	shady_toplevel_refresh_border(toplevel);
	if (HOST(host)->renderer) shady_render_schedule_all_outputs(HOST(host));
	return true;
}

static bool host_window_set_close_effect(shady_host host, shady_window window,
		const struct shady_close_effect *effect) {
	if (!shady_plugin_window_valid(host, window) || !effect) return false;
	if (effect->style > SHADY_CLOSE_EFFECT_SLIDE_FADE ||
			!isfinite(effect->duration) || !isfinite(effect->strength) ||
			!isfinite(effect->direction_x) || !isfinite(effect->direction_y)) return false;
	struct shady_toplevel *toplevel = WINDOW(window);
	toplevel->close_effect_override = true;
	toplevel->close_effect_style = effect->style;
	toplevel->close_effect_duration = effect->duration;
	if (toplevel->close_effect_duration < .08f) toplevel->close_effect_duration = .08f;
	if (toplevel->close_effect_duration > 2.5f) toplevel->close_effect_duration = 2.5f;
	toplevel->close_effect_strength = effect->strength;
	if (toplevel->close_effect_strength < 0.f) toplevel->close_effect_strength = 0.f;
	if (toplevel->close_effect_strength > 2.f) toplevel->close_effect_strength = 2.f;
	toplevel->close_effect_direction_x = effect->direction_x;
	toplevel->close_effect_direction_y = effect->direction_y;
	if (toplevel->close_effect_direction_x < -2.f) toplevel->close_effect_direction_x = -2.f;
	if (toplevel->close_effect_direction_x > 2.f) toplevel->close_effect_direction_x = 2.f;
	if (toplevel->close_effect_direction_y < -2.f) toplevel->close_effect_direction_y = -2.f;
	if (toplevel->close_effect_direction_y > 2.f) toplevel->close_effect_direction_y = 2.f;
	return true;
}

static bool host_window_close_effect(shady_window window,
		struct shady_close_effect *effect, bool *overridden) {
	struct shady_toplevel *toplevel = WINDOW(window);
	if (!toplevel || !effect) return false;
	shady_close_animation_get_effect(toplevel, &effect->style, &effect->duration,
		&effect->strength, &effect->direction_x, &effect->direction_y);
	if (overridden) *overridden = toplevel->close_effect_override;
	return true;
}

static bool host_window_reset_close_effect(shady_host host, shady_window window) {
	if (!shady_plugin_window_valid(host, window)) return false;
	WINDOW(window)->close_effect_override = false;
	return true;
}

static void host_terminate(shady_host host) {
	struct shady_server *server = HOST(host);
	if (server->wl_display) wl_display_terminate(server->wl_display);
}

void *shady_plugin_owner_from_address(void *address) {
	Dl_info info = {0};
	return address && dladdr(address, &info) != 0 ? info.dli_fbase : NULL;
}

static shady_shader_program host_shader_program_create(shady_host host,
		const char *vertex_path, const char *fragment_path) {
	return shady_render_plugin_shader_create(HOST(host), shady_plugin_owner_from_address(__builtin_return_address(0)),
		vertex_path, fragment_path);
}

static bool host_shader_program_destroy(shady_host host, shady_shader_program program) {
	return shady_render_plugin_shader_destroy(HOST(host), shady_plugin_owner_from_address(__builtin_return_address(0)), program);
}

static bool host_shader_uniform_float(shady_host host, shady_shader_program program,
		const char *name, float value) {
	return shady_render_plugin_shader_uniform_float(HOST(host), shady_plugin_owner_from_address(__builtin_return_address(0)),
		program, name, value);
}

static bool host_shader_uniform_int(shady_host host, shady_shader_program program,
		const char *name, int value) {
	return shady_render_plugin_shader_uniform_int(HOST(host), shady_plugin_owner_from_address(__builtin_return_address(0)),
		program, name, value);
}

static bool host_shader_uniform_vec2(shady_host host, shady_shader_program program,
		const char *name, float x, float y) {
	return shady_render_plugin_shader_uniform_vec2(HOST(host), shady_plugin_owner_from_address(__builtin_return_address(0)),
		program, name, x, y);
}

static bool host_shader_uniform_vec4(shady_host host, shady_shader_program program,
		const char *name, float x, float y, float z, float w) {
	return shady_render_plugin_shader_uniform_vec4(HOST(host), shady_plugin_owner_from_address(__builtin_return_address(0)),
		program, name, x, y, z, w);
}

static bool host_shader_draw_fullscreen(shady_host host, shady_shader_program program) {
	return shady_render_plugin_shader_draw_fullscreen(HOST(host), shady_plugin_owner_from_address(__builtin_return_address(0)),
		program);
}

static bool host_shader_draw_fullscreen_scene(shady_host host, shady_shader_program program) {
	return shady_render_plugin_shader_draw_fullscreen_scene(HOST(host),
		shady_plugin_owner_from_address(__builtin_return_address(0)), program);
}

static shady_render_hook_id host_render_hook_add(shady_host host, uint32_t stage,
		shady_render_callback callback, void *user_data) {
	return shady_render_plugin_hook_add(HOST(host), shady_plugin_owner_from_address(__builtin_return_address(0)),
		stage, callback, user_data);
}

static bool host_render_hook_remove(shady_host host, shady_render_hook_id hook) {
	return shady_render_plugin_hook_remove(HOST(host),
		shady_plugin_owner_from_address(__builtin_return_address(0)), hook);
}

static bool host_window_set_shader(shady_host host, shady_window window,
		shady_shader_program program) {
	if (!shady_plugin_window_valid(host, window) || !program) return false;
	void *owner = shady_plugin_owner_from_address(__builtin_return_address(0));
	if (!owner || !shady_render_plugin_shader_valid(HOST(host), owner, program))
		return false;
	struct shady_toplevel *toplevel = WINDOW(window);
	if (toplevel->plugin_shader_owner != owner)
		memset(toplevel->plugin_shader_params, 0, sizeof(toplevel->plugin_shader_params));
	toplevel->plugin_shader_program = program;
	toplevel->plugin_shader_owner = owner;
	if (HOST(host)->renderer) shady_render_schedule_all_outputs(HOST(host));
	return true;
}

static bool host_window_reset_shader(shady_host host, shady_window window) {
	if (!shady_plugin_window_valid(host, window)) return false;
	void *owner = shady_plugin_owner_from_address(__builtin_return_address(0));
	struct shady_toplevel *toplevel = WINDOW(window);
	if (toplevel->plugin_shader_owner && owner &&
			toplevel->plugin_shader_owner != owner)
		return false;
	toplevel->plugin_shader_program = 0;
	toplevel->plugin_shader_owner = NULL;
	memset(toplevel->plugin_shader_params, 0, sizeof(toplevel->plugin_shader_params));
	if (!toplevel->plugin_shader_source_owner ||
			toplevel->plugin_shader_source_owner == owner) {
		toplevel->plugin_shader_source = NULL;
		toplevel->plugin_shader_source_owner = NULL;
	}
	if (HOST(host)->renderer) shady_render_schedule_all_outputs(HOST(host));
	return true;
}

static bool host_window_set_shader_params(shady_host host, shady_window window,
		const float params[SHADY_WINDOW_SHADER_PARAMS * 4]) {
	if (!params || !shady_plugin_window_valid(host, window)) return false;
	void *owner = shady_plugin_owner_from_address(__builtin_return_address(0));
	struct shady_toplevel *toplevel = WINDOW(window);
	if (!owner || !toplevel->plugin_shader_program || toplevel->plugin_shader_owner != owner)
		return false;
	for (size_t i = 0; i < SHADY_WINDOW_SHADER_PARAMS * 4; i++)
		if (!isfinite(params[i])) return false;
	if (!memcmp(toplevel->plugin_shader_params, params, sizeof(toplevel->plugin_shader_params)))
		return true;
	memcpy(toplevel->plugin_shader_params, params, sizeof(toplevel->plugin_shader_params));
	if (HOST(host)->renderer) shady_render_schedule_all_outputs(HOST(host));
	return true;
}

static shady_shader_program host_window_shader(shady_window window) {
	struct shady_toplevel *toplevel = WINDOW(window);
	return toplevel ? toplevel->plugin_shader_program : 0;
}

static bool host_window_set_shader_source(shady_host host, shady_window target,
		shady_window source) {
	if (!shady_plugin_window_valid(host, target) || !shady_plugin_window_valid(host, source) ||
			target == source)
		return false;
	void *owner = shady_plugin_owner_from_address(__builtin_return_address(0));
	if (!owner) return false;
	struct shady_toplevel *toplevel = WINDOW(target);
	if (!toplevel->plugin_shader_program || toplevel->plugin_shader_owner != owner)
		return false;
	toplevel->plugin_shader_source = WINDOW(source);
	toplevel->plugin_shader_source_owner = owner;
	if (HOST(host)->renderer) shady_render_schedule_all_outputs(HOST(host));
	return true;
}

static bool host_window_reset_shader_source(shady_host host, shady_window target) {
	if (!shady_plugin_window_valid(host, target)) return false;
	void *owner = shady_plugin_owner_from_address(__builtin_return_address(0));
	struct shady_toplevel *toplevel = WINDOW(target);
	if (toplevel->plugin_shader_source_owner && owner &&
			toplevel->plugin_shader_source_owner != owner)
		return false;
	toplevel->plugin_shader_source = NULL;
	toplevel->plugin_shader_source_owner = NULL;
	if (HOST(host)->renderer) shady_render_schedule_all_outputs(HOST(host));
	return true;
}

static shady_window host_window_shader_source(shady_window target) {
	struct shady_toplevel *toplevel = WINDOW(target);
	return toplevel ? (shady_window)toplevel->plugin_shader_source : NULL;
}

const void *shady_plugin_query_api(shady_host host, const char *name, uint32_t version) {
    (void)host;
    if (name && !strcmp(name, SHADY_MOTION_API) && version == SHADY_MOTION_API_VERSION)
        return &shady_motion_api;
    if (name && !strcmp(name, SHADY_REPRESENTATION_API) && version == SHADY_REPRESENTATION_API_VERSION)
        return &shady_representation_api;
#if SHADY_HAS_SPATIAL
    if (name && !strcmp(name, SHADY_ENVIRONMENT_API) && version == SHADY_ENVIRONMENT_API_VERSION)
        return &shady_environment_api;
#endif
    return NULL;
}

const struct shady_plugin_api_v1 shady_plugin_api = {
	.query_api = shady_plugin_query_api,
	.abi_version = SHADY_PLUGIN_ABI_V1,
	.struct_size = sizeof(struct shady_plugin_api_v1),
	.log = host_log,
	.has_capability = host_has_capability,
	.config_set = host_config_set,
	.module_state = host_module_state,
	.window_state = host_window_state,
	.window_count = host_window_count,
	.window_at = host_window_at,
	.focused_window = host_focused_window,
	.window_title = host_window_title,
	.window_app_id = host_window_app_id,
	.window_valid = shady_plugin_window_valid,
	.window_mapped = host_window_mapped,
	.window_visible = host_window_visible,
	.window_maximized = host_window_maximized,
	.window_fullscreen = host_window_fullscreen,
	.window_focus = host_window_focus,
	.window_close = host_window_close,
	.window_set_maximized = host_window_set_maximized,
	.window_set_fullscreen = host_window_set_fullscreen,
	.window_size = host_window_size,
	.window_position = host_window_position,
	.window_set_position = host_window_set_position,
	.output_count = host_output_count,
	.output_at = host_output_at,
	.output_name = host_output_name,
	.output_valid = host_output_valid,
	.output_size = host_output_size,
	.output_scale = host_output_scale,
	.seat = host_seat,
	.seat_name = host_seat_name,
	.module_count = host_module_count,
	.module_at = host_module_at,
	.module_name = host_module_name,
	.module_active = host_module_active,
	.workspace_count = host_workspace_count,
	.workspace_at = host_workspace_at,
	.current_workspace = host_current_workspace,
	.workspace_switch = host_workspace_switch,
	.window_workspace = host_window_workspace,
	.window_move_to_workspace = host_window_move_to_workspace,
	.output_schedule_render = host_output_schedule_render,
	.window_set_water_effect = host_window_set_water_effect,
	.window_water_effect = host_window_water_effect,
	.window_set_water_surface = host_window_set_water_surface,
	.window_water_surface = host_window_water_surface,
	.window_set_border = host_window_set_border,
	.window_border = host_window_border,
	.window_reset_border = host_window_reset_border,
	.window_set_close_effect = host_window_set_close_effect,
	.window_close_effect = host_window_close_effect,
	.window_reset_close_effect = host_window_reset_close_effect,
	.event_name = host_event_name,
	.subscribe_event = host_subscribe_event,
	.subscribe_event_handle = host_subscribe_event_handle,
	.unsubscribe_event = host_unsubscribe_event,
	.schedule_render = host_schedule_render,
	.terminate = host_terminate,
	.shader_program_create = host_shader_program_create,
	.shader_program_destroy = host_shader_program_destroy,
	.shader_uniform_float = host_shader_uniform_float,
	.shader_uniform_int = host_shader_uniform_int,
	.shader_uniform_vec2 = host_shader_uniform_vec2,
	.shader_uniform_vec4 = host_shader_uniform_vec4,
	.shader_draw_fullscreen = host_shader_draw_fullscreen,
	.render_hook_add = host_render_hook_add,
	.render_hook_remove = host_render_hook_remove,
	.window_set_shader = host_window_set_shader,
	.window_reset_shader = host_window_reset_shader,
	.window_shader = host_window_shader,
	.window_set_representation = host_window_set_representation,
	.window_reset_representation = host_window_reset_representation,
	.window_representation = host_window_representation,
	.window_set_representation_provider = host_window_set_representation_provider,
	.window_reset_representation_provider = host_window_reset_representation_provider,
	.window_representation_state = host_window_representation_state,
	.window_set_shader_source = host_window_set_shader_source,
	.window_reset_shader_source = host_window_reset_shader_source,
	.window_shader_source = host_window_shader_source,
	.window_set_shader_params = host_window_set_shader_params,
	.shader_draw_fullscreen_scene = host_shader_draw_fullscreen_scene,
};

