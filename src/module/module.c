#include "module.h"

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

bool shady_modules_initialize_all(struct shady_server *server) {
	struct shady_module_manager *manager = &server->modules;
	for (size_t i = 0; i < manager->count; i++) {
		const struct shady_module *module = manager->modules[i];
		if (!module_enabled(module, server)) {
			wlr_log(WLR_INFO, "module: disabled %s", module->name);
			continue;
		}
		wlr_log(WLR_INFO, "module: init %s", module->name);
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
