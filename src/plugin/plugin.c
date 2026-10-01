#define _GNU_SOURCE
#include "plugin.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include <shady/plugin.h>

#include "../module/module.h"
#include "../event/event.h"
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
	.event_name = host_event_name,
	.subscribe_event = host_subscribe_event,
	.subscribe_event_handle = host_subscribe_event_handle,
	.unsubscribe_event = host_unsubscribe_event,
	.schedule_render = host_schedule_render,
	.terminate = host_terminate,
};

static bool list_contains(const char *const *items, const char *value) {
	if (!items || !value) return false;
	for (size_t i = 0; items[i]; i++) if (strcmp(items[i], value) == 0) return true;
	return false;
}

static bool plugin_open(struct shady_server *server, const char *path,
		void **handle_out, void **base_out, const struct shady_module **module_out) {
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
	Dl_info info = {0};
	void *base = dladdr((void *)entry, &info) != 0 ? info.dli_fbase : NULL;
	const struct shady_module *module =
		entry(SHADY_PLUGIN_ABI_V1, &plugin_api, (shady_host)server);
	if (!module || !module->name) {
		wlr_log(WLR_ERROR, "plugin: %s rejected ABI v%u", path, SHADY_PLUGIN_ABI_V1);
		shady_event_unsubscribe_owner(server, base);
		dlclose(handle);
		return false;
	}
	*handle_out = handle;
	*base_out = base;
	*module_out = module;
	return true;
}

static bool plugin_stateless(const struct shady_module *module) {
	return module && module->state_size == 0 && module->toplevel_state_size == 0;
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
	void *handle = NULL, *base = NULL;
	const struct shady_module *module = NULL;
	if (!plugin_open(server, path, &handle, &base, &module)) return false;
	char *path_copy = strdup(path);
	char *name_copy = strdup(module->name);
	if (!path_copy || !name_copy) {
		free(path_copy);
		free(name_copy);
		shady_event_unsubscribe_owner(server, base);
		dlclose(handle);
		wlr_log(WLR_ERROR, "plugin: failed to retain metadata for %s", module->name);
		return false;
	}
	if (!shady_modules_register(&server->modules, module)) {
		wlr_log(WLR_ERROR, "plugin: failed to register module %s from %s", module->name, path);
		free(path_copy);
		free(name_copy);
		shady_event_unsubscribe_owner(server, base);
		dlclose(handle);
		return false;
	}
	size_t index = server->modules.count - 1;
	server->modules.plugin_handle[index] = handle;
	server->modules.plugin_base[index] = base;
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
	dlclose(manager->plugin_handle[index]);
	manager->plugin_handle[index] = NULL;
	manager->plugin_base[index] = NULL;
	manager->plugin_stub[index] = (struct shady_module){
		.name = manager->plugin_name[index],
	};
	manager->modules[index] = &manager->plugin_stub[index];
	wlr_log(WLR_INFO, "plugin: unloaded %s", name);
	return true;
}

static bool plugin_reload_now(struct shady_server *server, const char *name) {
	ssize_t found = shady_module_index_by_name(server, name);
	if (found < 0) return false;
	size_t index = (size_t)found;
	struct shady_module_manager *manager = &server->modules;
	if (!manager->plugin_path[index]) {
		wlr_log(WLR_ERROR, "plugin: %s has no reloadable plugin path", name);
		return false;
	}
	if (manager->plugin_handle[index] && !plugin_unload_now(server, name)) return false;
	char *path = strdup(manager->plugin_path[index]);
	if (!path) return false;
	void *handle = NULL, *base = NULL;
	const struct shady_module *module = NULL;
	if (!plugin_open(server, path, &handle, &base, &module)) { free(path); return false; }
	if (strcmp(module->name, manager->plugin_name[index]) != 0 || !plugin_stateless(module) ||
			!plugin_contract_available(server, module, index)) {
		wlr_log(WLR_ERROR, "plugin: reload contract rejected for %s", name);
		shady_event_unsubscribe_owner(server, base);
		dlclose(handle);
		free(path);
		return false;
	}
	manager->modules[index] = module;
	manager->plugin_handle[index] = handle;
	manager->plugin_base[index] = base;
	if (module->init && !module->init(server)) {
		wlr_log(WLR_ERROR, "plugin: re-init failed for %s", name);
		shady_event_unsubscribe_owner(server, base);
		dlclose(handle);
		manager->plugin_handle[index] = NULL;
		manager->plugin_base[index] = NULL;
		manager->modules[index] = &manager->plugin_stub[index];
		free(path);
		return false;
	}
	manager->active[index] = true;
	if (manager->started) {
		if (module->start) module->start(server);
		manager->module_started[index] = true;
		shady_event_emit_module(server, SHADY_EVENT_MODULE_STARTED, module);
	}
	wlr_log(WLR_INFO, "plugin: reloaded %s from %s", name, path);
	free(path);
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
