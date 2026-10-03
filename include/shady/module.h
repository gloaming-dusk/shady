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

/* Stable public input values for native modules/plugins. These mirror the
 * modifier bits and key states delivered to shady_module.key without requiring
 * plugins to include wlroots' unstable headers. */
enum shady_key_state {
	SHADY_KEY_RELEASED = 0,
	SHADY_KEY_PRESSED = 1,
};

enum shady_modifier_mask {
	SHADY_MODIFIER_SHIFT = 1u << 0,
	SHADY_MODIFIER_CAPS = 1u << 1,
	SHADY_MODIFIER_CTRL = 1u << 2,
	SHADY_MODIFIER_ALT = 1u << 3,
	SHADY_MODIFIER_MOD2 = 1u << 4,
	SHADY_MODIFIER_MOD3 = 1u << 5,
	SHADY_MODIFIER_LOGO = 1u << 6,
	SHADY_MODIFIER_MOD5 = 1u << 7,
};

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
