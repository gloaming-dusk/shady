#ifndef SHADY_LAYOUT_STATE_H
#define SHADY_LAYOUT_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../module/module.h"
#include "../workspace/state.h"

#define SHADY_LAYOUT_MAX_COLUMNS 64
#define SHADY_LAYOUT_MAX_ROWS 8
#define SHADY_LAYOUT_MAX_PRESETS 8

struct wl_event_source;

/* One column of the scrolling strip: windows stacked top to bottom. */
struct shady_layout_column {
	struct shady_toplevel *rows[SHADY_LAYOUT_MAX_ROWS];
	float weights[SHADY_LAYOUT_MAX_ROWS];
	size_t count;
	size_t active;
	/* Share of the work area, used while width is 0. */
	float proportion;
	/* Fixed logical width (a resize or "set_width 900"), or 0. */
	int width;
	/* The preset last cycled to, or -1. */
	int preset;
	bool full_width;
};

struct shady_layout_workspace {
	struct shady_layout_column columns[SHADY_LAYOUT_MAX_COLUMNS];
	size_t count;
	size_t active;
	/* Strip offset of the screen's left edge: drawn and wanted. */
	double view;
	double view_target;
	/* Vertical offset while the workspace slides in or out. */
	double slide;
	double slide_target;
	bool view_valid;
	/* Still drawn while sliding away after a workspace switch. */
	bool leaving;
};

struct shady_layout_state {
	struct shady_server *server;
	struct shady_layout_workspace workspaces[SHADY_MAX_WORKSPACES];
	float presets[SHADY_LAYOUT_MAX_PRESETS];
	size_t preset_count;
	struct wl_event_source *timer;
	uint64_t last_tick_ns;
	size_t shown_workspace;
	bool arranging;
	/* A touchpad swipe scrolling the strip or switching workspaces. */
	bool swipe_active;
	bool swipe_ours;
	int swipe_axis; /* 0 undecided, 1 horizontal, 2 vertical */
	double swipe_dx, swipe_dy;
	double swipe_view_start;
	/* The overview (camera pulled back), and whether the camera is still
	 * ours while it flies back. */
	bool overview;
	bool camera_owned;
	/* The last `layout.strip` value published. */
	char published[4096];
	/* Top-layer surfaces hidden under a fullscreen window. */
	bool top_layers_hidden;
};

struct shady_layout_toplevel {
	bool tiled;
	/* A rule or the user made it float, or tile despite looking like a dialog. */
	bool force_float;
	bool force_tile;
	float rule_width;
	size_t workspace;
	bool tiled_state_sent;
	bool was_maximized;
	/* Drawn position: strip x (before the view is applied) and screen y. */
	bool placed;
	double strip_x, y;
	/* Where the layout wants it. */
	int target_strip_x, target_y;
	/* The size last asked of the client. */
	int asked_w, asked_h;
};

static inline struct shady_layout_state *shady_layout_state_for(
		struct shady_server *server) {
	return shady_module_state(server, "layout");
}

static inline struct shady_layout_toplevel *shady_layout_toplevel_for(
		struct shady_toplevel *toplevel) {
	return shady_toplevel_module_state(toplevel, "layout");
}

#endif
