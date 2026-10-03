#ifndef SHADY_MODULE_MODULE_H
#define SHADY_MODULE_MODULE_H

#include <sys/types.h>
#include <shady/module.h>

struct shady_plugin_v2;

struct shady_module_manager {
	const struct shady_module *modules[SHADY_MAX_MODULES];
	bool active[SHADY_MAX_MODULES];
	bool module_started[SHADY_MAX_MODULES];
	void *state[SHADY_MAX_MODULES];
	void *plugin_handle[SHADY_MAX_MODULES];
	void *plugin_base[SHADY_MAX_MODULES];
	uint32_t plugin_abi[SHADY_MAX_MODULES];
	const struct shady_plugin_v2 *plugin_v2[SHADY_MAX_MODULES];
	char *plugin_path[SHADY_MAX_MODULES];
	char *plugin_name[SHADY_MAX_MODULES];
	struct shady_module plugin_stub[SHADY_MAX_MODULES];
	int8_t enable_override[SHADY_MAX_MODULES];
	size_t count;
	bool resolved;
	bool started;
};

void shady_modules_init(struct shady_module_manager *manager);
bool shady_modules_register(struct shady_module_manager *manager,
	const struct shady_module *module);
bool shady_modules_set_enabled(struct shady_module_manager *manager,
	const char *name, bool enabled);
bool shady_modules_has_registered(const struct shady_module_manager *manager,
	const char *name);
void *shady_module_state(struct shady_server *server, const char *name);
bool shady_modules_resolve(struct shady_server *server);
bool shady_module_has_capability(struct shady_server *server, const char *capability);
bool shady_modules_initialize_all(struct shady_server *server);
void shady_modules_start_all(struct shady_server *server);
void shady_modules_stop_all(struct shady_server *server);
void shady_modules_destroy_all(struct shady_server *server);
void shady_modules_release_states(struct shady_server *server);
void shady_modules_close_plugins(struct shady_server *server);
ssize_t shady_module_index_by_name(struct shady_server *server, const char *name);

bool shady_modules_toplevel_state_init(struct shady_toplevel *toplevel);
void shady_modules_toplevel_state_finish(struct shady_toplevel *toplevel);
void *shady_toplevel_module_state(struct shady_toplevel *toplevel, const char *name);
const void *shady_toplevel_module_state_const(const struct shady_toplevel *toplevel,
	const char *name);

void shady_modules_toplevel_map(struct shady_toplevel *toplevel);
void shady_modules_toplevel_unmap(struct shady_toplevel *toplevel);
void shady_modules_toplevel_commit(struct shady_toplevel *toplevel);
void shady_modules_toplevel_destroy(struct shady_toplevel *toplevel);
bool shady_modules_key(struct shady_server *server, const xkb_keysym_t *syms,
	int nsyms, uint32_t state, uint32_t modifiers);
bool shady_modules_pointer_motion(struct shady_server *server,
	struct wlr_pointer_motion_event *event);
bool shady_modules_pointer_motion_absolute(struct shady_server *server,
	struct wlr_pointer_motion_absolute_event *event);
bool shady_modules_pointer_button(struct shady_server *server,
	struct wlr_pointer_button_event *event, uint32_t modifiers);
bool shady_modules_pointer_axis(struct shady_server *server,
	struct wlr_pointer_axis_event *event, uint32_t modifiers);
bool shady_modules_pick_surface(struct shady_server *server, double lx, double ly,
	struct wlr_surface **surface, double *sx, double *sy,
	struct shady_toplevel **toplevel);
void shady_modules_toplevel_moved(struct shady_toplevel *toplevel,
	double x, double y);
void shady_modules_tick(struct shady_server *server, float dt,
	float logical_w, float logical_h);

bool shady_register_builtin_modules(struct shady_server *server);

#endif
