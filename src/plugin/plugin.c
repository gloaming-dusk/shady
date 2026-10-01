#include "plugin.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include <shady/plugin.h>

#include "../module/module.h"
#include "../render/render.h"
#include "../shady.h"

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
	return shady_config_set(&HOST(host)->config, key, value);
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
	wl_list_for_each(toplevel, &HOST(host)->toplevels, link) count++;
	return count;
}

static shady_window host_window_at(shady_host host, size_t index) {
	size_t i = 0;
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &HOST(host)->toplevels, link) {
		if (i++ == index) return (shady_window)toplevel;
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
	wl_list_for_each(toplevel, &HOST(host)->toplevels, link) {
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

static bool host_window_focus(shady_host host, shady_window window) {
	if (!host_window_valid(host, window)) return false;
	focus_toplevel(WINDOW(window));
	return true;
}

static bool host_window_close(shady_host host, shady_window window) {
	if (!host_window_valid(host, window)) return false;
	struct shady_toplevel *toplevel = WINDOW(window);
	if (!toplevel->xdg_toplevel) return false;
	wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
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
	if (width) *width = w;
	if (height) *height = h;
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

static void host_schedule_render(shady_host host) {
	struct shady_server *server = HOST(host);
	if (server->renderer) shady_render_schedule_all_outputs(server);
}

static void host_terminate(shady_host host) {
	struct shady_server *server = HOST(host);
	if (server->wl_display) wl_display_terminate(server->wl_display);
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
	.window_title = host_window_title,
	.window_app_id = host_window_app_id,
	.window_valid = host_window_valid,
	.window_mapped = host_window_mapped,
	.window_focus = host_window_focus,
	.window_close = host_window_close,
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
	.schedule_render = host_schedule_render,
	.terminate = host_terminate,
};

bool shady_plugin_load(struct shady_server *server, const char *path) {
	if (!path || !*path) {
		wlr_log(WLR_ERROR, "plugin: empty path");
		return false;
	}
	if (server->modules.count >= SHADY_MAX_MODULES) {
		wlr_log(WLR_ERROR, "plugin: module limit reached");
		return false;
	}

	void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (!handle) {
		wlr_log(WLR_ERROR, "plugin: failed to load %s: %s", path, dlerror());
		return false;
	}

	dlerror();
	shady_plugin_entry_v1_fn entry =
		(shady_plugin_entry_v1_fn)dlsym(handle, SHADY_PLUGIN_ENTRY_V1);
	const char *error = dlerror();
	if (error || !entry) {
		wlr_log(WLR_ERROR, "plugin: %s does not export %s: %s",
			path, SHADY_PLUGIN_ENTRY_V1, error ? error : "symbol missing");
		dlclose(handle);
		return false;
	}

	const struct shady_module *module =
		entry(SHADY_PLUGIN_ABI_V1, &plugin_api, (shady_host)server);
	if (!module || !module->name) {
		wlr_log(WLR_ERROR, "plugin: %s rejected ABI v%u",
			path, SHADY_PLUGIN_ABI_V1);
		dlclose(handle);
		return false;
	}

	if (!shady_modules_register(&server->modules, module)) {
		wlr_log(WLR_ERROR, "plugin: failed to register module %s from %s",
			module->name, path);
		dlclose(handle);
		return false;
	}

	server->modules.plugin_handle[server->modules.count - 1] = handle;
	wlr_log(WLR_INFO, "plugin: loaded %s from %s", module->name, path);
	return true;
}
