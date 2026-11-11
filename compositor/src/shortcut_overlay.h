#ifndef SHADY_SHORTCUT_OVERLAY_H
#define SHADY_SHORTCUT_OVERLAY_H

/*
 * The shortcut overlay Shady draws itself when no shell shows one: a panel
 * in the middle of the output under the pointer listing every shortcut
 * (ipc/shortcuts.h). A shell that subscribes to `shortcuts.toggle` draws its
 * own instead, and this one stays out of the way.
 */

#include <stdbool.h>

struct shady_server;
struct wlr_output;
struct wlr_texture;
struct wlr_box;

/* Super+Shift+/, shady.toggle_shortcuts() and the IPC command: hide the
 * overlay if it is up, else ask the shells, else show this one. */
void shady_shortcuts_toggle(struct shady_server *server);

bool shady_shortcut_overlay_visible(void);
void shady_shortcut_overlay_hide(struct shady_server *server);
/* For the GL renderer: the panel's texture and its box in layout
 * coordinates, when it is up on `output`. */
bool shady_shortcut_overlay_for_output(struct shady_server *server,
	struct wlr_output *output, struct wlr_texture **texture, struct wlr_box *box);
/* The output is going away: hide the panel if it is there. */
void shady_shortcut_overlay_output_removed(struct shady_server *server,
	struct wlr_output *output);
void shady_shortcut_overlay_finish(void);

#endif
