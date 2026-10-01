#include "module.h"

#include <stdlib.h>
#include <string.h>
#include <wlr/util/log.h>

#include "../shady.h"

static bool module_enabled(const struct shady_module *module,
		struct shady_server *server) {
	return !module->enabled || module->enabled(server);
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
				return false;
			}
		}
		if (module->init && !module->init(server)) {
			wlr_log(WLR_ERROR, "module: init failed: %s", module->name);
			shady_modules_destroy_all(server);
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
		if (manager->active[i] && module->start) {
			module->start(server);
		}
	}
	manager->started = true;
}

void shady_modules_stop_all(struct shady_server *server) {
	struct shady_module_manager *manager = &server->modules;
	if (!manager->started) return;
	for (size_t i = manager->count; i > 0; i--) {
		const struct shady_module *module = manager->modules[i - 1];
		if (manager->active[i - 1] && module->stop) {
			module->stop(server);
		}
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
	}
}

void shady_modules_release_states(struct shady_server *server) {
	struct shady_module_manager *manager = &server->modules;
	for (size_t i = 0; i < manager->count; i++) {
		free(manager->state[i]);
		manager->state[i] = NULL;
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
