/* Adapted from wlroots 0.20.2 TinyWL (CC0). See LICENSES/tinywl-CC0.txt. */
#include <assert.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

#include "shady.h"
#include "module/module.h"
#include "render/render.h"

static void set_scene_buffer_opacity(struct wlr_scene_buffer *buffer,
		int sx, int sy, void *data) {
	(void)sx;
	(void)sy;
	float opacity = *(float *)data;
	wlr_scene_buffer_set_opacity(buffer, opacity);
}

static void apply_toplevel_opacity(struct shady_toplevel *toplevel) {
	if (!toplevel || !toplevel->scene_tree) return;
	float opacity = toplevel->server->config.window_opacity;
	wlr_scene_node_for_each_buffer(&toplevel->scene_tree->node,
		set_scene_buffer_opacity, &opacity);
}

static bool toplevel_is_logically_focused(const struct shady_toplevel *toplevel) {
	return toplevel && !wl_list_empty(&toplevel->server->toplevels) &&
		toplevel->server->toplevels.next == &toplevel->link;
}

void shady_toplevel_get_border(const struct shady_toplevel *toplevel,
		float *width, float color[4]) {
	float out_width = 0.f;
	float out_color[4] = {0.f, 0.f, 0.f, 0.f};
	if (toplevel && !toplevel->fullscreen) {
		if (toplevel->border_override) {
			out_width = toplevel->border_width;
			for (size_t i = 0; i < 4; i++) out_color[i] = toplevel->border_color[i];
		} else {
			const bool focused = toplevel_is_logically_focused(toplevel);
			out_width = toplevel->server->config.window_border_width;
			const float *rgb = focused
				? toplevel->server->config.window_border_focus_color
				: toplevel->server->config.window_border_color;
			out_color[0] = rgb[0];
			out_color[1] = rgb[1];
			out_color[2] = rgb[2];
			out_color[3] = 1.f;
		}
	}
	if (width) *width = out_width;
	if (color) for (size_t i = 0; i < 4; i++) color[i] = out_color[i];
}

void shady_toplevel_refresh_border(struct shady_toplevel *toplevel) {
	if (!toplevel || !toplevel->focus_border_tree) return;
	float border_width = 0.f, color[4];
	shady_toplevel_get_border(toplevel, &border_width, color);
	int px = border_width > 0.f ? (int)(border_width + 0.5f) : 0;
	struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
	int width = surface ? surface->current.width : 0;
	int height = surface ? surface->current.height : 0;
	bool enabled = toplevel->mapped && px > 0 && width > 0 && height > 0 && color[3] > 0.f;
	wlr_scene_node_set_enabled(&toplevel->focus_border_tree->node, enabled);
	if (!enabled) return;

	for (size_t i = 0; i < 4; i++) {
		if (toplevel->focus_border[i]) wlr_scene_rect_set_color(toplevel->focus_border[i], color);
	}
	wlr_scene_rect_set_size(toplevel->focus_border[0], width, px);
	wlr_scene_node_set_position(&toplevel->focus_border[0]->node, 0, -px);
	wlr_scene_rect_set_size(toplevel->focus_border[1], width, px);
	wlr_scene_node_set_position(&toplevel->focus_border[1]->node, 0, height);
	wlr_scene_rect_set_size(toplevel->focus_border[2], px, height);
	wlr_scene_node_set_position(&toplevel->focus_border[2]->node, -px, 0);
	wlr_scene_rect_set_size(toplevel->focus_border[3], px, height);
	wlr_scene_node_set_position(&toplevel->focus_border[3]->node, width, 0);
}

static void refresh_all_borders(struct shady_server *server) {
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->all_toplevels, all_link)
		shady_toplevel_refresh_border(toplevel);
}

static struct wlr_output *toplevel_output(struct shady_toplevel *toplevel) {
	struct shady_server *server = toplevel->server;
	if (toplevel->fullscreen && toplevel->xdg_toplevel->requested.fullscreen_output) {
		return toplevel->xdg_toplevel->requested.fullscreen_output;
	}
	struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
	double cx = toplevel->scene_tree->node.x + geo->x + geo->width * 0.5;
	double cy = toplevel->scene_tree->node.y + geo->y + geo->height * 0.5;
	struct wlr_output *output = wlr_output_layout_output_at(server->output_layout, cx, cy);
	if (output) return output;
	if (wl_list_empty(&server->outputs)) return NULL;
	struct shady_output *fallback = wl_container_of(server->outputs.next, fallback, link);
	return fallback->wlr_output;
}

static void save_restore_geometry(struct shady_toplevel *toplevel) {
	if (toplevel->restore_geometry_valid) return;
	struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
	int width = surface->current.width;
	int height = surface->current.height;
	if (width <= 0 || height <= 0) {
		struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
		width = geo->width;
		height = geo->height;
	}
	toplevel->restore_geometry = (struct wlr_box){
		.x = toplevel->scene_tree->node.x,
		.y = toplevel->scene_tree->node.y,
		.width = width,
		.height = height,
	};
	toplevel->restore_geometry_valid = width > 0 && height > 0;
}

static void apply_toplevel_state_values(struct shady_toplevel *toplevel,
		bool want_maximized, bool want_fullscreen) {
	struct wlr_xdg_toplevel *xdg = toplevel->xdg_toplevel;
	struct shady_server *server = toplevel->server;
	bool old_maximized = toplevel->maximized;
	bool old_fullscreen = toplevel->fullscreen;
	if (want_fullscreen) want_maximized = false;

	if ((want_fullscreen || want_maximized) && !toplevel->fullscreen && !toplevel->maximized) {
		save_restore_geometry(toplevel);
	}

	toplevel->fullscreen = want_fullscreen;
	toplevel->maximized = want_maximized;
	wlr_xdg_toplevel_set_fullscreen(xdg, want_fullscreen);
	wlr_xdg_toplevel_set_maximized(xdg, want_maximized);

	if (want_fullscreen || want_maximized) {
		struct wlr_output *output = toplevel_output(toplevel);
		if (output) {
			struct wlr_box box = {0};
			if (want_fullscreen) {
				wlr_output_layout_get_box(server->output_layout, output, &box);
			} else {
				shady_output_work_area(server, output, &box);
			}
			wlr_scene_node_set_position(&toplevel->scene_tree->node, box.x, box.y);
			wlr_xdg_toplevel_set_size(xdg, box.width, box.height);
			/* xdg_toplevel.configure_bounds was added in xdg-shell v4. */
			if (xdg->base->client->shell->version >= 4) {
				wlr_xdg_toplevel_set_bounds(xdg, box.width, box.height);
			}
		}
	} else if (toplevel->restore_geometry_valid) {
		struct wlr_box box = toplevel->restore_geometry;
		wlr_scene_node_set_position(&toplevel->scene_tree->node, box.x, box.y);
		wlr_xdg_toplevel_set_size(xdg, box.width, box.height);
		toplevel->restore_geometry_valid = false;
	}
	if (old_maximized != toplevel->maximized ||
			old_fullscreen != toplevel->fullscreen) {
		shady_event_emit_window(server, SHADY_EVENT_WINDOW_STATE_CHANGED, toplevel);
	}
	shady_toplevel_refresh_border(toplevel);
	shady_render_schedule_all_outputs(server);
}

static void apply_requested_toplevel_state(struct shady_toplevel *toplevel) {
	apply_toplevel_state_values(toplevel,
		toplevel->xdg_toplevel->requested.maximized,
		toplevel->xdg_toplevel->requested.fullscreen);
}

void shady_toplevel_set_maximized(struct shady_toplevel *toplevel, bool enabled) {
	if (!toplevel) return;
	toplevel->fullscreen_restore_maximized = false;
	apply_toplevel_state_values(toplevel, enabled, false);
}

void shady_toplevel_refresh_state(struct shady_toplevel *toplevel) {
	if (!toplevel || (!toplevel->maximized && !toplevel->fullscreen)) return;
	apply_toplevel_state_values(toplevel, toplevel->maximized, toplevel->fullscreen);
}

static void clamp_box_to_output(struct wlr_box *box, const struct wlr_box *area) {
	if (!box || !area || area->width <= 0 || area->height <= 0) return;
	int max_x = area->x + area->width - box->width;
	int max_y = area->y + area->height - box->height;
	if (max_x < area->x) max_x = area->x;
	if (max_y < area->y) max_y = area->y;
	if (box->x < area->x) box->x = area->x;
	if (box->y < area->y) box->y = area->y;
	if (box->x > max_x) box->x = max_x;
	if (box->y > max_y) box->y = max_y;
}

void shady_toplevel_recover_to_output(struct shady_toplevel *toplevel,
		struct wlr_output *output) {
	if (!toplevel || !output || !toplevel->scene_tree || !toplevel->xdg_toplevel)
		return;

	struct shady_server *server = toplevel->server;
	struct wlr_box area = {0};
	if (toplevel->fullscreen) {
		wlr_output_layout_get_box(server->output_layout, output, &area);
	} else {
		shady_output_work_area(server, output, &area);
	}
	if (area.width <= 0 || area.height <= 0) return;

	if (toplevel->fullscreen || toplevel->maximized) {
		wlr_scene_node_set_position(&toplevel->scene_tree->node, area.x, area.y);
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, area.width, area.height);
		if (toplevel->restore_geometry_valid) {
			clamp_box_to_output(&toplevel->restore_geometry, &area);
		}
	} else {
		struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
		struct wlr_box box = {
			.x = toplevel->scene_tree->node.x,
			.y = toplevel->scene_tree->node.y,
			.width = surface ? surface->current.width : 0,
			.height = surface ? surface->current.height : 0,
		};
		if (box.width <= 0 || box.height <= 0) {
			struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
			box.width = geo->width > 0 ? geo->width : 1;
			box.height = geo->height > 0 ? geo->height : 1;
		}
		clamp_box_to_output(&box, &area);
		wlr_scene_node_set_position(&toplevel->scene_tree->node, box.x, box.y);
	}
	shady_render_schedule_all_outputs(server);
}

void shady_toplevel_set_fullscreen(struct shady_toplevel *toplevel, bool enabled) {
	if (!toplevel) return;
	if (enabled && !toplevel->fullscreen) {
		toplevel->fullscreen_restore_maximized = toplevel->maximized;
	}
	bool restore_maximized = !enabled && toplevel->fullscreen_restore_maximized;
	if (!enabled) toplevel->fullscreen_restore_maximized = false;
	apply_toplevel_state_values(toplevel, restore_maximized, enabled);
}

void focus_toplevel(struct shady_toplevel *toplevel) {
	if (toplevel == NULL || !toplevel->scene_tree ||
			!toplevel->scene_tree->node.enabled) {
		return;
	}
	struct shady_server *server = toplevel->server;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
	if (prev_surface == surface) {
		shady_toplevel_refresh_border(toplevel);
		return;
	}
	if (prev_surface) {
		struct wlr_xdg_toplevel *prev_toplevel =
			wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
		if (prev_toplevel != NULL) {
			wlr_xdg_toplevel_set_activated(prev_toplevel, false);
		}
	}
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	wl_list_remove(&toplevel->link);
	wl_list_insert(&server->toplevels, &toplevel->link);
	wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
	refresh_all_borders(server);
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(seat, surface,
			keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	}
	shady_event_emit_window(server, SHADY_EVENT_WINDOW_FOCUSED, toplevel);
}

static void begin_interactive(struct shady_toplevel *toplevel,
		enum shady_cursor_mode mode, uint32_t edges) {
	if (toplevel->maximized || toplevel->fullscreen) return;
	struct shady_server *server = toplevel->server;

	server->grabbed_toplevel = toplevel;
	server->cursor_mode = mode;

	if (mode == SHADY_CURSOR_MOVE) {
		server->grab_x =
			server->cursor->x -
			toplevel->scene_tree->node.x;

		server->grab_y =
			server->cursor->y -
			toplevel->scene_tree->node.y;

	} else {
		struct wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;

		double border_x = (toplevel->scene_tree->node.x + geo_box->x) +
			((edges & WLR_EDGE_RIGHT) ? geo_box->width : 0);
		double border_y = (toplevel->scene_tree->node.y + geo_box->y) +
			((edges & WLR_EDGE_BOTTOM) ? geo_box->height : 0);
		server->grab_x = server->cursor->x - border_x;
		server->grab_y = server->cursor->y - border_y;

		server->grab_geobox = *geo_box;
		server->grab_geobox.x += toplevel->scene_tree->node.x;
		server->grab_geobox.y += toplevel->scene_tree->node.y;

		server->resize_edges = edges;
	}
}

static void place_toplevel_initial(struct shady_toplevel *toplevel) {
	struct shady_server *server = toplevel->server;
	if (wl_list_empty(&server->outputs)) return;
	struct shady_output *first = wl_container_of(server->outputs.next, first, link);
	if (!first->wlr_output) return;

	struct wlr_box area = {0};
	shady_output_work_area(server, first->wlr_output, &area);
	if (area.width <= 0 || area.height <= 0) return;

	struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
	struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
	int width = geo->width > 0 ? geo->width : surface->current.width;
	int height = geo->height > 0 ? geo->height : surface->current.height;
	if (width <= 0 || height <= 0) return;

	size_t mapped_before = 0;
	struct shady_toplevel *other;
	wl_list_for_each(other, &server->toplevels, link) {
		if (other != toplevel && other->mapped) mapped_before++;
	}
	int cascade = (int)(mapped_before % 6) * 28;
	int target_x = area.x + (area.width - width) / 2 + cascade;
	int target_y = area.y + (area.height - height) / 2 + cascade;
	if (target_x + width > area.x + area.width)
		target_x = area.x + area.width - width;
	if (target_y + height > area.y + area.height)
		target_y = area.y + area.height - height;
	if (target_x < area.x) target_x = area.x;
	if (target_y < area.y) target_y = area.y;
	wlr_scene_node_set_position(&toplevel->scene_tree->node,
		target_x - geo->x, target_y - geo->y);
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_toplevel *toplevel = wl_container_of(listener, toplevel, map);

	wl_list_insert(&toplevel->server->toplevels, &toplevel->link);
	toplevel->mapped = true;
	place_toplevel_initial(toplevel);
	apply_toplevel_opacity(toplevel);
	shady_toplevel_refresh_border(toplevel);
	if (toplevel->xdg_toplevel->requested.maximized ||
			toplevel->xdg_toplevel->requested.fullscreen) {
		apply_requested_toplevel_state(toplevel);
	}
	shady_modules_toplevel_map(toplevel);
	shady_event_emit_window(toplevel->server, SHADY_EVENT_WINDOW_MAPPED, toplevel);
	focus_toplevel(toplevel);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
	struct shady_server *server = toplevel->server;
	struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
	bool was_focused = server->seat->keyboard_state.focused_surface == surface ||
		server->toplevels.next == &toplevel->link;

	if (toplevel == server->grabbed_toplevel) {
		reset_cursor_mode(toplevel->server);
	}
	/*
	 * FPS carry state is a raw pointer into the toplevel object. A client may
	 * unmap/destroy itself (for example after typing "exit") while it is held.
	 * Clear it before renderer/physics can observe the stale object.
	 */
	shady_event_emit_window(server, SHADY_EVENT_WINDOW_UNMAPPED, toplevel);
	shady_modules_toplevel_unmap(toplevel);
	if (toplevel->focus_border_tree)
		wlr_scene_node_set_enabled(&toplevel->focus_border_tree->node, false);
	wl_list_remove(&toplevel->link);
	toplevel->mapped = false;

	if (was_focused && !wl_list_empty(&server->toplevels)) {
		struct shady_toplevel *next;
		wl_list_for_each(next, &server->toplevels, link) {
			if (next->scene_tree && next->scene_tree->node.enabled) {
				focus_toplevel(next);
				break;
			}
		}
	}
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_toplevel *toplevel = wl_container_of(listener, toplevel, commit);
	struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;

	if (toplevel->xdg_toplevel->base->initial_commit) {
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
		/* xdg_toplevel.wm_capabilities was added in xdg-shell v5. */
		if (toplevel->xdg_toplevel->base->client->shell->version >= 5) {
			wlr_xdg_toplevel_set_wm_capabilities(toplevel->xdg_toplevel,
				WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE |
				WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
		}
		/* Some clients (notably Electron) send state requests before their
		 * initial surface commit. Applying them earlier schedules an xdg
		 * configure before wlroots marks the surface initialized. */
		if (toplevel->xdg_toplevel->requested.maximized ||
				toplevel->xdg_toplevel->requested.fullscreen) {
			apply_requested_toplevel_state(toplevel);
		}
	}

	shady_modules_toplevel_commit(toplevel);
	apply_toplevel_opacity(toplevel);

	int width = surface->current.width;
	int height = surface->current.height;
	if (surface->mapped && width > 0 && height > 0 &&
			(width != toplevel->last_surface_width ||
			 height != toplevel->last_surface_height)) {
		toplevel->last_surface_width = width;
		toplevel->last_surface_height = height;
		shady_toplevel_refresh_border(toplevel);
		shady_event_emit_window(toplevel->server,
			SHADY_EVENT_WINDOW_RESIZED, toplevel);
	}
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);

	/* Be defensive if destroy arrives without the normal unmap path. */
	if (toplevel == toplevel->server->grabbed_toplevel) {
		reset_cursor_mode(toplevel->server);
	}
	shady_event_emit_window(toplevel->server, SHADY_EVENT_WINDOW_DESTROYED, toplevel);
	shady_modules_toplevel_destroy(toplevel);

	/* Normally xdg-surface unmap runs before destroy and removes this node from
	 * server->toplevels. Keep the destroy path safe even if that ordering is
	 * skipped. */
	if (toplevel->mapped) {
		wl_list_remove(&toplevel->link);
		toplevel->mapped = false;
	}

	wl_list_remove(&toplevel->map.link);
	wl_list_remove(&toplevel->unmap.link);
	wl_list_remove(&toplevel->commit.link);
	wl_list_remove(&toplevel->destroy.link);
	wl_list_remove(&toplevel->request_move.link);
	wl_list_remove(&toplevel->request_resize.link);
	wl_list_remove(&toplevel->request_maximize.link);
	wl_list_remove(&toplevel->request_fullscreen.link);

	wl_list_remove(&toplevel->all_link);
	shady_modules_toplevel_state_finish(toplevel);
	free(toplevel);
}

static void xdg_toplevel_request_move(
		struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
	begin_interactive(toplevel, SHADY_CURSOR_MOVE, 0);
}

static void xdg_toplevel_request_resize(
		struct wl_listener *listener, void *data) {
	struct wlr_xdg_toplevel_resize_event *event = data;
	struct shady_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
	begin_interactive(toplevel, SHADY_CURSOR_RESIZE, event->edges);
}

static void xdg_toplevel_request_maximize(
		struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_maximize);
	if (!toplevel->xdg_toplevel->base->initialized) return;
	apply_requested_toplevel_state(toplevel);
}

static void xdg_toplevel_request_fullscreen(
		struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_fullscreen);
	if (!toplevel->xdg_toplevel->base->initialized) return;
	apply_requested_toplevel_state(toplevel);
}

void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
	struct shady_server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *xdg_toplevel = data;

	struct shady_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	if (!toplevel) {
		return;
	}
	toplevel->server = server;
	toplevel->xdg_toplevel = xdg_toplevel;
	toplevel->shell_id = ++server->next_shell_window_id;
	if (toplevel->shell_id == 0) toplevel->shell_id = ++server->next_shell_window_id;
	if (!shady_modules_toplevel_state_init(toplevel)) {
		free(toplevel);
		return;
	}
	wl_list_insert(&server->all_toplevels, &toplevel->all_link);
	toplevel->scene_tree =
		wlr_scene_xdg_surface_create(toplevel->server->content_tree, xdg_toplevel->base);
	toplevel->scene_tree->node.data = toplevel;
	xdg_toplevel->base->data = toplevel->scene_tree;
	static const float border_color[4] = {0.10f, 0.65f, 1.0f, 1.0f};
	toplevel->focus_border_tree = wlr_scene_tree_create(toplevel->scene_tree);
	if (toplevel->focus_border_tree) {
		for (size_t i = 0; i < 4; i++)
			toplevel->focus_border[i] = wlr_scene_rect_create(
				toplevel->focus_border_tree, 1, 1, border_color);
		wlr_scene_node_set_enabled(&toplevel->focus_border_tree->node, false);
	}

	toplevel->map.notify = xdg_toplevel_map;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);
	toplevel->unmap.notify = xdg_toplevel_unmap;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);
	toplevel->commit.notify = xdg_toplevel_commit;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);

	toplevel->destroy.notify = xdg_toplevel_destroy;
	wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

	toplevel->request_move.notify = xdg_toplevel_request_move;
	wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);
	toplevel->request_resize.notify = xdg_toplevel_request_resize;
	wl_signal_add(&xdg_toplevel->events.request_resize, &toplevel->request_resize);
	toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
	wl_signal_add(&xdg_toplevel->events.request_maximize, &toplevel->request_maximize);
	toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen, &toplevel->request_fullscreen);
	shady_event_emit_window(server, SHADY_EVENT_WINDOW_CREATED, toplevel);
}

static void xdg_popup_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_popup *popup = wl_container_of(listener, popup, commit);

	if (popup->xdg_popup->base->initial_commit) {
		wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
	}
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_popup *popup = wl_container_of(listener, popup, destroy);

	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->destroy.link);
	wl_list_remove(&popup->link);

	free(popup);
}

void server_new_xdg_popup(struct wl_listener *listener, void *data) {
	struct shady_server *server = wl_container_of(listener, server, new_xdg_popup);
	struct wlr_xdg_popup *xdg_popup = data;

	struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
	if (parent == NULL) {
		/* Layer-shell helpers own their popup scene subtree. */
		return;
	}

	struct shady_popup *popup = calloc(1, sizeof(*popup));
	if (!popup) {
		return;
	}
	popup->server = server;
	popup->xdg_popup = xdg_popup;

	struct wlr_scene_tree *parent_tree = parent->data;
	popup->scene_tree = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);
	if (!popup->scene_tree) {
		free(popup);
		return;
	}
	xdg_popup->base->data = popup->scene_tree;
	wl_list_insert(&server->popups, &popup->link);

	popup->commit.notify = xdg_popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

	popup->destroy.notify = xdg_popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}
