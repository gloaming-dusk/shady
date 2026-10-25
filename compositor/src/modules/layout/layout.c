/* The scrolling layout. Each workspace is a strip of columns, as wide as it
 * needs to be, of which the screen shows a window's worth: opening a window
 * adds a column next to the focused one, and moving the focus scrolls the
 * strip so the focused column is on screen. A column stacks one or more
 * windows. This is niri's model; the strip lives on the first output.
 *
 * Positions are kept in strip coordinates (x before the view is applied) and
 * animated towards where the layout wants them, as is the view, so the strip
 * glides instead of jumping. The client is asked for its new size at once. */
#include "layout.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

#include "../../shady.h"
#include "../../ipc/values.h"
#include "../../render/render.h"
#include "../desktop/state.h"
#include "../workspace/workspace.h"
#include "state.h"
#if SHADY_HAS_SPATIAL
#include "../../render/math3d.h"
#include "../spatial/state.h"
#endif

#define MIN_COLUMN_WIDTH 120
#define MIN_ROW_HEIGHT 80
#define ANIMATION_INTERVAL_MS 7

static uint64_t now_ns(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

bool shady_layout_active(struct shady_server *server) {
	return server && server->config.layout_scrolling && shady_layout_state_for(server);
}

bool shady_layout_is_tiled(struct shady_toplevel *toplevel) {
	if (!toplevel || !shady_layout_active(toplevel->server)) return false;
	struct shady_layout_toplevel *lt = shady_layout_toplevel_for(toplevel);
	return lt && lt->tiled;
}

static size_t current_workspace(struct shady_server *server) {
	struct shady_workspace_state *ws = shady_workspace_state_for(server);
	return ws ? ws->current : 0;
}

static size_t toplevel_workspace(struct shady_toplevel *toplevel) {
	struct shady_workspace_toplevel_state *w =
		shady_workspace_toplevel_state_for(toplevel);
	return w ? w->workspace : 0;
}

/* The strip lives on the output connected first (the list keeps the newest
 * at its head), so plugging in a monitor does not move it. */
static struct wlr_output *layout_output(struct shady_server *server) {
	struct shady_output *output;
	wl_list_for_each_reverse(output, &server->outputs, link) {
		if (output->wlr_output && output->wlr_output->enabled &&
				wlr_output_layout_get(server->output_layout, output->wlr_output))
			return output->wlr_output;
	}
	return NULL;
}

/* The work area (what the bars leave) and the whole output. */
static bool layout_area(struct shady_server *server, struct wlr_box *area,
		struct wlr_box *full) {
	struct wlr_output *output = layout_output(server);
	if (!output) return false;
	shady_output_work_area(server, output, area);
	wlr_output_layout_get_box(server->output_layout, output, full);
	return area->width > 0 && area->height > 0;
}

static int layout_gap(struct shady_server *server) {
	float gap = server->config.layout_gap;
	return gap > 0.f ? (int)(gap + 0.5f) : 0;
}

static int titlebar_height(struct shady_toplevel *toplevel) {
	if (!shady_toplevel_has_titlebar(toplevel) || toplevel->fullscreen) return 0;
	return (int)(toplevel->server->config.window_titlebar_height + 0.5f);
}

static bool find_in(struct shady_layout_workspace *ws, struct shady_toplevel *toplevel,
		size_t *column, size_t *row) {
	for (size_t c = 0; c < ws->count; c++) {
		struct shady_layout_column *col = &ws->columns[c];
		for (size_t r = 0; r < col->count; r++) {
			if (col->rows[r] != toplevel) continue;
			if (column) *column = c;
			if (row) *row = r;
			return true;
		}
	}
	return false;
}

static struct shady_layout_workspace *strip_of(struct shady_toplevel *toplevel,
		size_t *column, size_t *row) {
	struct shady_layout_state *state = shady_layout_state_for(toplevel->server);
	struct shady_layout_toplevel *lt = shady_layout_toplevel_for(toplevel);
	if (!state || !lt || !lt->tiled || lt->workspace >= SHADY_MAX_WORKSPACES) return NULL;
	struct shady_layout_workspace *ws = &state->workspaces[lt->workspace];
	return find_in(ws, toplevel, column, row) ? ws : NULL;
}

static bool column_fullscreen(const struct shady_layout_column *col) {
	for (size_t r = 0; r < col->count; r++)
		if (col->rows[r]->fullscreen) return true;
	return false;
}

static int column_width(struct shady_server *server, const struct shady_layout_column *col,
		const struct wlr_box *area, const struct wlr_box *full) {
	int gap = layout_gap(server);
	if (column_fullscreen(col)) return full->width;
	int max = area->width - 2 * gap;
	if (max < MIN_COLUMN_WIDTH) max = MIN_COLUMN_WIDTH;
	int width;
	if (col->full_width) width = max;
	else if (col->width > 0) width = col->width;
	/* Proportions leave room for the gaps: two halves fill the screen. */
	else width = (int)lround((area->width - gap) * col->proportion) - gap;
	if (width < MIN_COLUMN_WIDTH) width = MIN_COLUMN_WIDTH;
	if (width > max) width = max;
	return width;
}

struct strip_metrics {
	int x[SHADY_LAYOUT_MAX_COLUMNS];
	int width[SHADY_LAYOUT_MAX_COLUMNS];
	int total;
};

static void strip_measure(struct shady_server *server, struct shady_layout_workspace *ws,
		const struct wlr_box *area, const struct wlr_box *full, struct strip_metrics *m) {
	int gap = layout_gap(server), x = 0;
	for (size_t c = 0; c < ws->count; c++) {
		m->width[c] = column_width(server, &ws->columns[c], area, full);
		m->x[c] = x;
		x += m->width[c] + gap;
	}
	m->total = ws->count ? x - gap : 0;
}

/* Scroll so column c is on screen. Coming from column prev, a column that
 * fits on screen together with it is scrolled to the nearest edge; one that
 * does not is centred (niri's center-focused-column "on-overflow"). */
static void ensure_visible(struct shady_server *server, struct shady_layout_workspace *ws,
		size_t c, size_t prev, const struct strip_metrics *m,
		const struct wlr_box *area, const struct wlr_box *full) {
	if (c >= ws->count) return;
	int gap = layout_gap(server);
	double sx = m->x[c], w = m->width[c];
	if (column_fullscreen(&ws->columns[c])) {
		ws->view_target = sx + area->x - full->x;
		return;
	}
	double centred = sx - (area->width - w) / 2.0;
	if (ws->count == 1 && server->config.layout_center_single) {
		ws->view_target = centred;
		return;
	}
	if (w + 2 * gap > area->width) {
		ws->view_target = centred;
		return;
	}
	double lo = sx + w + gap - area->width; /* right edge on screen */
	double hi = sx - gap;                   /* left edge on screen */
	double view = ws->view_target;
	if (view >= lo - 0.5 && view <= hi + 0.5) return;
	if (prev < ws->count && prev != c) {
		double left = prev < c ? m->x[prev] : sx;
		double right = prev < c ? sx + w : m->x[prev] + m->width[prev];
		if (right - left + 2 * gap > area->width) {
			ws->view_target = centred;
			return;
		}
	}
	ws->view_target = view < lo ? lo : hi;
}

/* No empty screen beside the strip when it could be filled. */
static void clamp_view(struct shady_server *server, struct shady_layout_workspace *ws,
		const struct strip_metrics *m, const struct wlr_box *area) {
	if (ws->count == 0) return;
	if (ws->count == 1 && server->config.layout_center_single) return;
	if (ws->active < ws->count && column_fullscreen(&ws->columns[ws->active])) return;
	int gap = layout_gap(server);
	double min = -gap;
	double max = m->total + gap - area->width;
	if (max < min) max = min;
	if (ws->view_target < min) ws->view_target = min;
	if (ws->view_target > max) ws->view_target = max;
}

static void apply_positions(struct shady_server *server, size_t index) {
	struct shady_layout_state *state = shady_layout_state_for(server);
	struct shady_layout_workspace *ws = &state->workspaces[index];
	struct wlr_box area, full;
	if (!layout_area(server, &area, &full)) return;
	for (size_t c = 0; c < ws->count; c++) {
		struct shady_layout_column *col = &ws->columns[c];
		for (size_t r = 0; r < col->count; r++) {
			struct shady_toplevel *t = col->rows[r];
			struct shady_layout_toplevel *lt = shady_layout_toplevel_for(t);
			if (!t->scene_tree || !lt) continue;
			int x = (int)lround(area.x + lt->strip_x - ws->view);
			int y = (int)lround(lt->y + ws->slide);
			wlr_scene_node_set_position(&t->scene_tree->node, x, y);
		}
	}
}

static void set_tiled_state(struct shady_toplevel *toplevel, bool tiled) {
	struct shady_layout_toplevel *lt = shady_layout_toplevel_for(toplevel);
	if (!lt || lt->tiled_state_sent == tiled) return;
	struct wlr_xdg_toplevel *xdg = toplevel->xdg_toplevel;
	if (!xdg->base->initialized) return;
	if (xdg->base->client->shell->version >= 2)
		wlr_xdg_toplevel_set_tiled(xdg, tiled ? WLR_EDGE_LEFT | WLR_EDGE_RIGHT |
			WLR_EDGE_TOP | WLR_EDGE_BOTTOM : WLR_EDGE_NONE);
	lt->tiled_state_sent = tiled;
}

static void ask_size(struct shady_toplevel *toplevel, int width, int height) {
	struct shady_layout_toplevel *lt = shady_layout_toplevel_for(toplevel);
	if (!lt || width <= 0 || height <= 0) return;
	if (!toplevel->xdg_toplevel->base->initialized) return;
	if (lt->asked_w == width && lt->asked_h == height) return;
	lt->asked_w = width;
	lt->asked_h = height;
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, width, height);
}

static void schedule_animation(struct shady_layout_state *state);

/* Where a fullscreen window is: the strip's output when tiled, else the
 * output under its middle. */
static struct wlr_output *fullscreen_output(struct shady_server *server,
		struct shady_toplevel *t) {
	if (!shady_layout_is_tiled(t) && t->scene_tree) {
		struct wlr_box geo = t->xdg_toplevel->base->geometry;
		struct wlr_output *output = wlr_output_layout_output_at(server->output_layout,
			t->scene_tree->node.x + geo.width / 2.0, t->scene_tree->node.y + geo.height / 2.0);
		if (output) return output;
	}
	return layout_output(server);
}

/* Hide the bars (top layer) on the output while a fullscreen window there
 * has the focus, so a video or game covers that whole screen; the bars of
 * the other outputs stay. */
static void update_top_layers(struct shady_server *server) {
	struct shady_layout_state *state = shady_layout_state_for(server);
	struct shady_desktop_state *desktop = shady_desktop_state(server);
	if (!state || !desktop) return;
	struct wlr_output *hide_on = NULL;
	struct wlr_surface *focus = server->seat->keyboard_state.focused_surface;
	if (shady_layout_active(server) && focus) {
		struct shady_toplevel *t;
		wl_list_for_each(t, &server->toplevels, link) {
			if (t->xdg_toplevel->base->surface != focus) continue;
			if (t->fullscreen && t->xdg_toplevel->base->surface->mapped &&
					toplevel_workspace(t) == current_workspace(server))
				hide_on = fullscreen_output(server, t);
			break;
		}
	}
	/* Applied to every top-layer surface each time, so one mapped while
	 * the bars are hidden is hidden too. */
	bool changed = hide_on != state->top_layers_hidden_on;
	state->top_layers_hidden_on = hide_on;
	struct shady_layer_surface *layer;
	wl_list_for_each(layer, &desktop->layer_surfaces, link) {
		if (!layer->layer_surface || !layer->scene_layer) continue;
		if (layer->layer_surface->current.layer != ZWLR_LAYER_SHELL_V1_LAYER_TOP) continue;
		struct wlr_scene_node *node = &layer->scene_layer->tree->node;
		bool enabled = !hide_on || layer->layer_surface->output != hide_on;
		if (node->enabled == enabled) continue;
		wlr_scene_node_set_enabled(node, enabled);
		changed = true;
	}
	if (changed) shady_render_schedule_all_outputs(server);
}

/* `layout.strip` for bars: "<screen width> <view> <active column>" and then
 * "|<x> <width> <windows>" per column, in logical pixels of the strip. */
static void publish_strip(struct shady_server *server, struct shady_layout_workspace *ws,
		const struct strip_metrics *m, const struct wlr_box *area) {
	struct shady_layout_state *state = shady_layout_state_for(server);
	char buf[SHADY_VALUE_MAX];
	int n = snprintf(buf, sizeof(buf), "%d %d %zu", area->width,
		(int)lround(ws->view_target), ws->count ? ws->active + 1 : 0);
	for (size_t c = 0; c < ws->count && n > 0 && (size_t)n < sizeof(buf) - 32; c++)
		n += snprintf(buf + n, sizeof(buf) - n, "|%d %d %zu", m->x[c], m->width[c],
			ws->columns[c].count);
	if (!strcmp(buf, state->published)) return;
	snprintf(state->published, sizeof(state->published), "%s", buf);
	shady_values_set(server, state, "layout.strip", buf);
}

/* Work out where every window of a workspace goes, ask the clients for
 * their sizes and move them (at once, or animated from where they are). */
static void arrange_workspace(struct shady_server *server, size_t index, bool animate) {
	struct shady_layout_state *state = shady_layout_state_for(server);
	if (!state || index >= SHADY_MAX_WORKSPACES) return;
	struct shady_layout_workspace *ws = &state->workspaces[index];
	struct wlr_box area, full;
	if (!layout_area(server, &area, &full)) return;
	if (ws->count == 0) {
		ws->view_valid = false;
		if (index == current_workspace(server)) {
			struct strip_metrics none = { .total = 0 };
			publish_strip(server, ws, &none, &area);
			update_top_layers(server);
		}
		return;
	}
	if (ws->active >= ws->count) ws->active = ws->count - 1;
	struct strip_metrics m;
	strip_measure(server, ws, &area, &full, &m);
	if (!ws->view_valid) {
		ws->view_target = -layout_gap(server);
		ws->view_valid = true;
		ensure_visible(server, ws, ws->active, ws->active, &m, &area, &full);
		clamp_view(server, ws, &m, &area);
		ws->view = ws->view_target;
	} else {
		ensure_visible(server, ws, ws->active, ws->active, &m, &area, &full);
		clamp_view(server, ws, &m, &area);
	}

	int gap = layout_gap(server);
	bool visible = index == current_workspace(server);
	float speed = server->config.layout_animation;
	if (!visible || speed <= 0.f) animate = false;
	if (!animate) ws->view = ws->view_target;

	for (size_t c = 0; c < ws->count; c++) {
		struct shady_layout_column *col = &ws->columns[c];
		if (col->active >= col->count) col->active = col->count ? col->count - 1 : 0;
		bool fullscreen = column_fullscreen(col);
		int avail = area.height - 2 * gap - (int)(col->count - 1) * gap;
		float total = 0.f;
		for (size_t r = 0; r < col->count; r++) {
			if (col->weights[r] <= 0.f) col->weights[r] = 1.f;
			total += col->weights[r];
		}
		int y = area.y + gap;
		for (size_t r = 0; r < col->count; r++) {
			struct shady_toplevel *t = col->rows[r];
			struct shady_layout_toplevel *lt = shady_layout_toplevel_for(t);
			if (!lt) continue;
			int h = r + 1 == col->count ? area.y + area.height - gap - y
				: (int)lround(avail * col->weights[r] / total);
			int ty = y, tw = m.width[c], th = h;
			y += h + gap;
			if (fullscreen) {
				if (t->fullscreen) {
					ty = full.y;
					th = full.height;
				}
			} else {
				int title = titlebar_height(t);
				ty += title;
				th -= title;
			}
			if (th < 1) th = 1;
			lt->target_strip_x = m.x[c];
			lt->target_y = ty;
			if (!lt->placed || !animate) {
				lt->strip_x = lt->target_strip_x;
				lt->y = lt->target_y;
				lt->placed = true;
			}
			set_tiled_state(t, true);
			ask_size(t, tw, th);
			/* xdg_toplevel.configure_bounds was added in xdg-shell v4. Every
			 * call schedules a configure, so only when the bounds change:
			 * a configure makes clients like browsers lay out again. */
			if (t->xdg_toplevel->base->initialized &&
					t->xdg_toplevel->base->client->shell->version >= 4 &&
					(lt->bounds_w != area.width || lt->bounds_h != area.height)) {
				lt->bounds_w = area.width;
				lt->bounds_h = area.height;
				wlr_xdg_toplevel_set_bounds(t->xdg_toplevel, area.width, area.height);
			}
		}
	}
	apply_positions(server, index);
	if (visible) {
		publish_strip(server, ws, &m, &area);
		update_top_layers(server);
		shady_render_schedule_all_outputs(server);
		if (animate) schedule_animation(state);
	}
}

void shady_layout_arrange(struct shady_server *server) {
	struct shady_layout_state *state = shady_layout_state_for(server);
	if (!shady_layout_active(server) || state->arranging) return;
	state->arranging = true;
	arrange_workspace(server, current_workspace(server), true);
	state->arranging = false;
}

static double approach(double value, double target, double a) {
	double next = value + (target - value) * a;
	return fabs(target - next) < 0.5 ? target : next;
}

static bool animate_workspace(struct shady_server *server, size_t index, double a,
		double slide_target) {
	struct shady_layout_state *state = shady_layout_state_for(server);
	struct shady_layout_workspace *ws = &state->workspaces[index];
	bool moving = false;
	ws->view = approach(ws->view, ws->view_target, a);
	ws->slide = approach(ws->slide, slide_target, a);
	moving |= ws->view != ws->view_target || ws->slide != slide_target;
	for (size_t c = 0; c < ws->count; c++) {
		struct shady_layout_column *col = &ws->columns[c];
		for (size_t r = 0; r < col->count; r++) {
			struct shady_layout_toplevel *lt = shady_layout_toplevel_for(col->rows[r]);
			if (!lt) continue;
			lt->strip_x = approach(lt->strip_x, lt->target_strip_x, a);
			lt->y = approach(lt->y, lt->target_y, a);
			moving |= lt->strip_x != lt->target_strip_x || lt->y != lt->target_y;
		}
	}
	apply_positions(server, index);
	return moving;
}

static void finish_leaving(struct shady_server *server, size_t index) {
	struct shady_layout_state *state = shady_layout_state_for(server);
	struct shady_layout_workspace *ws = &state->workspaces[index];
	ws->leaving = false;
	ws->slide = 0.0;
	if (index == current_workspace(server)) return;
	for (size_t c = 0; c < ws->count; c++) {
		struct shady_layout_column *col = &ws->columns[c];
		for (size_t r = 0; r < col->count; r++) {
			struct shady_toplevel *t = col->rows[r];
			if (t->scene_tree && toplevel_workspace(t) != current_workspace(server))
				wlr_scene_node_set_enabled(&t->scene_tree->node, false);
		}
	}
	apply_positions(server, index);
}

#if SHADY_HAS_SPATIAL
/* The overview: the camera pulls back until the whole strip of the current
 * workspace is on screen, tilted a little so it reads as a row of panes. */
static struct shady_camera *overview_camera(struct shady_server *server) {
	struct shady_spatial_state *spatial = shady_spatial_state(server);
	if (!spatial || !server->config.spatial_mode) return NULL;
	return &spatial->runtime.camera;
}

static bool camera_step(struct shady_server *server, double a) {
	struct shady_layout_state *state = shady_layout_state_for(server);
	struct shady_camera *cam = overview_camera(server);
	if (!cam || !state->camera_owned || cam->first_person) return false;
	float distance = SHADY_CAMERA_DEFAULT_DIST, tx = 0.f, ty = 0.f, pitch = 0.f;
	struct wlr_box area, full;
	if (state->overview && layout_area(server, &area, &full)) {
		struct shady_layout_workspace *ws = &state->workspaces[current_workspace(server)];
		struct strip_metrics m;
		strip_measure(server, ws, &area, &full, &m);
		double total = m.total > area.width ? m.total : area.width;
		double zoom = (total + 2 * layout_gap(server)) / area.width * 1.08;
		if (zoom < 1.35) zoom = 1.35;
		if (zoom > 6.0) zoom = 6.0;
		double cx = area.x - full.x + total / 2.0 - ws->view_target;
		if (m.total < area.width) cx = area.x - full.x + area.width / 2.0;
		double cy = area.y - full.y + area.height / 2.0;
		distance = (float)(SHADY_CAMERA_DEFAULT_DIST * zoom);
		tx = (float)((cx - full.width / 2.0) / full.height);
		ty = (float)((full.height / 2.0 - cy) / full.height);
		pitch = -0.10f;
	}
	/* Close enough (a tenth of a pixel or so) is there: comparing floats
	 * for equality after scaling kept the camera "moving" for ever. */
	float *values[4] = { &cam->distance, &cam->target_x, &cam->target_y, &cam->pitch };
	const float targets[4] = { distance, tx, ty, pitch };
	bool moving = false;
	for (size_t i = 0; i < 4; i++) {
		float v = *values[i] + (targets[i] - *values[i]) * (float)a;
		if (fabsf(targets[i] - v) < 1e-4f) v = targets[i];
		else moving = true;
		*values[i] = v;
	}
	if (!state->overview && !moving) {
		cam->yaw = 0.f;
		state->camera_owned = false;
	}
	return moving;
}
#endif

bool shady_layout_overview(struct shady_server *server, int enable) {
	struct shady_layout_state *state = shady_layout_state_for(server);
	if (!shady_layout_active(server)) return false;
#if SHADY_HAS_SPATIAL
	struct shady_camera *cam = overview_camera(server);
	if (!cam || cam->first_person) return false;
	bool on = enable < 0 ? !state->overview : enable > 0;
	if (on == state->overview) return true;
	state->overview = on;
	state->camera_owned = true;
	shady_values_set(server, state, "layout.overview", on ? "1" : "0");
	schedule_animation(state);
	return true;
#else
	(void)state;
	(void)enable;
	return false;
#endif
}

static int animation_tick(void *data) {
	struct shady_layout_state *state = data;
	struct shady_server *server = state->server;
	uint64_t now = now_ns();
	double dt = (now - state->last_tick_ns) / 1e9;
	state->last_tick_ns = now;
	if (dt <= 0.0 || dt > 0.1) dt = ANIMATION_INTERVAL_MS / 1000.0;
	float speed = server->config.layout_animation;
	double a = speed > 0.f ? 1.0 - exp(-speed * dt) : 1.0;

	bool moving = false;
#if SHADY_HAS_SPATIAL
	moving |= camera_step(server, a);
#endif
	size_t current = current_workspace(server);
	for (size_t i = 0; i < SHADY_MAX_WORKSPACES; i++) {
		struct shady_layout_workspace *ws = &state->workspaces[i];
		if (i == current) {
			moving |= animate_workspace(server, i, a, 0.0);
		} else if (ws->leaving) {
			if (animate_workspace(server, i, a, ws->slide_target) &&
					fabs(ws->slide - ws->slide_target) > 1.0)
				moving = true;
			else
				finish_leaving(server, i);
		}
	}
	shady_render_schedule_all_outputs(server);
	if (moving) wl_event_source_timer_update(state->timer, ANIMATION_INTERVAL_MS);
	return 0;
}

static void schedule_animation(struct shady_layout_state *state) {
	if (!state->timer) return;
	state->last_tick_ns = now_ns();
	wl_event_source_timer_update(state->timer, ANIMATION_INTERVAL_MS);
}

static bool wants_tiling(struct shady_toplevel *toplevel) {
	struct shady_layout_toplevel *lt = shady_layout_toplevel_for(toplevel);
	if (!lt) return false;
	if (lt->force_float) return false;
	if (lt->force_tile) return true;
	struct wlr_xdg_toplevel *xdg = toplevel->xdg_toplevel;
	/* Dialogs and fixed-size windows float. */
	if (xdg->parent) return false;
	const struct wlr_xdg_toplevel_state *s = &xdg->current;
	if (s->min_width > 0 && s->min_width == s->max_width &&
			s->min_height > 0 && s->min_height == s->max_height)
		return false;
	return true;
}

static float default_proportion(struct shady_server *server) {
	float p = server->config.layout_default_width;
	return p > 0.05f && p <= 1.f ? p : 0.5f;
}

static bool insert_column(struct shady_layout_workspace *ws, size_t at,
		struct shady_toplevel *toplevel, float proportion) {
	if (ws->count >= SHADY_LAYOUT_MAX_COLUMNS) return false;
	if (at > ws->count) at = ws->count;
	memmove(&ws->columns[at + 1], &ws->columns[at],
		(ws->count - at) * sizeof(ws->columns[0]));
	ws->columns[at] = (struct shady_layout_column){
		.rows = { toplevel },
		.weights = { 1.f },
		.count = 1,
		.proportion = proportion,
		.preset = -1,
	};
	ws->count++;
	return true;
}

static void remove_column(struct shady_layout_workspace *ws, size_t c) {
	if (c >= ws->count) return;
	memmove(&ws->columns[c], &ws->columns[c + 1],
		(ws->count - c - 1) * sizeof(ws->columns[0]));
	ws->count--;
	memset(&ws->columns[ws->count], 0, sizeof(ws->columns[0]));
}

static void remove_row(struct shady_layout_column *col, size_t r) {
	if (r >= col->count) return;
	memmove(&col->rows[r], &col->rows[r + 1], (col->count - r - 1) * sizeof(col->rows[0]));
	memmove(&col->weights[r], &col->weights[r + 1],
		(col->count - r - 1) * sizeof(col->weights[0]));
	col->count--;
	col->rows[col->count] = NULL;
	if (col->active >= col->count && col->count) col->active = col->count - 1;
	else if (col->active > r) col->active--;
}

/* Take a window out of its strip. Returns the window that should have the
 * focus now, if it was in the current workspace. */
static struct shady_toplevel *detach(struct shady_toplevel *toplevel) {
	size_t c, r;
	struct shady_layout_workspace *ws = strip_of(toplevel, &c, &r);
	struct shady_layout_toplevel *lt = shady_layout_toplevel_for(toplevel);
	if (lt) {
		lt->tiled = false;
		lt->placed = false;
		lt->asked_w = lt->asked_h = 0;
	}
	if (!ws) return NULL;
	struct shady_layout_column *col = &ws->columns[c];
	remove_row(col, r);
	if (col->count == 0) {
		size_t active = ws->active;
		remove_column(ws, c);
		/* Closing the focused column focuses the one to its right, as in
		 * niri, or the new last one. */
		if (active > c) ws->active = active - 1;
		else if (active == c) ws->active = c < ws->count ? c : (ws->count ? ws->count - 1 : 0);
	}
	if (ws->count == 0) return NULL;
	struct shady_layout_column *next = &ws->columns[ws->active];
	return next->count ? next->rows[next->active] : NULL;
}

static void attach(struct shady_toplevel *toplevel, size_t index, float proportion) {
	struct shady_layout_state *state = shady_layout_state_for(toplevel->server);
	struct shady_layout_toplevel *lt = shady_layout_toplevel_for(toplevel);
	struct shady_layout_workspace *ws = &state->workspaces[index];
	size_t at = ws->count ? ws->active + 1 : 0;
	if (!insert_column(ws, at, toplevel, proportion)) return;
	if (toplevel->maximized) ws->columns[at].full_width = true;
	ws->active = at;
	lt->tiled = true;
	lt->workspace = index;
	lt->placed = false;
}

void shady_layout_initial_configure(struct shady_toplevel *toplevel) {
	struct shady_server *server = toplevel->server;
	if (!shady_layout_active(server) || !wants_tiling(toplevel)) return;
	struct wlr_box area, full;
	if (!layout_area(server, &area, &full)) return;
	struct shady_layout_toplevel *lt = shady_layout_toplevel_for(toplevel);
	struct shady_layout_column col = {
		.proportion = lt->rule_width > 0.f ? lt->rule_width : default_proportion(server),
	};
	int gap = layout_gap(server);
	int width = column_width(server, &col, &area, &full);
	int height = area.height - 2 * gap - titlebar_height(toplevel);
	if (height > 0) wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, width, height);
	lt->asked_w = width;
	lt->asked_h = height;
	if (toplevel->xdg_toplevel->base->client->shell->version >= 2)
		wlr_xdg_toplevel_set_tiled(toplevel->xdg_toplevel, WLR_EDGE_LEFT |
			WLR_EDGE_RIGHT | WLR_EDGE_TOP | WLR_EDGE_BOTTOM);
	lt->tiled_state_sent = true;
}

static void place_floating(struct shady_toplevel *toplevel) {
	struct shady_server *server = toplevel->server;
	struct wlr_box area, full;
	if (!layout_area(server, &area, &full)) return;
	struct shady_layout_toplevel *lt = shady_layout_toplevel_for(toplevel);
	set_tiled_state(toplevel, false);
	struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
	int width = geo->width > 0 ? geo->width : area.width / 2;
	int height = geo->height > 0 ? geo->height : area.height / 2;
	/* Out of a tall column, float at a comfortable size. */
	if (width > area.width * 3 / 4) width = area.width * 3 / 4;
	if (height > area.height * 3 / 4) height = area.height * 3 / 4;
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, width, height);
	if (lt) lt->asked_w = lt->asked_h = 0;
	wlr_scene_node_set_position(&toplevel->scene_tree->node,
		area.x + (area.width - width) / 2,
		area.y + titlebar_height(toplevel) + (area.height - height) / 2);
}

void shady_layout_rule(struct shady_toplevel *toplevel, int floating, float width) {
	struct shady_layout_toplevel *lt = shady_layout_toplevel_for(toplevel);
	if (!lt) return;
	if (floating > 0) { lt->force_float = true; lt->force_tile = false; }
	else if (floating == 0) { lt->force_tile = true; lt->force_float = false; }
	if (width > 0.f && width <= 1.f) lt->rule_width = width;
}

static void layout_toplevel_map(struct shady_toplevel *toplevel) {
	struct shady_server *server = toplevel->server;
	if (!shady_layout_active(server)) return;
	struct shady_layout_toplevel *lt = shady_layout_toplevel_for(toplevel);
	if (!lt) return;
	lt->workspace = toplevel_workspace(toplevel);
	if (!wants_tiling(toplevel)) {
		/* Asked for a column's size before a rule made it float. */
		if (lt->tiled_state_sent) place_floating(toplevel);
		set_tiled_state(toplevel, false);
		return;
	}
	attach(toplevel, lt->workspace,
		lt->rule_width > 0.f ? lt->rule_width : default_proportion(server));
	struct shady_layout_state *state = shady_layout_state_for(server);
	if (!state->arranging) {
		state->arranging = true;
		arrange_workspace(server, lt->workspace, true);
		state->arranging = false;
	}
}

/* Make the window the next one the core focuses, by putting it first in the
 * focus order. */
static void prefer_focus(struct shady_toplevel *toplevel) {
	if (!toplevel || !toplevel->mapped) return;
	struct shady_server *server = toplevel->server;
	wl_list_remove(&toplevel->link);
	wl_list_insert(&server->toplevels, &toplevel->link);
}

static void layout_toplevel_unmap(struct shady_toplevel *toplevel) {
	struct shady_server *server = toplevel->server;
	struct shady_layout_toplevel *lt = shady_layout_toplevel_for(toplevel);
	if (!lt || !lt->tiled) return;
	size_t index = lt->workspace;
	bool focused = server->toplevels.next == &toplevel->link;
	struct shady_toplevel *next = detach(toplevel);
	lt->tiled_state_sent = false;
	if (focused && next && index == current_workspace(server)) {
		prefer_focus(next);
		/* The unmapping window still has to be first for the core to see
		 * that it had the focus. */
		wl_list_remove(&toplevel->link);
		wl_list_insert(&server->toplevels, &toplevel->link);
	}
	arrange_workspace(server, index, true);
}

static void layout_toplevel_destroy(struct shady_toplevel *toplevel) {
	struct shady_layout_toplevel *lt = shady_layout_toplevel_for(toplevel);
	if (!lt || !lt->tiled) return;
	size_t index = lt->workspace;
	detach(toplevel);
	arrange_workspace(toplevel->server, index, true);
}

void shady_layout_workspace_changed(struct shady_toplevel *toplevel, size_t old_workspace) {
	struct shady_layout_toplevel *lt = shady_layout_toplevel_for(toplevel);
	if (!lt || !lt->tiled || !shady_layout_active(toplevel->server)) return;
	(void)old_workspace;
	size_t from = lt->workspace;
	struct shady_layout_column *col = NULL;
	size_t c;
	float proportion = default_proportion(toplevel->server);
	struct shady_layout_workspace *ws = strip_of(toplevel, &c, NULL);
	if (ws) {
		col = &ws->columns[c];
		proportion = col->proportion;
	}
	detach(toplevel);
	attach(toplevel, toplevel_workspace(toplevel), proportion);
	arrange_workspace(toplevel->server, from, true);
	arrange_workspace(toplevel->server, lt->workspace, false);
}

static struct shady_toplevel *focused_toplevel(struct shady_server *server) {
	struct wlr_surface *surface = server->seat->keyboard_state.focused_surface;
	struct shady_toplevel *toplevel;
	if (surface) {
		struct wlr_xdg_toplevel *xdg = wlr_xdg_toplevel_try_from_wlr_surface(
			wlr_surface_get_root_surface(surface));
		wl_list_for_each(toplevel, &server->toplevels, link)
			if (toplevel->xdg_toplevel == xdg) return toplevel;
	}
	wl_list_for_each(toplevel, &server->toplevels, link)
		if (toplevel->mapped && toplevel->scene_tree && toplevel->scene_tree->node.enabled)
			return toplevel;
	return NULL;
}

/* The current workspace's strip and the focused column, also when a
 * floating window or nothing has the focus. */
static struct shady_layout_workspace *current_strip(struct shady_server *server,
		size_t *column, size_t *row) {
	struct shady_layout_state *state = shady_layout_state_for(server);
	if (!shady_layout_active(server)) return NULL;
	struct shady_toplevel *focused = focused_toplevel(server);
	if (focused) {
		struct shady_layout_workspace *ws = strip_of(focused, column, row);
		if (ws) return ws;
	}
	struct shady_layout_workspace *ws = &state->workspaces[current_workspace(server)];
	if (ws->count == 0) return NULL;
	if (ws->active >= ws->count) ws->active = ws->count - 1;
	*column = ws->active;
	*row = ws->columns[ws->active].active;
	return ws;
}

static void focus_cell(struct shady_layout_workspace *ws, size_t c, size_t r) {
	if (c >= ws->count) return;
	struct shady_layout_column *col = &ws->columns[c];
	if (r >= col->count) r = col->active < col->count ? col->active : 0;
	if (!col->count) return;
	focus_toplevel(col->rows[r]);
}

bool shady_layout_focus(struct shady_server *server, enum shady_layout_direction direction) {
	size_t c, r;
	struct shady_layout_workspace *ws = current_strip(server, &c, &r);
	if (!ws) return false;
	struct shady_toplevel *focused = focused_toplevel(server);
	bool from_floating = focused && !shady_layout_is_tiled(focused);
	switch (direction) {
	case SHADY_LAYOUT_LEFT:
		if (from_floating) break;
		if (c == 0) return false;
		c--;
		r = SIZE_MAX;
		break;
	case SHADY_LAYOUT_RIGHT:
		if (from_floating) break;
		if (c + 1 >= ws->count) return false;
		c++;
		r = SIZE_MAX;
		break;
	case SHADY_LAYOUT_UP:
		if (r == 0) return false;
		r--;
		break;
	case SHADY_LAYOUT_DOWN:
		if (r + 1 >= ws->columns[c].count) return false;
		r++;
		break;
	case SHADY_LAYOUT_FIRST:
		c = 0;
		r = SIZE_MAX;
		break;
	case SHADY_LAYOUT_LAST:
		c = ws->count - 1;
		r = SIZE_MAX;
		break;
	}
	focus_cell(ws, c, r);
	return true;
}

static void swap_columns(struct shady_layout_workspace *ws, size_t a, size_t b) {
	struct shady_layout_column tmp = ws->columns[a];
	ws->columns[a] = ws->columns[b];
	ws->columns[b] = tmp;
}

bool shady_layout_move(struct shady_server *server, enum shady_layout_direction direction) {
	size_t c, r;
	struct shady_layout_workspace *ws = current_strip(server, &c, &r);
	struct shady_toplevel *focused = focused_toplevel(server);
	if (!ws || !shady_layout_is_tiled(focused)) return false;
	struct shady_layout_column *col = &ws->columns[c];
	switch (direction) {
	case SHADY_LAYOUT_LEFT:
		if (c == 0) return false;
		swap_columns(ws, c, c - 1);
		ws->active = c - 1;
		break;
	case SHADY_LAYOUT_RIGHT:
		if (c + 1 >= ws->count) return false;
		swap_columns(ws, c, c + 1);
		ws->active = c + 1;
		break;
	case SHADY_LAYOUT_UP:
	case SHADY_LAYOUT_DOWN: {
		size_t other = direction == SHADY_LAYOUT_UP ? r - 1 : r + 1;
		if ((direction == SHADY_LAYOUT_UP && r == 0) || other >= col->count) return false;
		struct shady_toplevel *t = col->rows[r];
		col->rows[r] = col->rows[other];
		col->rows[other] = t;
		float w = col->weights[r];
		col->weights[r] = col->weights[other];
		col->weights[other] = w;
		col->active = other;
		break;
	}
	case SHADY_LAYOUT_FIRST:
	case SHADY_LAYOUT_LAST: {
		struct shady_layout_column moved = *col;
		remove_column(ws, c);
		size_t at = direction == SHADY_LAYOUT_FIRST ? 0 : ws->count;
		memmove(&ws->columns[at + 1], &ws->columns[at],
			(ws->count - at) * sizeof(ws->columns[0]));
		ws->columns[at] = moved;
		ws->count++;
		ws->active = at;
		break;
	}
	}
	shady_layout_arrange(server);
	return true;
}

bool shady_layout_consume_or_expel(struct shady_server *server, bool left) {
	size_t c, r;
	struct shady_layout_workspace *ws = current_strip(server, &c, &r);
	struct shady_toplevel *focused = focused_toplevel(server);
	if (!ws || !shady_layout_is_tiled(focused)) return false;
	struct shady_layout_column *col = &ws->columns[c];
	struct shady_toplevel *t = col->rows[r];
	if (col->count > 1) {
		/* Expel into a column of its own on that side. */
		if (ws->count >= SHADY_LAYOUT_MAX_COLUMNS) return false;
		float proportion = col->proportion;
		int width = col->width;
		remove_row(col, r);
		size_t at = left ? c : c + 1;
		if (!insert_column(ws, at, t, proportion)) return false;
		ws->columns[at].width = width;
		ws->active = at;
	} else {
		/* Join the neighbouring column, at its bottom. */
		if ((left && c == 0) || (!left && c + 1 >= ws->count)) return false;
		size_t target = left ? c - 1 : c + 1;
		struct shady_layout_column *dst = &ws->columns[target];
		if (dst->count >= SHADY_LAYOUT_MAX_ROWS) return false;
		dst->rows[dst->count] = t;
		dst->weights[dst->count] = 1.f;
		dst->active = dst->count++;
		remove_column(ws, c);
		ws->active = left ? target : target - 1;
	}
	shady_layout_arrange(server);
	return true;
}

/* "+10%", "-10%", "50%" or pixels "900", "+40", "-40". */
static bool parse_amount(const char *spec, bool *relative, bool *percent, double *value) {
	if (!spec || !*spec) return false;
	*relative = spec[0] == '+' || spec[0] == '-';
	char *end = NULL;
	*value = strtod(spec, &end);
	if (end == spec) return false;
	*percent = *end == '%';
	if (*percent) end++;
	return *end == '\0';
}

bool shady_layout_set_width(struct shady_server *server, const char *spec) {
	size_t c, r;
	struct shady_layout_workspace *ws = current_strip(server, &c, &r);
	struct wlr_box area, full;
	bool relative, percent;
	double value;
	if (!ws || !layout_area(server, &area, &full) ||
			!parse_amount(spec, &relative, &percent, &value)) return false;
	struct shady_layout_column *col = &ws->columns[c];
	int gap = layout_gap(server);
	double span = area.width - gap;
	int current = column_width(server, col, &area, &full);
	col->full_width = false;
	col->preset = -1;
	if (percent) {
		double p = relative ? (current + gap) / span + value / 100.0 : value / 100.0;
		if (p < 0.1) p = 0.1;
		if (p > 1.0) p = 1.0;
		col->proportion = (float)p;
		col->width = 0;
	} else {
		int w = (int)lround(relative ? current + value : value);
		if (w < MIN_COLUMN_WIDTH) w = MIN_COLUMN_WIDTH;
		col->width = w;
	}
	shady_layout_arrange(server);
	return true;
}

static void set_row_height(struct shady_layout_column *col, size_t r, int height, int avail) {
	if (col->count < 2) return;
	int min_total = MIN_ROW_HEIGHT * (int)(col->count - 1);
	if (height < MIN_ROW_HEIGHT) height = MIN_ROW_HEIGHT;
	if (height > avail - min_total) height = avail - min_total;
	float others = 0.f;
	for (size_t i = 0; i < col->count; i++) if (i != r) others += col->weights[i];
	float rest = (float)(avail - height);
	for (size_t i = 0; i < col->count; i++) {
		if (i == r) col->weights[i] = (float)height;
		else col->weights[i] = others > 0.f ? col->weights[i] / others * rest : rest;
	}
}

static int row_height(const struct shady_layout_column *col, size_t r, int avail) {
	float total = 0.f;
	for (size_t i = 0; i < col->count; i++) total += col->weights[i] > 0.f ? col->weights[i] : 1.f;
	return (int)lround(avail * (col->weights[r] > 0.f ? col->weights[r] : 1.f) / total);
}

bool shady_layout_set_height(struct shady_server *server, const char *spec) {
	size_t c, r;
	struct shady_layout_workspace *ws = current_strip(server, &c, &r);
	struct wlr_box area, full;
	bool relative, percent;
	double value;
	if (!ws || !layout_area(server, &area, &full) ||
			!parse_amount(spec, &relative, &percent, &value)) return false;
	struct shady_layout_column *col = &ws->columns[c];
	if (col->count < 2) return false;
	int gap = layout_gap(server);
	int avail = area.height - 2 * gap - (int)(col->count - 1) * gap;
	int current = row_height(col, r, avail);
	double amount = percent ? value / 100.0 * (area.height - 2 * gap) : value;
	set_row_height(col, r, (int)lround(relative ? current + amount : amount), avail);
	shady_layout_arrange(server);
	return true;
}

bool shady_layout_reset_height(struct shady_server *server) {
	size_t c, r;
	struct shady_layout_workspace *ws = current_strip(server, &c, &r);
	if (!ws) return false;
	struct shady_layout_column *col = &ws->columns[c];
	for (size_t i = 0; i < col->count; i++) col->weights[i] = 1.f;
	shady_layout_arrange(server);
	return true;
}

bool shady_layout_cycle_width(struct shady_server *server, bool forward) {
	struct shady_layout_state *state = shady_layout_state_for(server);
	size_t c, r;
	struct shady_layout_workspace *ws = current_strip(server, &c, &r);
	struct wlr_box area, full;
	if (!ws || state->preset_count == 0 || !layout_area(server, &area, &full)) return false;
	struct shady_layout_column *col = &ws->columns[c];
	int n = (int)state->preset_count;
	int next;
	if (col->preset >= 0 && col->preset < n) {
		next = (col->preset + (forward ? 1 : n - 1)) % n;
	} else {
		/* Start from the preset just past the column's current width. */
		int gap = layout_gap(server);
		double p = (column_width(server, col, &area, &full) + gap) / (double)(area.width - gap);
		next = forward ? 0 : n - 1;
		for (int i = 0; i < n; i++) {
			int k = forward ? i : n - 1 - i;
			if (forward ? state->presets[k] > p + 0.01 : state->presets[k] < p - 0.01) {
				next = k;
				break;
			}
		}
	}
	col->preset = next;
	col->proportion = state->presets[next];
	col->width = 0;
	col->full_width = false;
	shady_layout_arrange(server);
	return true;
}

bool shady_layout_toggle_full_width(struct shady_server *server) {
	size_t c, r;
	struct shady_layout_workspace *ws = current_strip(server, &c, &r);
	if (!ws) return false;
	ws->columns[c].full_width = !ws->columns[c].full_width;
	shady_layout_arrange(server);
	return true;
}

bool shady_layout_center(struct shady_server *server) {
	size_t c, r;
	struct shady_layout_workspace *ws = current_strip(server, &c, &r);
	struct wlr_box area, full;
	if (!ws || !layout_area(server, &area, &full)) return false;
	struct strip_metrics m;
	strip_measure(server, ws, &area, &full, &m);
	ws->view_target = m.x[c] - (area.width - m.width[c]) / 2.0;
	struct shady_layout_state *state = shady_layout_state_for(server);
	apply_positions(server, current_workspace(server));
	publish_strip(server, ws, &m, &area);
	schedule_animation(state);
	return true;
}

bool shady_layout_toggle_floating(struct shady_toplevel *toplevel) {
	if (!toplevel || !toplevel->mapped || !shady_layout_active(toplevel->server)) return false;
	struct shady_server *server = toplevel->server;
	struct shady_layout_toplevel *lt = shady_layout_toplevel_for(toplevel);
	if (!lt) return false;
	if (lt->tiled) {
		size_t index = lt->workspace;
		detach(toplevel);
		lt->force_float = true;
		lt->force_tile = false;
		place_floating(toplevel);
		arrange_workspace(server, index, true);
	} else {
		lt->force_float = false;
		lt->force_tile = true;
		attach(toplevel, toplevel_workspace(toplevel), default_proportion(server));
		arrange_workspace(server, lt->workspace, true);
	}
	focus_toplevel(toplevel);
	shady_event_emit_window(server, SHADY_EVENT_WINDOW_STATE_CHANGED, toplevel);
	return true;
}

bool shady_layout_set_presets(struct shady_server *server, const float *presets, size_t count) {
	struct shady_layout_state *state = shady_layout_state_for(server);
	if (!state || count == 0 || count > SHADY_LAYOUT_MAX_PRESETS) return false;
	for (size_t i = 0; i < count; i++)
		if (!(presets[i] > 0.05f && presets[i] <= 1.f)) return false;
	memcpy(state->presets, presets, count * sizeof(presets[0]));
	state->preset_count = count;
	return true;
}

static bool workspace_is_empty(struct shady_server *server, size_t index) {
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link)
		if (toplevel->mapped && toplevel_workspace(toplevel) == index) return false;
	return true;
}

bool shady_layout_workspace_step(struct shady_server *server, int delta, bool move_window) {
	struct shady_workspace_state *wss = shady_workspace_state_for(server);
	if (!wss || delta == 0) return false;
	const char *name = wss->names[wss->current];
	char *end = NULL;
	long number = strtol(name, &end, 10);
	char target[SHADY_WORKSPACE_NAME_MAX];
	if (end != name && *end == '\0') {
		/* Numbered workspaces: one past the last is made on demand, but
		 * not past an empty one. */
		long next = number + delta;
		if (next < 1) return false;
		snprintf(target, sizeof(target), "%ld", next);
		bool exists = false;
		for (size_t i = 0; i < wss->count; i++)
			if (!strcmp(wss->names[i], target)) exists = true;
		if (!exists && delta > 0 && !move_window && workspace_is_empty(server, wss->current))
			return false;
	} else {
		long next = (long)wss->current + delta;
		if (next < 0 || next >= (long)wss->count) return false;
		snprintf(target, sizeof(target), "%s", wss->names[next]);
	}
	struct shady_toplevel *focused = move_window ? focused_toplevel(server) : NULL;
	if (move_window) {
		if (!focused) return false;
		shady_workspace_move_toplevel(focused, target);
	}
	bool ok = shady_workspace_switch(server, target);
	if (ok && focused) focus_toplevel(focused);
	return ok;
}

void shady_layout_describe(struct shady_server *server, size_t *columns, size_t *active,
		double *view_fraction, double *visible_fraction) {
	*columns = *active = 0;
	*view_fraction = 0.0;
	*visible_fraction = 1.0;
	struct shady_layout_state *state = shady_layout_state_for(server);
	struct wlr_box area, full;
	if (!shady_layout_active(server) || !layout_area(server, &area, &full)) return;
	struct shady_layout_workspace *ws = &state->workspaces[current_workspace(server)];
	*columns = ws->count;
	*active = ws->active;
	struct strip_metrics m;
	strip_measure(server, ws, &area, &full, &m);
	if (m.total <= 0) return;
	*visible_fraction = area.width >= m.total ? 1.0 : (double)area.width / m.total;
	*view_fraction = ws->view_target / m.total;
}

bool shady_layout_begin_interactive(struct shady_toplevel *toplevel, bool move) {
	if (!shady_layout_is_tiled(toplevel)) return false;
	if (move) {
		/* Dragging a tiled window lifts it out of the strip. */
		struct shady_layout_toplevel *lt = shady_layout_toplevel_for(toplevel);
		size_t index = lt->workspace;
		detach(toplevel);
		lt->force_float = true;
		lt->force_tile = false;
		set_tiled_state(toplevel, false);
		arrange_workspace(toplevel->server, index, true);
		shady_event_emit_window(toplevel->server, SHADY_EVENT_WINDOW_STATE_CHANGED, toplevel);
	}
	return false;
}

bool shady_layout_interactive_resize(struct shady_toplevel *toplevel,
		int width, int height, uint32_t edges) {
	size_t c, r;
	struct shady_layout_workspace *ws = strip_of(toplevel, &c, &r);
	struct wlr_box area, full;
	if (!ws || !layout_area(toplevel->server, &area, &full)) return false;
	struct shady_layout_column *col = &ws->columns[c];
	if (edges & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT)) {
		col->width = width;
		col->full_width = false;
		col->preset = -1;
	}
	if ((edges & (WLR_EDGE_TOP | WLR_EDGE_BOTTOM)) && col->count > 1) {
		int gap = layout_gap(toplevel->server);
		int avail = area.height - 2 * gap - (int)(col->count - 1) * gap;
		set_row_height(col, r, height + titlebar_height(toplevel), avail);
	}
	struct shady_layout_state *state = shady_layout_state_for(toplevel->server);
	state->arranging = true;
	arrange_workspace(toplevel->server, shady_layout_toplevel_for(toplevel)->workspace, false);
	state->arranging = false;
	return true;
}

/* Touchpad swipes: sideways scrolls the strip, up and down changes the
 * workspace. */
bool shady_layout_swipe_begin(struct shady_server *server, uint32_t fingers) {
	struct shady_layout_state *state = shady_layout_state_for(server);
	int wanted = server->config.gesture_fingers;
	if (!shady_layout_active(server) || wanted <= 0 || (int)fingers != wanted) return false;
	struct shady_layout_workspace *ws = &state->workspaces[current_workspace(server)];
	state->swipe_active = true;
	state->swipe_axis = 0;
	state->swipe_dx = state->swipe_dy = 0.0;
	state->swipe_view_start = ws->view_target;
	return true;
}

bool shady_layout_swipe_update(struct shady_server *server, double dx, double dy) {
	struct shady_layout_state *state = shady_layout_state_for(server);
	if (!state || !state->swipe_active) return false;
	state->swipe_dx += dx;
	state->swipe_dy += dy;
	if (state->swipe_axis == 0) {
		if (fabs(state->swipe_dx) < 12.0 && fabs(state->swipe_dy) < 12.0) return true;
		state->swipe_axis = fabs(state->swipe_dx) >= fabs(state->swipe_dy) ? 1 : 2;
	}
	if (state->swipe_axis != 1) return true;
	size_t index = current_workspace(server);
	struct shady_layout_workspace *ws = &state->workspaces[index];
	struct wlr_box area, full;
	if (ws->count == 0 || !layout_area(server, &area, &full)) return true;
	struct strip_metrics m;
	strip_measure(server, ws, &area, &full, &m);
	int gap = layout_gap(server);
	double min = -gap - area.width * 0.15;
	double max = m.total + gap - area.width;
	if (max < -gap) max = -gap;
	max += area.width * 0.15;
	/* The strip follows the fingers, a little faster than 1:1. */
	double view = state->swipe_view_start - state->swipe_dx * 1.5;
	if (view < min) view = min;
	if (view > max) view = max;
	ws->view = ws->view_target = view;
	apply_positions(server, index);
	publish_strip(server, ws, &m, &area);
	shady_render_schedule_all_outputs(server);
	return true;
}

bool shady_layout_swipe_end(struct shady_server *server, bool cancelled) {
	struct shady_layout_state *state = shady_layout_state_for(server);
	if (!state || !state->swipe_active) return false;
	state->swipe_active = false;
	size_t index = current_workspace(server);
	struct shady_layout_workspace *ws = &state->workspaces[index];
	if (state->swipe_axis == 2) {
		if (!cancelled && fabs(state->swipe_dy) > 60.0)
			shady_layout_workspace_step(server, state->swipe_dy < 0 ? 1 : -1, false);
		return true;
	}
	struct wlr_box area, full;
	if (ws->count == 0 || !layout_area(server, &area, &full)) return true;
	if (cancelled) {
		ws->view_target = state->swipe_view_start;
		schedule_animation(state);
		return true;
	}
	/* Settle on the column nearest the middle of the screen. */
	struct strip_metrics m;
	strip_measure(server, ws, &area, &full, &m);
	double middle = ws->view + area.width / 2.0;
	size_t best = ws->active;
	double best_distance = INFINITY;
	for (size_t c = 0; c < ws->count; c++) {
		double distance = fabs(m.x[c] + m.width[c] / 2.0 - middle);
		if (distance < best_distance) {
			best_distance = distance;
			best = c;
		}
	}
	ws->active = best;
	ensure_visible(server, ws, best, best, &m, &area, &full);
	clamp_view(server, ws, &m, &area);
	struct shady_layout_column *col = &ws->columns[best];
	if (col->count) focus_toplevel(col->rows[col->active]);
	schedule_animation(state);
	return true;
}

/* Super+wheel: workspaces up and down; Super+Shift+wheel or a sideways
 * wheel: columns. */
static bool layout_pointer_axis(struct shady_server *server,
		struct wlr_pointer_axis_event *event, uint32_t modifiers) {
	static double accumulated;
	static uint32_t last_step_ms;
	/* Nothing behind the lock screen may move or take the focus. */
	if (!shady_layout_active(server) || shady_desktop_state(server)->session_locked)
		return false;
	const uint32_t mask = WLR_MODIFIER_ALT | WLR_MODIFIER_SHIFT |
		WLR_MODIFIER_CTRL | WLR_MODIFIER_LOGO;
	uint32_t mods = modifiers & mask;
	if (mods != WLR_MODIFIER_LOGO && mods != (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT))
		return false;
	bool columns = event->orientation == WL_POINTER_AXIS_HORIZONTAL_SCROLL ||
		(mods & WLR_MODIFIER_SHIFT);
	accumulated += event->delta;
	double threshold = event->source == WL_POINTER_AXIS_SOURCE_FINGER ? 60.0 : 1.0;
	if (fabs(accumulated) < threshold) return true;
	int step = accumulated > 0 ? 1 : -1;
	accumulated = 0.0;
	if (columns) {
		shady_layout_focus(server, step > 0 ? SHADY_LAYOUT_RIGHT : SHADY_LAYOUT_LEFT);
	} else {
		if (event->time_msec - last_step_ms < 150) return true;
		last_step_ms = event->time_msec;
		shady_layout_workspace_step(server, step, false);
	}
	return true;
}

static void on_focus(struct shady_toplevel *toplevel) {
	struct shady_server *server = toplevel->server;
	size_t c, r;
	struct shady_layout_workspace *ws = strip_of(toplevel, &c, &r);
	struct shady_layout_toplevel *lt = shady_layout_toplevel_for(toplevel);
	if (ws && lt->workspace == current_workspace(server)) {
		struct wlr_box area, full;
		size_t prev = ws->active;
		ws->active = c;
		ws->columns[c].active = r;
		if (layout_area(server, &area, &full)) {
			struct strip_metrics m;
			strip_measure(server, ws, &area, &full, &m);
			ensure_visible(server, ws, c, prev, &m, &area, &full);
		}
		shady_layout_arrange(server);
	}
	update_top_layers(server);
}

/* A click in the overview picks a window: the core gives it the focus,
 * then the camera flies back into it. */
static void overview_leave_idle(void *data) {
	shady_layout_overview(data, 0);
}

static bool layout_pointer_button(struct shady_server *server,
		struct wlr_pointer_button_event *event, uint32_t modifiers) {
	(void)modifiers;
	struct shady_layout_state *state = shady_layout_state_for(server);
	if (state && state->overview && event->state == WL_POINTER_BUTTON_STATE_PRESSED &&
			!shady_desktop_state(server)->session_locked)
		wl_event_loop_add_idle(wl_display_get_event_loop(server->wl_display),
			overview_leave_idle, server);
	return false;
}

static void on_state_changed(struct shady_toplevel *toplevel) {
	struct shady_layout_toplevel *lt = shady_layout_toplevel_for(toplevel);
	if (!lt) return;
	size_t c;
	struct shady_layout_workspace *ws = strip_of(toplevel, &c, NULL);
	if (ws && toplevel->maximized != lt->was_maximized)
		ws->columns[c].full_width = toplevel->maximized;
	lt->was_maximized = toplevel->maximized;
	if (ws) arrange_workspace(toplevel->server, lt->workspace, true);
	update_top_layers(toplevel->server);
}

static void set_workspace_nodes(struct shady_layout_workspace *ws, bool enabled) {
	for (size_t c = 0; c < ws->count; c++)
		for (size_t r = 0; r < ws->columns[c].count; r++) {
			struct shady_toplevel *t = ws->columns[c].rows[r];
			if (t->scene_tree) wlr_scene_node_set_enabled(&t->scene_tree->node, enabled);
		}
}

/* The old workspace slides out one way while the new one slides in. */
static void on_workspace_changed(struct shady_server *server) {
	struct shady_layout_state *state = shady_layout_state_for(server);
	size_t index = current_workspace(server), old = state->shown_workspace;
	if (index == old || index >= SHADY_MAX_WORKSPACES) return;
	state->shown_workspace = index;
	struct shady_layout_workspace *ws = &state->workspaces[index];
	struct wlr_box area, full;
	ws->leaving = false;
	ws->slide = 0.0;
	if (server->config.layout_animation > 0.f && old < SHADY_MAX_WORKSPACES &&
			layout_area(server, &area, &full)) {
		double dir = index > old ? 1.0 : -1.0;
		struct shady_layout_workspace *prev = &state->workspaces[old];
		if (prev->count) {
			prev->leaving = true;
			prev->slide_target = -dir * full.height;
			set_workspace_nodes(prev, true);
		}
		ws->slide = dir * full.height;
	}
	state->arranging = true;
	arrange_workspace(server, index, false);
	state->arranging = false;
	update_top_layers(server);
	schedule_animation(state);
}

static void layout_event(shady_host host, const struct shady_event *event, void *user_data) {
	(void)host;
	struct shady_layout_state *state = user_data;
	if (!event || !shady_layout_active(state->server)) return;
	struct shady_toplevel *toplevel = (struct shady_toplevel *)event->object.window;
	switch (event->type) {
	case SHADY_EVENT_WINDOW_FOCUSED:
		on_focus(toplevel);
		break;
	case SHADY_EVENT_WINDOW_STATE_CHANGED:
		on_state_changed(toplevel);
		break;
	case SHADY_EVENT_WORKSPACE_CHANGED:
		on_workspace_changed(state->server);
		break;
	default:
		break;
	}
}

static bool layout_init(struct shady_server *server) {
	struct shady_layout_state *state = shady_layout_state_for(server);
	state->server = server;
	const float presets[] = { 1.f / 3.f, 0.5f, 2.f / 3.f };
	memcpy(state->presets, presets, sizeof(presets));
	state->preset_count = 3;
	state->timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->wl_display),
		animation_tick, state);
	if (!state->timer) return false;
	const uint32_t events[] = { SHADY_EVENT_WINDOW_FOCUSED,
		SHADY_EVENT_WINDOW_STATE_CHANGED, SHADY_EVENT_WORKSPACE_CHANGED };
	for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
		if (!shady_event_subscribe(server, events[i], layout_event, state)) {
			wl_event_source_remove(state->timer);
			state->timer = NULL;
			return false;
		}
	}
	return true;
}

static void layout_destroy(struct shady_server *server) {
	struct shady_layout_state *state = shady_layout_state_for(server);
	if (state && state->timer) wl_event_source_remove(state->timer);
	if (state) state->timer = NULL;
}

static const char *const layout_provides[] = {
	"desktop.layout",
	NULL,
};

static const char *const layout_requires[] = {
	"desktop.workspace",
	"desktop.protocols",
	NULL,
};

static const struct shady_module layout_module = {
	.name = "layout",
	.provides = layout_provides,
	.requires = layout_requires,
	.state_size = sizeof(struct shady_layout_state),
	.toplevel_state_size = sizeof(struct shady_layout_toplevel),
	.init = layout_init,
	.destroy = layout_destroy,
	.toplevel_map = layout_toplevel_map,
	.toplevel_unmap = layout_toplevel_unmap,
	.toplevel_destroy = layout_toplevel_destroy,
	.pointer_axis = layout_pointer_axis,
	.pointer_button = layout_pointer_button,
};

const struct shady_module *shady_layout_module(void) {
	return &layout_module;
}
