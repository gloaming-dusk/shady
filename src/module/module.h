#ifndef SHADY_MODULE_MODULE_H
#define SHADY_MODULE_MODULE_H

#include <stdbool.h>
#include <stddef.h>

struct shady_server;
struct shady_toplevel;

#define SHADY_MAX_MODULES 32

struct shady_module {
	const char *name;
	bool (*enabled)(struct shady_server *server);
	bool (*init)(struct shady_server *server);
	void (*start)(struct shady_server *server);
	void (*stop)(struct shady_server *server);
	void (*destroy)(struct shady_server *server);

	void (*toplevel_map)(struct shady_toplevel *toplevel);
	void (*toplevel_unmap)(struct shady_toplevel *toplevel);
	void (*toplevel_commit)(struct shady_toplevel *toplevel);
	void (*toplevel_destroy)(struct shady_toplevel *toplevel);
};

struct shady_module_manager {
	const struct shady_module *modules[SHADY_MAX_MODULES];
	bool active[SHADY_MAX_MODULES];
	size_t count;
	bool started;
};

void shady_modules_init(struct shady_module_manager *manager);
bool shady_modules_register(struct shady_module_manager *manager,
	const struct shady_module *module);
bool shady_modules_initialize_all(struct shady_server *server);
void shady_modules_start_all(struct shady_server *server);
void shady_modules_stop_all(struct shady_server *server);
void shady_modules_destroy_all(struct shady_server *server);

void shady_modules_toplevel_map(struct shady_toplevel *toplevel);
void shady_modules_toplevel_unmap(struct shady_toplevel *toplevel);
void shady_modules_toplevel_commit(struct shady_toplevel *toplevel);
void shady_modules_toplevel_destroy(struct shady_toplevel *toplevel);

void shady_register_builtin_modules(struct shady_server *server);

#endif
