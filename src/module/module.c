#include "module.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <wlr/util/log.h>

#include "../shady.h"
#include "../event/event.h"

static ssize_t module_index(struct shady_module_manager *manager,
		const struct shady_module *module) {
	for (size_t i = 0; i < manager->count; i++) {
		if (manager->modules[i] == module) return (ssize_t)i;
	}
	return -1;
}

static bool module_enabled(const struct shady_module *module,
		struct shady_server *server) {
	ssize_t index = module_index(&server->modules, module);
	if (index >= 0 && server->modules.enable_override[index] != 0) {
		return server->modules.enable_override[index] > 0;
	}
	return !module->enabled || module->enabled(server);
}

static bool string_list_contains(const char *const *items, const char *needle) {
	if (!items || !needle) return false;
	for (size_t i = 0; items[i]; i++) {
		if (strcmp(items[i], needle) == 0) return true;
	}
	return false;
}

static ssize_t capability_provider(struct shady_server *server,
		const char *capability, bool enabled_only) {
	struct shady_module_manager *manager = &server->modules;
	ssize_t provider = -1;
	for (size_t i = 0; i < manager->count; i++) {
		const struct shady_module *module = manager->modules[i];
		if (enabled_only && !module_enabled(module, server)) continue;
		if (!string_list_contains(module->provides, capability)) continue;
		if (provider >= 0) {
			wlr_log(WLR_ERROR, "module: capability %s has multiple providers: %s, %s",
				capability, manager->modules[provider]->name, module->name);
			return -2;
		}
		provider = (ssize_t)i;
	}
	return provider;
}

void shady_modules_init(struct shady_module_manager *manager) {
	memset(manager, 0, sizeof(*manager));
}

bool shady_modules_register(struct shady_module_manager *manager,
		const struct shady_module *module) {
	if (!module || !module->name || manager->count >= SHADY_MAX_MODULES) {
		return false;
	}
	for (size_t i = 0; i < manager->count; i++) {
		if (strcmp(manager->modules[i]->name, module->name) == 0) {
			return false;
		}
	}
	manager->modules[manager->count++] = module;
	manager->resolved = false;
	return true;
}

ssize_t shady_module_index_by_name(struct shady_server *server, const char *name) {
	for (size_t i = 0; i < server->modules.count; i++) {
		if (server->modules.modules[i] && server->modules.modules[i]->name &&
				strcmp(server->modules.modules[i]->name, name) == 0) return (ssize_t)i;
	}
	return -1;
}

bool shady_modules_has_registered(const struct shady_module_manager *manager,
		const char *name) {
	for (size_t i = 0; i < manager->count; i++) {
		if (strcmp(manager->modules[i]->name, name) == 0) return true;
	}
	return false;
}

bool shady_modules_set_enabled(struct shady_module_manager *manager,
		const char *name, bool enabled) {
	for (size_t i = 0; i < manager->count; i++) {
		if (strcmp(manager->modules[i]->name, name) == 0) {
			manager->enable_override[i] = enabled ? 1 : -1;
			manager->resolved = false;
			return true;
		}
	}
	return false;
}

bool shady_module_has_capability(struct shady_server *server, const char *capability) {
	ssize_t provider = capability_provider(server, capability, true);
	if (provider < 0) return false;
	return server->modules.active[provider];
}

bool shady_modules_resolve(struct shady_server *server) {
	struct shady_module_manager *manager = &server->modules;
	if (manager->resolved) return true;

	bool edges[SHADY_MAX_MODULES][SHADY_MAX_MODULES] = {{0}};
	unsigned indegree[SHADY_MAX_MODULES] = {0};
	bool enabled[SHADY_MAX_MODULES] = {0};
	for (size_t i = 0; i < manager->count; i++) {
		enabled[i] = module_enabled(manager->modules[i], server);
	}

	/* Capabilities have a single active provider to keep dependency
	 * resolution deterministic. */
	for (size_t i = 0; i < manager->count; i++) {
		if (!enabled[i] || !manager->modules[i]->provides) continue;
		for (size_t c = 0; manager->modules[i]->provides[c]; c++) {
			ssize_t provider = capability_provider(server,
				manager->modules[i]->provides[c], true);
			if (provider == -2) return false;
		}
	}

	for (size_t i = 0; i < manager->count; i++) {
		if (!enabled[i]) continue;
		const struct shady_module *module = manager->modules[i];
		const char *const *lists[2] = { module->requires, module->optional_requires };
		for (size_t kind = 0; kind < 2; kind++) {
			const char *const *caps = lists[kind];
			if (!caps) continue;
			for (size_t c = 0; caps[c]; c++) {
				ssize_t provider = capability_provider(server, caps[c], true);
				if (provider == -2) return false;
				if (provider < 0) {
					if (kind == 0) {
						wlr_log(WLR_ERROR, "module: %s requires missing capability %s",
							module->name, caps[c]);
						return false;
					}
					continue;
				}
				if ((size_t)provider == i || edges[provider][i]) continue;
				edges[provider][i] = true;
				indegree[i]++;
			}
		}
	}

	const struct shady_module *ordered[SHADY_MAX_MODULES] = {0};
	size_t out = 0;
	bool emitted[SHADY_MAX_MODULES] = {0};
	while (out < manager->count) {
		ssize_t pick = -1;
		for (size_t i = 0; i < manager->count; i++) {
			if (!emitted[i] && indegree[i] == 0) { pick = (ssize_t)i; break; }
		}
		if (pick < 0) {
			wlr_log(WLR_ERROR, "module: dependency cycle detected");
			return false;
		}
		emitted[pick] = true;
		ordered[out++] = manager->modules[pick];
		for (size_t j = 0; j < manager->count; j++) {
			if (edges[pick][j] && indegree[j] > 0) indegree[j]--;
		}
	}
	const struct shady_module *old_modules[SHADY_MAX_MODULES] = {0};
	void *old_handles[SHADY_MAX_MODULES] = {0};
	void *old_bases[SHADY_MAX_MODULES] = {0};
	uint32_t old_abis[SHADY_MAX_MODULES] = {0};
	const struct shady_plugin_v2 *old_v2[SHADY_MAX_MODULES] = {0};
	char *old_paths[SHADY_MAX_MODULES] = {0};
	char *old_names[SHADY_MAX_MODULES] = {0};
	int8_t old_overrides[SHADY_MAX_MODULES] = {0};
	memcpy(old_modules, manager->modules, sizeof(old_modules));
	memcpy(old_handles, manager->plugin_handle, sizeof(old_handles));
	memcpy(old_bases, manager->plugin_base, sizeof(old_bases));
	memcpy(old_abis, manager->plugin_abi, sizeof(old_abis));
	memcpy(old_v2, manager->plugin_v2, sizeof(old_v2));
	memcpy(old_paths, manager->plugin_path, sizeof(old_paths));
	memcpy(old_names, manager->plugin_name, sizeof(old_names));
	memcpy(old_overrides, manager->enable_override, sizeof(old_overrides));
	memcpy(manager->modules, ordered, sizeof(manager->modules));
	memset(manager->plugin_handle, 0, sizeof(manager->plugin_handle));
	memset(manager->plugin_base, 0, sizeof(manager->plugin_base));
	memset(manager->plugin_abi, 0, sizeof(manager->plugin_abi));
	memset(manager->plugin_v2, 0, sizeof(manager->plugin_v2));
	memset(manager->plugin_path, 0, sizeof(manager->plugin_path));
	memset(manager->plugin_name, 0, sizeof(manager->plugin_name));
	memset(manager->enable_override, 0, sizeof(manager->enable_override));
	for (size_t i = 0; i < manager->count; i++) {
		for (size_t j = 0; j < manager->count; j++) {
			if (manager->modules[i] == old_modules[j]) {
				manager->plugin_handle[i] = old_handles[j];
				manager->plugin_base[i] = old_bases[j];
				manager->plugin_abi[i] = old_abis[j];
				manager->plugin_v2[i] = old_v2[j];
				manager->plugin_path[i] = old_paths[j];
				manager->plugin_name[i] = old_names[j];
				manager->enable_override[i] = old_overrides[j];
				break;
			}
		}
		wlr_log(WLR_DEBUG, "module: resolved[%zu] %s", i,
			manager->modules[i]->name);
	}
	memset(manager->active, 0, sizeof(manager->active));
	memset(manager->module_started, 0, sizeof(manager->module_started));
	memset(manager->state, 0, sizeof(manager->state));
	manager->resolved = true;
	return true;
}

void *shady_module_state(struct shady_server *server, const char *name) {
	struct shady_module_manager *manager = &server->modules;
	for (size_t i = 0; i < manager->count; i++) {
		if (strcmp(manager->modules[i]->name, name) == 0) {
			return manager->state[i];
		}
	}
	return NULL;
}

bool shady_modules_initialize_all(struct shady_server *server) {
	struct shady_module_manager *manager = &server->modules;
	if (!shady_modules_resolve(server)) return false;
	for (size_t i = 0; i < manager->count; i++) {
		const struct shady_module *module = manager->modules[i];
		if (!module_enabled(module, server)) {
			wlr_log(WLR_INFO, "module: disabled %s", module->name);
			continue;
		}
		wlr_log(WLR_INFO, "module: init %s", module->name);
		if (module->state_size > 0) {
			manager->state[i] = calloc(1, module->state_size);
			if (!manager->state[i]) {
				wlr_log(WLR_ERROR, "module: state allocation failed: %s", module->name);
				shady_modules_destroy_all(server);
				shady_modules_release_states(server);
				return false;
			}
		}
		if (module->init && !module->init(server)) {
			wlr_log(WLR_ERROR, "module: init failed: %s", module->name);
			shady_modules_destroy_all(server);
			shady_modules_release_states(server);
			return false;
		}
		manager->active[i] = true;
	}
	return true;
}

void shady_modules_start_all(struct shady_server *server) {
	struct shady_module_manager *manager = &server->modules;
	for (size_t i = 0; i < manager->count; i++) {
		const struct shady_module *module = manager->modules[i];
		if (!manager->active[i]) continue;
		if (module->start) module->start(server);
		manager->module_started[i] = true;
		shady_event_emit_module(server, SHADY_EVENT_MODULE_STARTED, module);
	}
	manager->started = true;
}

void shady_modules_stop_all(struct shady_server *server) {
	struct shady_module_manager *manager = &server->modules;
	if (!manager->started) return;
	for (size_t i = manager->count; i > 0; i--) {
		const struct shady_module *module = manager->modules[i - 1];
		if (!manager->module_started[i - 1]) continue;
		shady_event_emit_module(server, SHADY_EVENT_MODULE_STOPPED, module);
		if (module->stop) module->stop(server);
		manager->module_started[i - 1] = false;
	}
	manager->started = false;
}

void shady_modules_destroy_all(struct shady_server *server) {
	struct shady_module_manager *manager = &server->modules;
	for (size_t i = manager->count; i > 0; i--) {
		const struct shady_module *module = manager->modules[i - 1];
		if (manager->active[i - 1] && module->destroy) {
			module->destroy(server);
		}
		manager->active[i - 1] = false;
		manager->module_started[i - 1] = false;
	}
}

void shady_modules_release_states(struct shady_server *server) {
	struct shady_module_manager *manager = &server->modules;
	for (size_t i = 0; i < manager->count; i++) {
		free(manager->state[i]);
		manager->state[i] = NULL;
	}
}

void shady_modules_close_plugins(struct shady_server *server) {
	struct shady_module_manager *manager = &server->modules;
	for (size_t i = manager->count; i > 0; i--) {
		if (manager->plugin_handle[i - 1]) {
			shady_event_unsubscribe_owner(server, manager->plugin_base[i - 1]);
			dlclose(manager->plugin_handle[i - 1]);
			manager->plugin_handle[i - 1] = NULL;
			manager->plugin_base[i - 1] = NULL;
			manager->plugin_abi[i - 1] = 0;
			manager->plugin_v2[i - 1] = NULL;
		}
		free(manager->plugin_path[i - 1]);
		manager->plugin_path[i - 1] = NULL;
		free(manager->plugin_name[i - 1]);
		manager->plugin_name[i - 1] = NULL;
	}
}

#define DISPATCH_TOPLEVEL(hook) \
	do { \
		struct shady_server *server = toplevel->server; \
		for (size_t i = 0; i < server->modules.count; i++) { \
			const struct shady_module *module = server->modules.modules[i]; \
			if (server->modules.active[i] && module->hook) { \
				module->hook(toplevel); \
			} \
		} \
	} while (0)

bool shady_modules_toplevel_state_init(struct shady_toplevel *toplevel) {
	struct shady_server *server = toplevel->server;
	for (size_t i = 0; i < server->modules.count; i++) {
		const struct shady_module *module = server->modules.modules[i];
		if (!server->modules.active[i] || module->toplevel_state_size == 0) continue;
		toplevel->module_state[i] = calloc(1, module->toplevel_state_size);
		if (!toplevel->module_state[i]) {
			shady_modules_toplevel_state_finish(toplevel);
			return false;
		}
	}
	return true;
}

void shady_modules_toplevel_state_finish(struct shady_toplevel *toplevel) {
	for (size_t i = 0; i < SHADY_MAX_MODULES; i++) {
		free(toplevel->module_state[i]);
		toplevel->module_state[i] = NULL;
	}
}

void *shady_toplevel_module_state(struct shady_toplevel *toplevel, const char *name) {
	struct shady_server *server = toplevel->server;
	for (size_t i = 0; i < server->modules.count; i++) {
		if (strcmp(server->modules.modules[i]->name, name) == 0) return toplevel->module_state[i];
	}
	return NULL;
}

const void *shady_toplevel_module_state_const(const struct shady_toplevel *toplevel,
		const char *name) {
	return shady_toplevel_module_state((struct shady_toplevel *)toplevel, name);
}

void shady_modules_toplevel_map(struct shady_toplevel *toplevel) {
	DISPATCH_TOPLEVEL(toplevel_map);
}
void shady_modules_toplevel_unmap(struct shady_toplevel *toplevel) {
	DISPATCH_TOPLEVEL(toplevel_unmap);
}
void shady_modules_toplevel_commit(struct shady_toplevel *toplevel) {
	DISPATCH_TOPLEVEL(toplevel_commit);
}
void shady_modules_toplevel_destroy(struct shady_toplevel *toplevel) {
	DISPATCH_TOPLEVEL(toplevel_destroy);
}

bool shady_modules_key(struct shady_server *server, const xkb_keysym_t *syms,
		int nsyms, uint32_t state, uint32_t modifiers) {
	for (size_t i = 0; i < server->modules.count; i++) {
		const struct shady_module *module = server->modules.modules[i];
		if (server->modules.active[i] && module->key &&
				module->key(server, syms, nsyms, state, modifiers)) return true;
	}
	return false;
}

#define DISPATCH_INPUT(hook, event, modifiers) \
	do { \
		for (size_t i = 0; i < server->modules.count; i++) { \
			const struct shady_module *module = server->modules.modules[i]; \
			if (server->modules.active[i] && module->hook && \
					module->hook(server, event, modifiers)) return true; \
		} \
		return false; \
	} while (0)

bool shady_modules_pointer_motion(struct shady_server *server,
		struct wlr_pointer_motion_event *event) {
	for (size_t i = 0; i < server->modules.count; i++) {
		const struct shady_module *module = server->modules.modules[i];
		if (server->modules.active[i] && module->pointer_motion &&
				module->pointer_motion(server, event)) return true;
	}
	return false;
}

bool shady_modules_pointer_motion_absolute(struct shady_server *server,
		struct wlr_pointer_motion_absolute_event *event) {
	for (size_t i = 0; i < server->modules.count; i++) {
		const struct shady_module *module = server->modules.modules[i];
		if (server->modules.active[i] && module->pointer_motion_absolute &&
				module->pointer_motion_absolute(server, event)) return true;
	}
	return false;
}

bool shady_modules_pointer_button(struct shady_server *server,
		struct wlr_pointer_button_event *event, uint32_t modifiers) {
	DISPATCH_INPUT(pointer_button, event, modifiers);
}

bool shady_modules_pointer_axis(struct shady_server *server,
		struct wlr_pointer_axis_event *event, uint32_t modifiers) {
	DISPATCH_INPUT(pointer_axis, event, modifiers);
}

bool shady_modules_pick_surface(struct shady_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy,
		struct shady_toplevel **toplevel) {
	for (size_t i = 0; i < server->modules.count; i++) {
		const struct shady_module *module = server->modules.modules[i];
		if (server->modules.active[i] && module->pick_surface &&
				module->pick_surface(server, lx, ly, surface, sx, sy, toplevel)) {
			return true;
		}
	}
	return false;
}

void shady_modules_toplevel_moved(struct shady_toplevel *toplevel,
		double x, double y) {
	struct shady_server *server = toplevel->server;
	for (size_t i = 0; i < server->modules.count; i++) {
		const struct shady_module *module = server->modules.modules[i];
		if (server->modules.active[i] && module->toplevel_moved) {
			module->toplevel_moved(toplevel, x, y);
		}
	}
}

void shady_modules_tick(struct shady_server *server, float dt,
		float logical_w, float logical_h) {
	for (size_t i = 0; i < server->modules.count; i++) {
		const struct shady_module *module = server->modules.modules[i];
		if (server->modules.active[i] && module->tick) {
			module->tick(server, dt, logical_w, logical_h);
		}
	}
}
