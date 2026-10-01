#ifndef SHADY_MODULE_MODULE_H
#define SHADY_MODULE_MODULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct shady_server;
struct shady_toplevel;
struct wlr_pointer_motion_event;
struct wlr_pointer_motion_absolute_event;
struct wlr_pointer_button_event;
struct wlr_pointer_axis_event;
struct wlr_surface;
typedef uint32_t xkb_keysym_t;

#define SHADY_MAX_MODULES 32

struct shady_module {
	const char *name;
	size_t state_size;
	size_t toplevel_state_size;
	bool (*enabled)(struct shady_server *server);
	bool (*init)(struct shady_server *server);
	void (*start)(struct shady_server *server);
	void (*stop)(struct shady_server *server);
	void (*destroy)(struct shady_server *server);

	void (*toplevel_map)(struct shady_toplevel *toplevel);
	void (*toplevel_unmap)(struct shady_toplevel *toplevel);
	void (*toplevel_commit)(struct shady_toplevel *toplevel);
	void (*toplevel_destroy)(struct shady_toplevel *toplevel);

	bool (*key)(struct shady_server *server, const xkb_keysym_t *syms,
		int nsyms, uint32_t state, uint32_t modifiers);
	bool (*pointer_motion)(struct shady_server *server,
		struct wlr_pointer_motion_event *event);
	bool (*pointer_motion_absolute)(struct shady_server *server,
		struct wlr_pointer_motion_absolute_event *event);
	bool (*pointer_button)(struct shady_server *server,
		struct wlr_pointer_button_event *event, uint32_t modifiers);
	bool (*pointer_axis)(struct shady_server *server,
		struct wlr_pointer_axis_event *event, uint32_t modifiers);
	bool (*pick_surface)(struct shady_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy,
		struct shady_toplevel **toplevel);
	void (*toplevel_moved)(struct shady_toplevel *toplevel,
		double x, double y);
};

struct shady_module_manager {
	const struct shady_module *modules[SHADY_MAX_MODULES];
	bool active[SHADY_MAX_MODULES];
	void *state[SHADY_MAX_MODULES];
	size_t count;
	bool started;
};

void shady_modules_init(struct shady_module_manager *manager);
bool shady_modules_register(struct shady_module_manager *manager,
	const struct shady_module *module);
void *shady_module_state(struct shady_server *server, const char *name);
bool shady_modules_initialize_all(struct shady_server *server);
void shady_modules_start_all(struct shady_server *server);
void shady_modules_stop_all(struct shady_server *server);
void shady_modules_destroy_all(struct shady_server *server);
void shady_modules_release_states(struct shady_server *server);

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

void shady_register_builtin_modules(struct shady_server *server);

#endif
