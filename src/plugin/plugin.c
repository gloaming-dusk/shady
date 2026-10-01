#include "plugin.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_xdg_shell.h>

#include <shady/plugin.h>

#include "../module/module.h"
#include "../render/render.h"
#include "../shady.h"

static void host_log(enum shady_plugin_log_level level, const char *message) {
	enum wlr_log_importance importance = WLR_INFO;
	if (level == SHADY_PLUGIN_LOG_DEBUG) importance = WLR_DEBUG;
	else if (level == SHADY_PLUGIN_LOG_ERROR) importance = WLR_ERROR;
	wlr_log(importance, "[plugin] %s", message ? message : "");
}

static bool host_has_capability(void *host, const char *capability) {
	return shady_module_has_capability(host, capability);
}

static bool host_config_set(void *host, const char *key, const char *value) {
	struct shady_server *server = host;
	return shady_config_set(&server->config, key, value);
}

static void *host_module_state(void *host, const char *module_name) {
	return shady_module_state(host, module_name);
}

static void *host_window_state(void *window, const char *module_name) {
	return shady_toplevel_module_state(window, module_name);
}

static const char *host_window_title(void *window) {
	struct shady_toplevel *toplevel = window;
	return toplevel && toplevel->xdg_toplevel && toplevel->xdg_toplevel->title
		? toplevel->xdg_toplevel->title : "";
}

static const char *host_window_app_id(void *window) {
	struct shady_toplevel *toplevel = window;
	return toplevel && toplevel->xdg_toplevel && toplevel->xdg_toplevel->app_id
		? toplevel->xdg_toplevel->app_id : "";
}

static void host_schedule_render(void *host) {
	struct shady_server *server = host;
	if (server->renderer) {
		shady_render_schedule_all_outputs(server);
	}
}

static void host_terminate(void *host) {
	struct shady_server *server = host;
	if (server->wl_display) {
		wl_display_terminate(server->wl_display);
	}
}

static const struct shady_plugin_api_v1 plugin_api = {
	.abi_version = SHADY_PLUGIN_ABI_V1,
	.struct_size = sizeof(struct shady_plugin_api_v1),
	.log = host_log,
	.has_capability = host_has_capability,
	.config_set = host_config_set,
	.module_state = host_module_state,
	.window_state = host_window_state,
	.window_title = host_window_title,
	.window_app_id = host_window_app_id,
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
		entry(SHADY_PLUGIN_ABI_V1, &plugin_api, server);
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
