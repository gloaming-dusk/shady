#ifndef SHADY_LAYOUT_H
#define SHADY_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct shady_server;
struct shady_toplevel;
struct wlr_output;

/* The scrolling layout: with `layout = "scrolling"` each workspace is a strip
 * of columns that scrolls sideways, niri style. Every function is a no-op
 * (returning false) while the layout is "floating". */

enum shady_layout_direction {
	SHADY_LAYOUT_LEFT,
	SHADY_LAYOUT_RIGHT,
	SHADY_LAYOUT_UP,
	SHADY_LAYOUT_DOWN,
	SHADY_LAYOUT_FIRST,
	SHADY_LAYOUT_LAST,
};

bool shady_layout_active(struct shady_server *server);
bool shady_layout_is_tiled(struct shady_toplevel *toplevel);
/* Whether the window is drawn and picked on that output. A tiled window
 * belongs to the strip's output only: columns scrolled past its edge are
 * off screen, not on the monitor beside it. */
bool shady_layout_shown_on(struct shady_toplevel *toplevel, struct wlr_output *output);

/* Hooks from the core. */
void shady_layout_initial_configure(struct shady_toplevel *toplevel);
void shady_layout_workspace_changed(struct shady_toplevel *toplevel, size_t old_workspace);
void shady_layout_arrange(struct shady_server *server);
/* A move or resize grab begins on a tiled window. True when the layout took
 * it over (the core should not start its own). */
bool shady_layout_begin_interactive(struct shady_toplevel *toplevel, bool move);
bool shady_layout_interactive_resize(struct shady_toplevel *toplevel,
	int width, int height, uint32_t edges);

/* Commands (Lua shady.layout.*). */
bool shady_layout_focus(struct shady_server *server, enum shady_layout_direction direction);
bool shady_layout_move(struct shady_server *server, enum shady_layout_direction direction);
/* Pull the focused window into the column on that side, or push it out of
 * its column into a new one on that side. */
bool shady_layout_consume_or_expel(struct shady_server *server, bool left);
/* "+10%", "-10%", "50%", "900", "+40". */
bool shady_layout_set_width(struct shady_server *server, const char *spec);
bool shady_layout_set_height(struct shady_server *server, const char *spec);
bool shady_layout_cycle_width(struct shady_server *server, bool forward);
bool shady_layout_reset_height(struct shady_server *server);
bool shady_layout_toggle_full_width(struct shady_server *server);
bool shady_layout_center(struct shady_server *server);
bool shady_layout_toggle_floating(struct shady_toplevel *toplevel);
bool shady_layout_set_presets(struct shady_server *server, const float *presets, size_t count);
/* Rules: whether a window floats, and its first column width (0 for the default). */
void shady_layout_rule(struct shady_toplevel *toplevel, int floating, float width);
/* Go to the workspace above or below; numbered workspaces are made as needed. */
bool shady_layout_workspace_step(struct shady_server *server, int delta, bool move_window);
/* A touchpad swipe with the configured number of fingers; false when the
 * layout does not take it (forward it to the client). */
bool shady_layout_swipe_begin(struct shady_server *server, uint32_t fingers);
bool shady_layout_swipe_update(struct shady_server *server, double dx, double dy);
bool shady_layout_swipe_end(struct shady_server *server, bool cancelled);
/* Pull the camera back to show the whole strip (GL renderer only); enable
 * is 1, 0 or -1 to toggle. False when there is no overview to show. */
bool shady_layout_overview(struct shady_server *server, int enable);
/* The scroll position and column count of the current workspace, for bars. */
void shady_layout_describe(struct shady_server *server, size_t *columns, size_t *active,
	double *view_fraction, double *visible_fraction);

#endif
