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
#include "../modules/spatial/state.h"
#include "../modules/close_animation/close_animation.h"
#include "../modules/workspace/workspace.h"
#include "../shady.h"

#define HOST(h) ((struct shady_server *)(h))
#define WINDOW(w) ((struct shady_toplevel *)(w))
#define OUTPUT(o) ((struct shady_output *)(o))
#define SEAT(s) ((struct wlr_seat *)(s))
#define MODULE(m) ((const struct shady_module *)(m))
#define SHADY_MAX_REPRESENTATION_STATE (1024u * 1024u)

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

static bool host_window_valid(shady_host host, shady_window window) {
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
	if (!host_window_valid(host, window)) return false;
	struct shady_toplevel *toplevel = WINDOW(window);
	if (!toplevel->scene_tree || !toplevel->scene_tree->node.enabled) return false;
	focus_toplevel(toplevel);
	return true;
}

static bool host_window_close(shady_host host, shady_window window) {
	if (!host_window_valid(host, window)) return false;
	struct shady_toplevel *toplevel = WINDOW(window);
	if (!toplevel->xdg_toplevel) return false;
	shady_close_animation_begin_window(HOST(host), toplevel);
	return true;
}

static bool host_window_set_maximized(shady_host host, shady_window window,
		bool enabled) {
	if (!host_window_valid(host, window)) return false;
	shady_toplevel_set_maximized(WINDOW(window), enabled);
	return true;
}

static bool host_window_set_fullscreen(shady_host host, shady_window window,
		bool enabled) {
	if (!host_window_valid(host, window)) return false;
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
	if (!host_window_valid(host, window)) return false;
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
	if (!host_window_valid(host, window)) return false;
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
	if (!host_window_valid(host, window)) return false;
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
	if (!host_window_valid(host, window)) return false;
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
	if (!host_window_valid(host, window)) return false;
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
	if (!host_window_valid(host, window)) return false;
	struct shady_toplevel *toplevel = WINDOW(window);
	toplevel->border_override = false;
	shady_toplevel_refresh_border(toplevel);
	if (HOST(host)->renderer) shady_render_schedule_all_outputs(HOST(host));
	return true;
}

static bool host_window_set_close_effect(shady_host host, shady_window window,
		const struct shady_close_effect *effect) {
	if (!host_window_valid(host, window) || !effect) return false;
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
	if (!host_window_valid(host, window)) return false;
	WINDOW(window)->close_effect_override = false;
	return true;
}

static void host_terminate(shady_host host) {
	struct shady_server *server = HOST(host);
	if (server->wl_display) wl_display_terminate(server->wl_display);
}

static void *plugin_owner_from_address(void *address) {
	Dl_info info = {0};
	return address && dladdr(address, &info) != 0 ? info.dli_fbase : NULL;
}

static bool plugin_callback_owned_by(void *owner, void *callback) {
	return owner && callback && plugin_owner_from_address(callback) == owner;
}

static bool plugin_representation_provider_callbacks_valid(
		void *owner, const struct shady_window_representation_provider *provider) {
	if (!owner || !provider) return false;
	if (provider->state_init &&
			!plugin_callback_owned_by(owner, (void *)provider->state_init)) return false;
	if (provider->state_destroy &&
			!plugin_callback_owned_by(owner, (void *)provider->state_destroy)) return false;
	if (provider->update &&
			!plugin_callback_owned_by(owner, (void *)provider->update)) return false;
	if (provider->model &&
			!plugin_callback_owned_by(owner, (void *)provider->model)) return false;
	if (provider->collision &&
			!plugin_callback_owned_by(owner, (void *)provider->collision)) return false;
	return true;
}

static void plugin_representation_provider_detach(struct shady_toplevel *toplevel) {
	if (!toplevel || !toplevel->plugin_representation_provider_active) return;
	struct shady_window_representation_provider provider =
		toplevel->plugin_representation_provider;
	void *owner = toplevel->plugin_representation_provider_owner;
	void *state = toplevel->plugin_representation_state;
	if (provider.state_destroy &&
			plugin_callback_owned_by(owner, (void *)provider.state_destroy)) {
		provider.state_destroy((shady_host)toplevel->server, (shady_window)toplevel,
			state, provider.user_data);
	}
	free(state);
	memset(&toplevel->plugin_representation_provider, 0,
		sizeof(toplevel->plugin_representation_provider));
	toplevel->plugin_representation_provider_active = false;
	toplevel->plugin_representation_provider_anchor = NULL;
	toplevel->plugin_representation_provider_owner = NULL;
	toplevel->plugin_representation_state = NULL;
	toplevel->plugin_representation_state_size = 0;
}

void shady_plugin_representation_cleanup_owner(struct shady_server *server, void *owner) {
	if (!server || !owner) return;
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->all_toplevels, all_link) {
		if (toplevel->plugin_representation_provider_owner == owner)
			plugin_representation_provider_detach(toplevel);
	}
}

void shady_plugin_window_cleanup(struct shady_toplevel *toplevel) {
	plugin_representation_provider_detach(toplevel);
}

void shady_plugin_representation_tick(struct shady_server *server, float dt) {
	if (!server || dt <= 0.f) return;
	bool needs_render = false;
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->all_toplevels, all_link) {
		if (!toplevel->mapped || !toplevel->plugin_representation_provider_active)
			continue;
		shady_representation_update_callback callback =
			toplevel->plugin_representation_provider.update;
		if (!callback || !plugin_callback_owned_by(
				toplevel->plugin_representation_provider_owner, (void *)callback))
			continue;
		if (callback((shady_host)server, (shady_window)toplevel, dt,
				toplevel->plugin_representation_state,
				toplevel->plugin_representation_provider.user_data))
			needs_render = true;
	}
	if (needs_render && server->renderer)
		shady_render_schedule_all_outputs(server);
}

static void plugin_cleanup_owner_resources(struct shady_server *server, void *owner) {
	shady_plugin_representation_cleanup_owner(server, owner);
	shady_render_plugin_cleanup_owner(server, owner);
}

bool shady_toplevel_representation_base(const struct shady_toplevel *toplevel,
		struct shady_window_representation *representation) {
	if (!toplevel || !representation) return false;
	if (toplevel->plugin_representation_provider_active) {
		*representation = toplevel->plugin_representation_provider.base;
		return representation->kind == SHADY_WINDOW_REPRESENTATION_BOX;
	}
	if (!toplevel->plugin_representation_override) return false;
	*representation = toplevel->plugin_representation;
	return representation->kind == SHADY_WINDOW_REPRESENTATION_BOX;
}

bool shady_toplevel_representation_model(const struct shady_toplevel *toplevel,
		const struct shady_representation_context *context,
		struct shady_representation_model *model) {
	if (!toplevel || !context || !model) return false;
	struct shady_window_representation base;
	if (!shady_toplevel_representation_base(toplevel, &base)) return false;
	*model = (struct shady_representation_model){
		.struct_size = sizeof(*model),
		.center_x = context->center_x,
		.center_y = context->center_y,
		.center_z = context->center_z,
		.width = base.width,
		.height = base.height,
		.depth = base.depth,
		.tilt_x = context->tilt_x,
		.tilt_y = context->tilt_y,
		.hide_titlebar = base.hide_titlebar,
	};
	shady_representation_model_callback callback =
		toplevel->plugin_representation_provider.model;
	if (toplevel->plugin_representation_provider_active && callback &&
			plugin_callback_owned_by(toplevel->plugin_representation_provider_owner,
				(void *)callback)) {
		struct shady_representation_model custom = *model;
		if (callback((shady_host)toplevel->server, (shady_window)toplevel,
				context, &custom, toplevel->plugin_representation_state,
				toplevel->plugin_representation_provider.user_data) &&
				custom.struct_size >= sizeof(custom) && custom.width > 0.f &&
				custom.height > 0.f && custom.depth > 0.f)
			*model = custom;
	}
	return true;
}

bool shady_toplevel_representation_collision(const struct shady_toplevel *toplevel,
		const struct shady_representation_context *context,
		const struct shady_representation_model *model,
		struct shady_collision_box *box) {
	if (!toplevel || !context || !model || !box) return false;
	*box = (struct shady_collision_box){
		.struct_size = sizeof(*box),
		.center = {model->center_x, model->center_y, model->center_z},
		.half = {model->width * .5f, model->height * .5f, model->depth * .5f},
	};
	shady_representation_collision_callback callback =
		toplevel->plugin_representation_provider.collision;
	if (toplevel->plugin_representation_provider_active && callback &&
			plugin_callback_owned_by(toplevel->plugin_representation_provider_owner,
				(void *)callback)) {
		struct shady_collision_box custom = *box;
		if (callback((shady_host)toplevel->server, (shady_window)toplevel,
				context, &custom, toplevel->plugin_representation_state,
				toplevel->plugin_representation_provider.user_data) &&
				custom.struct_size >= sizeof(custom) && custom.half[0] > 0.f &&
				custom.half[1] > 0.f && custom.half[2] > 0.f)
			*box = custom;
	}
	return true;
}

static shady_shader_program host_shader_program_create(shady_host host,
		const char *vertex_path, const char *fragment_path) {
	return shady_render_plugin_shader_create(HOST(host), plugin_owner_from_address(__builtin_return_address(0)),
		vertex_path, fragment_path);
}

static bool host_shader_program_destroy(shady_host host, shady_shader_program program) {
	return shady_render_plugin_shader_destroy(HOST(host), plugin_owner_from_address(__builtin_return_address(0)), program);
}

static bool host_shader_uniform_float(shady_host host, shady_shader_program program,
		const char *name, float value) {
	return shady_render_plugin_shader_uniform_float(HOST(host), plugin_owner_from_address(__builtin_return_address(0)),
		program, name, value);
}

static bool host_shader_uniform_int(shady_host host, shady_shader_program program,
		const char *name, int value) {
	return shady_render_plugin_shader_uniform_int(HOST(host), plugin_owner_from_address(__builtin_return_address(0)),
		program, name, value);
}

static bool host_shader_uniform_vec2(shady_host host, shady_shader_program program,
		const char *name, float x, float y) {
	return shady_render_plugin_shader_uniform_vec2(HOST(host), plugin_owner_from_address(__builtin_return_address(0)),
		program, name, x, y);
}

static bool host_shader_uniform_vec4(shady_host host, shady_shader_program program,
		const char *name, float x, float y, float z, float w) {
	return shady_render_plugin_shader_uniform_vec4(HOST(host), plugin_owner_from_address(__builtin_return_address(0)),
		program, name, x, y, z, w);
}

static bool host_shader_draw_fullscreen(shady_host host, shady_shader_program program) {
	return shady_render_plugin_shader_draw_fullscreen(HOST(host), plugin_owner_from_address(__builtin_return_address(0)),
		program);
}

static shady_render_hook_id host_render_hook_add(shady_host host, uint32_t stage,
		shady_render_callback callback, void *user_data) {
	return shady_render_plugin_hook_add(HOST(host), plugin_owner_from_address(__builtin_return_address(0)),
		stage, callback, user_data);
}

static bool host_render_hook_remove(shady_host host, shady_render_hook_id hook) {
	return shady_render_plugin_hook_remove(HOST(host),
		plugin_owner_from_address(__builtin_return_address(0)), hook);
}

static bool host_window_set_shader(shady_host host, shady_window window,
		shady_shader_program program) {
	if (!host_window_valid(host, window) || !program) return false;
	void *owner = plugin_owner_from_address(__builtin_return_address(0));
	if (!owner || !shady_render_plugin_shader_valid(HOST(host), owner, program))
		return false;
	struct shady_toplevel *toplevel = WINDOW(window);
	toplevel->plugin_shader_program = program;
	toplevel->plugin_shader_owner = owner;
	if (HOST(host)->renderer) shady_render_schedule_all_outputs(HOST(host));
	return true;
}

static bool host_window_reset_shader(shady_host host, shady_window window) {
	if (!host_window_valid(host, window)) return false;
	void *owner = plugin_owner_from_address(__builtin_return_address(0));
	struct shady_toplevel *toplevel = WINDOW(window);
	if (toplevel->plugin_shader_owner && owner &&
			toplevel->plugin_shader_owner != owner)
		return false;
	toplevel->plugin_shader_program = 0;
	toplevel->plugin_shader_owner = NULL;
	if (HOST(host)->renderer) shady_render_schedule_all_outputs(HOST(host));
	return true;
}

static shady_shader_program host_window_shader(shady_window window) {
	struct shady_toplevel *toplevel = WINDOW(window);
	return toplevel ? toplevel->plugin_shader_program : 0;
}

static bool host_window_set_representation(shady_host host, shady_window window,
		const struct shady_window_representation *representation) {
	if (!host_window_valid(host, window) || !representation ||
			representation->struct_size < sizeof(*representation) ||
			representation->kind != SHADY_WINDOW_REPRESENTATION_BOX ||
			representation->width <= 0.f || representation->height <= 0.f ||
			representation->depth <= 0.f)
		return false;
	void *owner = plugin_owner_from_address(__builtin_return_address(0));
	if (!owner) return false;
	struct shady_toplevel *toplevel = WINDOW(window);
	if (toplevel->plugin_representation_provider_active &&
			toplevel->plugin_representation_provider_owner != owner)
		return false;
	if (toplevel->plugin_representation_provider_owner == owner)
		plugin_representation_provider_detach(toplevel);
	toplevel->plugin_representation = *representation;
	toplevel->plugin_representation_override = true;
	toplevel->plugin_representation_owner = owner;
	if (HOST(host)->renderer) shady_render_schedule_all_outputs(HOST(host));
	return true;
}

static bool host_window_reset_representation(shady_host host, shady_window window) {
	if (!host_window_valid(host, window)) return false;
	void *owner = plugin_owner_from_address(__builtin_return_address(0));
	struct shady_toplevel *toplevel = WINDOW(window);
	if (toplevel->plugin_representation_owner && owner &&
			toplevel->plugin_representation_owner != owner)
		return false;
	memset(&toplevel->plugin_representation, 0,
		sizeof(toplevel->plugin_representation));
	toplevel->plugin_representation_override = false;
	toplevel->plugin_representation_owner = NULL;
	if (HOST(host)->renderer) shady_render_schedule_all_outputs(HOST(host));
	return true;
}

static bool host_window_set_representation_provider(shady_host host,
		shady_window window,
		const struct shady_window_representation_provider *provider) {
	if (!host_window_valid(host, window) || !provider ||
			provider->struct_size < sizeof(*provider) ||
			provider->base.struct_size < sizeof(provider->base) ||
			provider->base.kind != SHADY_WINDOW_REPRESENTATION_BOX ||
			provider->base.width <= 0.f || provider->base.height <= 0.f ||
			provider->base.depth <= 0.f)
		return false;
	void *owner = plugin_owner_from_address((void *)provider);
	if (!owner || provider->state_size > SHADY_MAX_REPRESENTATION_STATE ||
			!plugin_representation_provider_callbacks_valid(owner, provider))
		return false;
	struct shady_toplevel *toplevel = WINDOW(window);
	if (toplevel->plugin_representation_override &&
			toplevel->plugin_representation_owner != owner)
		return false;
	if (toplevel->plugin_representation_provider_active &&
			toplevel->plugin_representation_provider_owner != owner)
		return false;
	void *state = provider->state_size > 0 ? calloc(1, provider->state_size) : NULL;
	if (provider->state_size > 0 && !state) return false;
	if (provider->state_init &&
			!provider->state_init(host, window, state, provider->user_data)) {
		free(state);
		return false;
	}
	if (toplevel->plugin_representation_provider_active)
		plugin_representation_provider_detach(toplevel);
	if (toplevel->plugin_representation_owner == owner) {
		memset(&toplevel->plugin_representation, 0,
			sizeof(toplevel->plugin_representation));
		toplevel->plugin_representation_override = false;
		toplevel->plugin_representation_owner = NULL;
	}
	toplevel->plugin_representation_provider = *provider;
	toplevel->plugin_representation_provider_active = true;
	toplevel->plugin_representation_provider_anchor = provider;
	toplevel->plugin_representation_provider_owner = owner;
	toplevel->plugin_representation_state = state;
	toplevel->plugin_representation_state_size = provider->state_size;
	if (HOST(host)->renderer) shady_render_schedule_all_outputs(HOST(host));
	return true;
}

static bool host_window_reset_representation_provider(shady_host host,
		shady_window window,
		const struct shady_window_representation_provider *provider) {
	if (!host_window_valid(host, window) || !provider) return false;
	void *owner = plugin_owner_from_address((void *)provider);
	if (!owner) return false;
	struct shady_toplevel *toplevel = WINDOW(window);
	if (!toplevel->plugin_representation_provider_active ||
			toplevel->plugin_representation_provider_owner != owner ||
			toplevel->plugin_representation_provider_anchor != provider)
		return false;
	plugin_representation_provider_detach(toplevel);
	if (HOST(host)->renderer) shady_render_schedule_all_outputs(HOST(host));
	return true;
}

static void *host_window_representation_state(shady_host host, shady_window window,
		const struct shady_window_representation_provider *provider,
		size_t *state_size) {
	if (state_size) *state_size = 0;
	if (!host_window_valid(host, window) || !provider) return NULL;
	void *owner = plugin_owner_from_address((void *)provider);
	if (!owner) return NULL;
	struct shady_toplevel *toplevel = WINDOW(window);
	if (!toplevel->plugin_representation_provider_active ||
			toplevel->plugin_representation_provider_owner != owner ||
			toplevel->plugin_representation_provider_anchor != provider)
		return NULL;
	if (state_size) *state_size = toplevel->plugin_representation_state_size;
	return toplevel->plugin_representation_state;
}

static bool host_window_representation(shady_window window,
		struct shady_window_representation *representation, bool *overridden) {
	struct shady_toplevel *toplevel = WINDOW(window);
	if (!toplevel || !representation) return false;
	if (toplevel->plugin_representation_provider_active)
		*representation = toplevel->plugin_representation_provider.base;
	else if (toplevel->plugin_representation_override)
		*representation = toplevel->plugin_representation;
	else
		*representation = (struct shady_window_representation){
			.struct_size = sizeof(*representation),
			.kind = SHADY_WINDOW_REPRESENTATION_DEFAULT,
		};
	if (overridden) *overridden = toplevel->plugin_representation_override ||
		toplevel->plugin_representation_provider_active;
	return true;
}

static const struct shady_plugin_api_v1 plugin_api = {
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
	.window_valid = host_window_valid,
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
};

static bool list_contains(const char *const *items, const char *value) {
	if (!items || !value) return false;
	for (size_t i = 0; items[i]; i++) if (strcmp(items[i], value) == 0) return true;
	return false;
}

static char *plugin_reload_copy(const char *path) {
	size_t len = strlen(path) + sizeof(".reload-XXXXXX");
	char *tmp = malloc(len);
	if (!tmp) return NULL;
	snprintf(tmp, len, "%s.reload-XXXXXX", path);
	int out = mkstemp(tmp);
	if (out < 0) {
		free(tmp);
		return NULL;
	}
	int in = open(path, O_RDONLY);
	if (in < 0) {
		close(out);
		unlink(tmp);
		free(tmp);
		return NULL;
	}
	char buffer[65536];
	bool ok = true;
	for (;;) {
		ssize_t n = read(in, buffer, sizeof(buffer));
		if (n == 0) break;
		if (n < 0) { ok = false; break; }
		ssize_t off = 0;
		while (off < n) {
			ssize_t written = write(out, buffer + off, (size_t)(n - off));
			if (written <= 0) { ok = false; break; }
			off += written;
		}
		if (!ok) break;
	}
	close(in);
	close(out);
	if (!ok) {
		unlink(tmp);
		free(tmp);
		return NULL;
	}
	return tmp;
}

static bool plugin_open(struct shady_server *server, const char *path,
		void **handle_out, void **base_out, const struct shady_module **module_out,
		uint32_t *abi_out, const struct shady_plugin_v2 **v2_out) {
	void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (!handle) {
		wlr_log(WLR_ERROR, "plugin: failed to load %s: %s", path, dlerror());
		return false;
	}

	dlerror();
	shady_plugin_entry_v2_fn entry_v2 =
		(shady_plugin_entry_v2_fn)dlsym(handle, SHADY_PLUGIN_ENTRY_V2);
	const char *v2_error = dlerror();
	if (!v2_error && entry_v2) {
		Dl_info info = {0};
		void *base = dladdr((void *)entry_v2, &info) != 0 ? info.dli_fbase : NULL;
		const struct shady_plugin_v2 *descriptor =
			entry_v2(SHADY_PLUGIN_ABI_V2, &plugin_api, (shady_host)server);
		if (!descriptor || descriptor->struct_size < sizeof(*descriptor) ||
				!descriptor->module || !descriptor->module->name) {
			wlr_log(WLR_ERROR, "plugin: %s rejected ABI v%u", path, SHADY_PLUGIN_ABI_V2);
			shady_event_unsubscribe_owner(server, base);
			plugin_cleanup_owner_resources(server, base);
			dlclose(handle);
			return false;
		}
		*handle_out = handle;
		*base_out = base;
		*module_out = descriptor->module;
		*abi_out = SHADY_PLUGIN_ABI_V2;
		*v2_out = descriptor;
		return true;
	}

	dlerror();
	shady_plugin_entry_v1_fn entry_v1 =
		(shady_plugin_entry_v1_fn)dlsym(handle, SHADY_PLUGIN_ENTRY_V1);
	const char *v1_error = dlerror();
	if (v1_error || !entry_v1) {
		wlr_log(WLR_ERROR, "plugin: %s exports neither %s nor %s",
			path, SHADY_PLUGIN_ENTRY_V2, SHADY_PLUGIN_ENTRY_V1);
		dlclose(handle);
		return false;
	}
	Dl_info info = {0};
	void *base = dladdr((void *)entry_v1, &info) != 0 ? info.dli_fbase : NULL;
	const struct shady_module *module =
		entry_v1(SHADY_PLUGIN_ABI_V1, &plugin_api, (shady_host)server);
	if (!module || !module->name) {
		wlr_log(WLR_ERROR, "plugin: %s rejected ABI v%u", path, SHADY_PLUGIN_ABI_V1);
		shady_event_unsubscribe_owner(server, base);
		plugin_cleanup_owner_resources(server, base);
		dlclose(handle);
		return false;
	}
	*handle_out = handle;
	*base_out = base;
	*module_out = module;
	*abi_out = SHADY_PLUGIN_ABI_V1;
	*v2_out = NULL;
	return true;
}

static bool plugin_stateless(const struct shady_module *module) {
	return module && module->state_size == 0 && module->toplevel_state_size == 0;
}

struct plugin_window_snapshot {
	struct shady_toplevel *window;
	void *data;
	size_t size;
};

struct plugin_reload_snapshot {
	uint32_t schema_version;
	void *module_data;
	size_t module_size;
	struct plugin_window_snapshot *windows;
	size_t window_count;
};

static void plugin_snapshot_finish(struct plugin_reload_snapshot *snapshot) {
	if (!snapshot) return;
	free(snapshot->module_data);
	for (size_t i = 0; i < snapshot->window_count; i++) {
		free(snapshot->windows[i].data);
	}
	free(snapshot->windows);
	memset(snapshot, 0, sizeof(*snapshot));
}

static bool plugin_snapshot_capture(struct shady_server *server, size_t index,
		struct plugin_reload_snapshot *snapshot) {
	struct shady_module_manager *manager = &server->modules;
	const struct shady_module *module = manager->modules[index];
	const struct shady_plugin_v2 *v2 = manager->plugin_v2[index];
	memset(snapshot, 0, sizeof(*snapshot));
	if (!manager->active[index] || plugin_stateless(module)) return true;
	if (manager->plugin_abi[index] != SHADY_PLUGIN_ABI_V2 || !v2) {
		wlr_log(WLR_ERROR, "plugin: stateful reload requires ABI v2: %s", module->name);
		return false;
	}
	snapshot->schema_version = v2->state_schema_version;

	if (module->state_size > 0) {
		if (!v2->module_snapshot_size || !v2->save_module_state) {
			wlr_log(WLR_ERROR, "plugin: %s lacks module state save callbacks", module->name);
			return false;
		}
		snapshot->module_size = v2->module_snapshot_size((shady_host)server,
			manager->state[index]);
		if (snapshot->module_size == 0) {
			wlr_log(WLR_ERROR, "plugin: %s returned empty module snapshot", module->name);
			return false;
		}
		snapshot->module_data = malloc(snapshot->module_size);
		if (!snapshot->module_data || !v2->save_module_state((shady_host)server,
				manager->state[index], snapshot->module_data, snapshot->module_size)) {
			wlr_log(WLR_ERROR, "plugin: %s failed to save module state", module->name);
			plugin_snapshot_finish(snapshot);
			return false;
		}
	}

	if (module->toplevel_state_size > 0) {
		if (!v2->window_snapshot_size || !v2->save_window_state) {
			wlr_log(WLR_ERROR, "plugin: %s lacks window state save callbacks", module->name);
			plugin_snapshot_finish(snapshot);
			return false;
		}
		struct shady_toplevel *window;
		wl_list_for_each(window, &server->all_toplevels, all_link) snapshot->window_count++;
		if (snapshot->window_count > 0) {
			snapshot->windows = calloc(snapshot->window_count, sizeof(*snapshot->windows));
			if (!snapshot->windows) {
				plugin_snapshot_finish(snapshot);
				return false;
			}
		}
		size_t i = 0;
		wl_list_for_each(window, &server->all_toplevels, all_link) {
			struct plugin_window_snapshot *entry = &snapshot->windows[i++];
			entry->window = window;
			entry->size = v2->window_snapshot_size((shady_host)server,
				(shady_window)window, window->module_state[index]);
			if (entry->size == 0) {
				wlr_log(WLR_ERROR, "plugin: %s returned empty window snapshot", module->name);
				plugin_snapshot_finish(snapshot);
				return false;
			}
			entry->data = malloc(entry->size);
			if (!entry->data || !v2->save_window_state((shady_host)server,
					(shady_window)window, window->module_state[index],
					entry->data, entry->size)) {
				wlr_log(WLR_ERROR, "plugin: %s failed to save window state", module->name);
				plugin_snapshot_finish(snapshot);
				return false;
			}
		}
	}
	return true;
}

static bool plugin_snapshot_restore(struct shady_server *server, size_t index,
		const struct shady_plugin_v2 *v2,
		const struct plugin_reload_snapshot *snapshot) {
	const struct shady_module *module = server->modules.modules[index];
	if (snapshot->module_data) {
		if (!v2 || !v2->restore_module_state || module->state_size == 0 ||
				!v2->restore_module_state((shady_host)server, server->modules.state[index],
				snapshot->module_data, snapshot->module_size, snapshot->schema_version)) {
			wlr_log(WLR_ERROR, "plugin: %s failed to restore module state", module->name);
			return false;
		}
	}
	for (size_t i = 0; i < snapshot->window_count; i++) {
		const struct plugin_window_snapshot *entry = &snapshot->windows[i];
		if (!v2 || !v2->restore_window_state || module->toplevel_state_size == 0 ||
				!v2->restore_window_state((shady_host)server, (shady_window)entry->window,
					entry->window->module_state[index], entry->data, entry->size,
					snapshot->schema_version)) {
			wlr_log(WLR_ERROR, "plugin: %s failed to restore window state", module->name);
			return false;
		}
	}
	return true;
}

static bool plugin_has_active_dependents(struct shady_server *server, size_t index) {
	const struct shady_module *plugin = server->modules.modules[index];
	if (!plugin || !plugin->provides) return false;
	for (size_t i = 0; i < server->modules.count; i++) {
		if (i == index || !server->modules.active[i]) continue;
		const struct shady_module *module = server->modules.modules[i];
		for (size_t p = 0; plugin->provides[p]; p++) {
			if (list_contains(module->requires, plugin->provides[p]) ||
					list_contains(module->optional_requires, plugin->provides[p])) {
				wlr_log(WLR_ERROR, "plugin: %s is used by active module %s via %s",
					plugin->name, module->name, plugin->provides[p]);
				return true;
			}
		}
	}
	return false;
}

static bool plugin_contract_available(struct shady_server *server,
		const struct shady_module *module, size_t slot) {
	if (module->requires) {
		for (size_t i = 0; module->requires[i]; i++) {
			if (!shady_module_has_capability(server, module->requires[i])) {
				wlr_log(WLR_ERROR, "plugin: %s requires unavailable capability %s",
					module->name, module->requires[i]);
				return false;
			}
		}
	}
	if (module->provides) {
		for (size_t p = 0; module->provides[p]; p++) {
			for (size_t i = 0; i < server->modules.count; i++) {
				if (i == slot || !server->modules.active[i]) continue;
				if (list_contains(server->modules.modules[i]->provides, module->provides[p])) {
					wlr_log(WLR_ERROR, "plugin: capability %s already has active provider %s",
						module->provides[p], server->modules.modules[i]->name);
					return false;
				}
			}
		}
	}
	return true;
}

bool shady_plugin_load(struct shady_server *server, const char *path) {
	if (!path || !*path) {
		wlr_log(WLR_ERROR, "plugin: empty path");
		return false;
	}
	if (server->modules.count >= SHADY_MAX_MODULES) {
		wlr_log(WLR_ERROR, "plugin: module limit reached");
		return false;
	}
	/* Never dlopen the developer-controlled source path directly. Build tools
	 * commonly replace or truncate a .so in place, which can corrupt mappings
	 * belonging to the currently running plugin before reload even begins.
	 * Load a private snapshot and retain the original path only as the reload
	 * source. */
	char *load_path = plugin_reload_copy(path);
	if (!load_path) {
		wlr_log(WLR_ERROR, "plugin: failed to stage load copy for %s", path);
		return false;
	}
	void *handle = NULL, *base = NULL;
	const struct shady_module *module = NULL;
	uint32_t abi = 0;
	const struct shady_plugin_v2 *v2 = NULL;
	bool opened = plugin_open(server, load_path, &handle, &base, &module, &abi, &v2);
	unlink(load_path);
	free(load_path);
	if (!opened) return false;
	char *path_copy = strdup(path);
	char *name_copy = strdup(module->name);
	if (!path_copy || !name_copy) {
		free(path_copy);
		free(name_copy);
		wlr_log(WLR_ERROR, "plugin: failed to retain metadata for %s", module->name);
		shady_event_unsubscribe_owner(server, base);
		plugin_cleanup_owner_resources(server, base);
		dlclose(handle);
		return false;
	}
	if (!shady_modules_register(&server->modules, module)) {
		wlr_log(WLR_ERROR, "plugin: failed to register module %s from %s", module->name, path);
		free(path_copy);
		free(name_copy);
		shady_event_unsubscribe_owner(server, base);
		plugin_cleanup_owner_resources(server, base);
		dlclose(handle);
		return false;
	}
	size_t index = server->modules.count - 1;
	server->modules.plugin_handle[index] = handle;
	server->modules.plugin_base[index] = base;
	server->modules.plugin_abi[index] = abi;
	server->modules.plugin_v2[index] = v2;
	server->modules.plugin_path[index] = path_copy;
	server->modules.plugin_name[index] = name_copy;
	wlr_log(WLR_INFO, "plugin: loaded %s from %s", module->name, path);
	return true;
}

static bool plugin_unload_now(struct shady_server *server, const char *name) {
	ssize_t found = shady_module_index_by_name(server, name);
	if (found < 0) return false;
	size_t index = (size_t)found;
	struct shady_module_manager *manager = &server->modules;
	const struct shady_module *module = manager->modules[index];
	if (!manager->plugin_handle[index]) {
		wlr_log(WLR_ERROR, "plugin: %s is not a loaded external plugin", name);
		return false;
	}
	if (!plugin_stateless(module)) {
		wlr_log(WLR_ERROR, "plugin: hot unload requires stateless plugin: %s", name);
		return false;
	}
	if (plugin_has_active_dependents(server, index)) return false;
	if (manager->module_started[index]) {
		shady_event_emit_module(server, SHADY_EVENT_MODULE_STOPPED, module);
		if (module->stop) module->stop(server);
		manager->module_started[index] = false;
	}
	if (manager->active[index]) {
		if (module->destroy) module->destroy(server);
		manager->active[index] = false;
	}
	shady_event_unsubscribe_owner(server, manager->plugin_base[index]);
	plugin_cleanup_owner_resources(server, manager->plugin_base[index]);
	dlclose(manager->plugin_handle[index]);
	manager->plugin_handle[index] = NULL;
	manager->plugin_base[index] = NULL;
	manager->plugin_abi[index] = 0;
	manager->plugin_v2[index] = NULL;
	manager->plugin_stub[index] = (struct shady_module){
		.name = manager->plugin_name[index],
	};
	manager->modules[index] = &manager->plugin_stub[index];
	wlr_log(WLR_INFO, "plugin: unloaded %s", name);
	return true;
}

struct plugin_window_swap {
	struct shady_toplevel *window;
	void *old_state;
	void *new_state;
};

static bool plugin_provides_compatible(const struct shady_module *old_module,
		const struct shady_module *new_module) {
	if (!old_module->provides) return true;
	for (size_t i = 0; old_module->provides[i]; i++) {
		if (!list_contains(new_module->provides, old_module->provides[i])) return false;
	}
	return true;
}

static bool plugin_reload_now(struct shady_server *server, const char *name) {
	ssize_t found = shady_module_index_by_name(server, name);
	if (found < 0) return false;
	size_t index = (size_t)found;
	struct shady_module_manager *manager = &server->modules;
	if (!manager->plugin_path[index] || !manager->plugin_handle[index]) {
		wlr_log(WLR_ERROR, "plugin: %s is not currently loaded", name);
		return false;
	}

	const struct shady_module *old_module = manager->modules[index];
	const struct shady_plugin_v2 *old_v2 = manager->plugin_v2[index];
	uint32_t old_abi = manager->plugin_abi[index];
	void *old_handle = manager->plugin_handle[index];
	void *old_base = manager->plugin_base[index];
	bool was_active = manager->active[index];
	bool was_started = manager->module_started[index];
	void *old_module_state = manager->state[index];

	char *reload_path = plugin_reload_copy(manager->plugin_path[index]);
	if (!reload_path) {
		wlr_log(WLR_ERROR, "plugin: failed to stage reload copy for %s", name);
		return false;
	}
	void *new_handle = NULL, *new_base = NULL;
	const struct shady_module *new_module = NULL;
	uint32_t new_abi = 0;
	const struct shady_plugin_v2 *new_v2 = NULL;
	bool opened = plugin_open(server, reload_path, &new_handle, &new_base,
		&new_module, &new_abi, &new_v2);
	unlink(reload_path);
	free(reload_path);
	if (!opened) return false;

	if (strcmp(new_module->name, manager->plugin_name[index]) != 0 ||
			!plugin_contract_available(server, new_module, index) ||
			!plugin_provides_compatible(old_module, new_module)) {
		wlr_log(WLR_ERROR, "plugin: reload contract rejected for %s", name);
		shady_event_unsubscribe_owner(server, new_base);
		plugin_cleanup_owner_resources(server, new_base);
		dlclose(new_handle);
		return false;
	}

	bool stateful = was_active && (!plugin_stateless(old_module) || !plugin_stateless(new_module));
	if (stateful && (old_abi != SHADY_PLUGIN_ABI_V2 || new_abi != SHADY_PLUGIN_ABI_V2 ||
			!old_v2 || !new_v2)) {
		wlr_log(WLR_ERROR, "plugin: stateful reload requires ABI v2 on both sides: %s", name);
		shady_event_unsubscribe_owner(server, new_base);
		plugin_cleanup_owner_resources(server, new_base);
		dlclose(new_handle);
		return false;
	}
	if (was_active && old_module->state_size > 0 && new_module->state_size == 0) {
		wlr_log(WLR_ERROR, "plugin: %s cannot drop module state during hot reload", name);
		shady_event_unsubscribe_owner(server, new_base);
		plugin_cleanup_owner_resources(server, new_base);
		dlclose(new_handle);
		return false;
	}
	if (was_active && old_module->toplevel_state_size > 0 && new_module->toplevel_state_size == 0) {
		wlr_log(WLR_ERROR, "plugin: %s cannot drop window state during hot reload", name);
		shady_event_unsubscribe_owner(server, new_base);
		plugin_cleanup_owner_resources(server, new_base);
		dlclose(new_handle);
		return false;
	}

	struct plugin_reload_snapshot snapshot;
	if (!plugin_snapshot_capture(server, index, &snapshot)) {
		shady_event_unsubscribe_owner(server, new_base);
		plugin_cleanup_owner_resources(server, new_base);
		dlclose(new_handle);
		return false;
	}

	size_t window_count = 0;
	struct shady_toplevel *window;
	wl_list_for_each(window, &server->all_toplevels, all_link) window_count++;
	struct plugin_window_swap *swaps = window_count > 0
		? calloc(window_count, sizeof(*swaps)) : NULL;
	if (window_count > 0 && !swaps) {
		plugin_snapshot_finish(&snapshot);
		shady_event_unsubscribe_owner(server, new_base);
		plugin_cleanup_owner_resources(server, new_base);
		dlclose(new_handle);
		return false;
	}
	void *new_module_state = was_active && new_module->state_size > 0
		? calloc(1, new_module->state_size) : NULL;
	if (was_active && new_module->state_size > 0 && !new_module_state) {
		free(swaps);
		plugin_snapshot_finish(&snapshot);
		shady_event_unsubscribe_owner(server, new_base);
		plugin_cleanup_owner_resources(server, new_base);
		dlclose(new_handle);
		return false;
	}
	size_t wi = 0;
	wl_list_for_each(window, &server->all_toplevels, all_link) {
		swaps[wi].window = window;
		swaps[wi].old_state = window->module_state[index];
		if (was_active && new_module->toplevel_state_size > 0) {
			swaps[wi].new_state = calloc(1, new_module->toplevel_state_size);
			if (!swaps[wi].new_state) {
				for (size_t j = 0; j < wi; j++) free(swaps[j].new_state);
				free(new_module_state);
				free(swaps);
				plugin_snapshot_finish(&snapshot);
				shady_event_unsubscribe_owner(server, new_base);
				plugin_cleanup_owner_resources(server, new_base);
				dlclose(new_handle);
				return false;
			}
		}
		wi++;
	}

	if (was_started) {
		shady_event_emit_module(server, SHADY_EVENT_MODULE_STOPPED, old_module);
		if (old_module->stop) old_module->stop(server);
		manager->module_started[index] = false;
	}
	if (was_active && old_module->destroy) old_module->destroy(server);
	manager->active[index] = false;

	manager->modules[index] = new_module;
	manager->plugin_handle[index] = new_handle;
	manager->plugin_base[index] = new_base;
	manager->plugin_abi[index] = new_abi;
	manager->plugin_v2[index] = new_v2;
	manager->state[index] = new_module_state;
	for (size_t i = 0; i < window_count; i++) {
		swaps[i].window->module_state[index] = swaps[i].new_state;
	}

	bool new_ok = true;
	if (was_active) {
		new_ok = !new_module->init || new_module->init(server);
		if (new_ok) {
			manager->active[index] = true;
			new_ok = plugin_snapshot_restore(server, index, new_v2, &snapshot);
		}
	}

	if (!new_ok) {
		wlr_log(WLR_ERROR, "plugin: reload failed, rolling back %s", name);
		if (manager->active[index] && new_module->destroy) new_module->destroy(server);
		manager->active[index] = false;
		shady_event_unsubscribe_owner(server, new_base);
		plugin_cleanup_owner_resources(server, new_base);
		dlclose(new_handle);
		free(manager->state[index]);
		for (size_t i = 0; i < window_count; i++) free(swaps[i].window->module_state[index]);

		manager->modules[index] = old_module;
		manager->plugin_handle[index] = old_handle;
		manager->plugin_base[index] = old_base;
		manager->plugin_abi[index] = old_abi;
		manager->plugin_v2[index] = old_v2;
		manager->state[index] = old_module_state;
		for (size_t i = 0; i < window_count; i++) swaps[i].window->module_state[index] = swaps[i].old_state;
		if (was_active) {
			bool old_ok = !old_module->init || old_module->init(server);
			manager->active[index] = old_ok;
			if (old_ok && stateful && old_v2) {
				old_ok = plugin_snapshot_restore(server, index, old_v2, &snapshot);
				manager->active[index] = old_ok;
			}
			if (!old_ok) wlr_log(WLR_ERROR, "plugin: rollback re-init failed for %s", name);
		}
		if (was_started && manager->active[index]) {
			if (old_module->start) old_module->start(server);
			manager->module_started[index] = true;
			shady_event_emit_module(server, SHADY_EVENT_MODULE_STARTED, old_module);
		}
		plugin_snapshot_finish(&snapshot);
		free(swaps);
		return false;
	}

	shady_event_unsubscribe_owner(server, old_base);
	plugin_cleanup_owner_resources(server, old_base);
	dlclose(old_handle);
	free(old_module_state);
	for (size_t i = 0; i < window_count; i++) free(swaps[i].old_state);
	if (was_started) {
		if (new_module->start) new_module->start(server);
		manager->module_started[index] = true;
		shady_event_emit_module(server, SHADY_EVENT_MODULE_STARTED, new_module);
	}
	plugin_snapshot_finish(&snapshot);
	free(swaps);
	wlr_log(WLR_INFO, "plugin: reloaded %s from %s (ABI v%u, schema %u)",
		name, manager->plugin_path[index], new_abi,
		new_v2 ? new_v2->state_schema_version : 0);
	return true;
}

enum deferred_plugin_action_kind {
	DEFERRED_PLUGIN_UNLOAD,
	DEFERRED_PLUGIN_RELOAD,
};

struct deferred_plugin_action {
	struct shady_server *server;
	char *name;
	enum deferred_plugin_action_kind kind;
};

static void run_deferred_plugin_action(void *data) {
	struct deferred_plugin_action *action = data;
	if (action->kind == DEFERRED_PLUGIN_RELOAD) {
		plugin_reload_now(action->server, action->name);
	} else {
		plugin_unload_now(action->server, action->name);
	}
	free(action->name);
	free(action);
}

static bool defer_plugin_action(struct shady_server *server, const char *name,
		enum deferred_plugin_action_kind kind) {
	if (!server->wl_display) return false;
	struct deferred_plugin_action *action = calloc(1, sizeof(*action));
	if (!action) return false;
	action->server = server;
	action->name = strdup(name);
	action->kind = kind;
	if (!action->name) {
		free(action);
		return false;
	}
	struct wl_event_source *source = wl_event_loop_add_idle(
		wl_display_get_event_loop(server->wl_display), run_deferred_plugin_action, action);
	if (!source) {
		free(action->name);
		free(action);
		return false;
	}
	return true;
}

bool shady_plugin_unload(struct shady_server *server, const char *name) {
	if (shady_events_dispatching(server)) {
		return defer_plugin_action(server, name, DEFERRED_PLUGIN_UNLOAD);
	}
	return plugin_unload_now(server, name);
}

bool shady_plugin_reload(struct shady_server *server, const char *name) {
	if (shady_events_dispatching(server)) {
		return defer_plugin_action(server, name, DEFERRED_PLUGIN_RELOAD);
	}
	return plugin_reload_now(server, name);
}
