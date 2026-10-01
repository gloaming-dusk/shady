#ifndef SHADY_PUBLIC_MODULE_H
#define SHADY_PUBLIC_MODULE_H

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
	const char *const *provides;
	const char *const *requires;
	const char *const *optional_requires;
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
	void (*tick)(struct shady_server *server, float dt,
		float logical_w, float logical_h);
};

#endif
