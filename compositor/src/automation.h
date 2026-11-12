#ifndef SHADY_AUTOMATION_H
#define SHADY_AUTOMATION_H

/*
 * Input that scripts and agents drive: keys and text typed into the focused
 * window, the pointer, and operations that take time (a drag, waiting for the
 * screen to settle). Used by the IPC socket and by shady.automation in Lua.
 *
 * Keys reach clients through a keyboard of Shady's own, created the first
 * time automation types, with the configured keymap. A shortcut goes
 * through the compositor's bindings first, exactly like before; a key no
 * binding takes is then pressed on that keyboard for the focused surface.
 */

#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

struct shady_server;
struct wlr_surface;

struct shady_automation_key_result {
	bool handled; /* a binding, module or plugin took it */
	bool sent;    /* it was pressed for the focused surface */
};

/* Press and release a shortcut (sym with WLR_MODIFIER_* mods). */
void shady_automation_key(struct shady_server *server, xkb_keysym_t sym,
	uint32_t mods, struct shady_automation_key_result *result);
/* Only press it, or only release it. */
void shady_automation_key_state(struct shady_server *server, xkb_keysym_t sym,
	uint32_t mods, bool pressed, struct shady_automation_key_result *result);
/* Type UTF-8 text into the focused surface. Characters the keymap lacks are
 * typed with a keymap made for them. Returns the number of characters typed,
 * or -1 with *error set. */
int shady_automation_type(struct shady_server *server, const char *text,
	const char **error);

/* "left", "right", "middle", "side", "extra", "forward", "back". */
bool shady_automation_button_from_name(const char *name, uint32_t *button);
void shady_automation_pointer_move(struct shady_server *server, double x, double y);
void shady_automation_pointer_button(struct shady_server *server, uint32_t button,
	bool pressed);
/* `count` presses and releases: 2 is a double click. */
void shady_automation_click(struct shady_server *server, uint32_t button, int count);
/* Wheel clicks (notches); positive is down and right. */
void shady_automation_scroll(struct shady_server *server, double dx, double dy);

struct shady_automation_op;
struct shady_automation_result {
	bool settled;        /* wait: it stopped changing in time */
	uint32_t elapsed_ms;
	/* wait, not settled: what changed last ("window 3", "layout", ...) */
	const char *active;
};

/* What a wait watches for changes. */
enum shady_automation_scope {
	/* The windows: their surfaces and popups, the layout moving them, a
	 * layer surface with the keyboard (a launcher) and the lock screen. A
	 * bar's clock, a wallpaper and the cursor do not count. */
	SHADY_WAIT_WINDOWS,
	/* Every frame any output draws. */
	SHADY_WAIT_SCREEN,
};
typedef void (*shady_automation_done_fn)(void *data,
	const struct shady_automation_result *result);

/* Press at (x1, y1), move there in `steps` over `duration_ms`, release.
 * With a duration of 0 it all happens now and NULL is returned with *now
 * set; otherwise `done` runs once released. NULL without *now: failed. */
struct shady_automation_op *shady_automation_drag(struct shady_server *server,
	double x1, double y1, double x2, double y2, uint32_t button, int steps,
	int duration_ms, shady_automation_done_fn done, void *data, bool *now);
/* Wait at least `ms`, then until nothing in `scope` has changed for
 * `idle_ms` (0: no such wait), giving up after `timeout_ms`. A `window`
 * id (non-zero) narrows SHADY_WAIT_WINDOWS to that window and the layout. */
struct shady_automation_op *shady_automation_wait(struct shady_server *server,
	int ms, int idle_ms, int timeout_ms, enum shady_automation_scope scope,
	uint32_t window, shady_automation_done_fn done, void *data);
/* Stop an operation without running its callback. A drag lets go. */
void shady_automation_op_cancel(struct shady_automation_op *op);

/* output.c: an output committed a new frame. */
void shady_automation_frame_committed(void);
/* output.c: a client surface committed. */
void shady_automation_surface_committed(struct shady_server *server,
	struct wlr_surface *surface);
/* The layout moved windows (an animation tick). */
void shady_automation_layout_moved(void);
void shady_automation_finish(struct shady_server *server);

#endif
