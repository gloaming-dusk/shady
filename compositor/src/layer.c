#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "shady.h"
#include "modules/desktop/state.h"
#include "modules/workspace/workspace.h"
#include "render/render.h"

static struct wlr_output *fallback_output(struct shady_server *server) {
	if (wl_list_empty(&server->outputs)) {
		return NULL;
	}
	struct shady_output *output =
		wl_container_of(server->outputs.next, output, link);
	return output->wlr_output;
}

static uint32_t layer_exclusive_edge(const struct wlr_layer_surface_v1 *surface) {
	if (surface->current.exclusive_edge) return surface->current.exclusive_edge;
	uint32_t anchor = surface->current.anchor;
	bool top = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
	bool bottom = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
	bool left = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
	bool right = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
	if (top && !bottom) return ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
	if (bottom && !top) return ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
	if (left && !right) return ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
	if (right && !left) return ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
	return 0;
}

void shady_output_work_area(struct shady_server *server,
		struct wlr_output *output, struct wlr_box *box) {
	if (!box) return;
	*box = (struct wlr_box){0};
	if (!output) return;
	wlr_output_layout_get_box(server->output_layout, output, box);

	struct shady_desktop_state *state = shady_desktop_state(server);
	if (!state) return;
	struct shady_layer_surface *layer;
	wl_list_for_each(layer, &state->layer_surfaces, link) {
		struct wlr_layer_surface_v1 *surface = layer->layer_surface;
		if (!surface || surface->output != output || !surface->surface->mapped ||
				surface->current.exclusive_zone <= 0) continue;
		int zone = surface->current.exclusive_zone;
		switch (layer_exclusive_edge(surface)) {
		case ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP:
			zone += surface->current.margin.top;
			box->y += zone;
			box->height -= zone;
			break;
		case ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM:
			zone += surface->current.margin.bottom;
			box->height -= zone;
			break;
		case ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT:
			zone += surface->current.margin.left;
			box->x += zone;
			box->width -= zone;
			break;
		case ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT:
			zone += surface->current.margin.right;
			box->width -= zone;
			break;
		default:
			break;
		}
	}
	if (box->width < 1) box->width = 1;
	if (box->height < 1) box->height = 1;
}

static void refresh_maximized_for_output(struct shady_server *server,
		struct wlr_output *output) {
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (!toplevel->maximized || toplevel->fullscreen) continue;
		struct wlr_output *current = wlr_output_layout_output_at(server->output_layout,
			toplevel->scene_tree->node.x, toplevel->scene_tree->node.y);
		if (!current || current == output) shady_toplevel_refresh_state(toplevel);
	}
}

static void focus_layer_surface(struct shady_layer_surface *layer) {
	struct wlr_layer_surface_v1 *surface = layer->layer_surface;
	if (!surface || !surface->surface->mapped ||
			surface->current.keyboard_interactive ==
			ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) return;

	struct shady_server *server = layer->server;
	struct wlr_surface *old = server->seat->keyboard_state.focused_surface;
	if (old && old != surface->surface) {
		struct wlr_xdg_toplevel *xdg = wlr_xdg_toplevel_try_from_wlr_surface(old);
		if (xdg) wlr_xdg_toplevel_set_activated(xdg, false);
	}
	/* Focus even before the seat has a keyboard (one plugged in later, or a
	 * virtual keyboard): keys sent to an unfocused seat are dropped. */
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	wlr_seat_keyboard_notify_enter(server->seat, surface->surface,
		keyboard ? keyboard->keycodes : NULL, keyboard ? keyboard->num_keycodes : 0,
		keyboard ? &keyboard->modifiers : NULL);
}

static void restore_layer_keyboard_focus(struct shady_layer_surface *layer) {
	struct wlr_surface *surface = layer->layer_surface->surface;
	if (layer->server->seat->keyboard_state.focused_surface == surface) {
		wlr_seat_keyboard_clear_focus(layer->server->seat);
		shady_workspace_refocus_current(layer->server);
	}
}

static void configure_layer_surface(struct shady_layer_surface *layer) {
	struct wlr_layer_surface_v1 *surface = layer->layer_surface;
	struct shady_server *server = layer->server;

	if (!surface->output) {
		surface->output = fallback_output(server);
	}
	if (!surface->output || !layer->scene_layer) {
		return;
	}

	struct wlr_box full = {0};
	wlr_output_layout_get_box(server->output_layout, surface->output, &full);
	struct wlr_box usable = full;
	wlr_scene_layer_surface_v1_configure(layer->scene_layer, &full, &usable);
}

/* Output geometry changed (mode, scale, position, hotplug): give every
 * initialised layer surface its output's new box, so bars follow the
 * output instead of keeping the size from their last own commit. */
void shady_layers_arrange(struct shady_server *server) {
	struct shady_desktop_state *state = shady_desktop_state(server);
	if (!state) return;
	struct shady_layer_surface *layer;
	wl_list_for_each(layer, &state->layer_surfaces, link) {
		struct wlr_layer_surface_v1 *surface = layer->layer_surface;
		if (!surface || !surface->initialized) continue;
		if (surface->output && !wlr_output_layout_get(server->output_layout, surface->output))
			continue;
		configure_layer_surface(layer);
	}
	struct shady_output *output;
	wl_list_for_each(output, &server->outputs, link)
		refresh_maximized_for_output(server, output->wlr_output);
}

static void layer_surface_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_layer_surface *layer =
		wl_container_of(listener, layer, commit);
	struct wlr_layer_surface_v1 *surface = layer->layer_surface;
	/* A new buffer needs a frame: the spatial renderer draws on demand. */
	shady_render_schedule_all_outputs(layer->server);
	const uint32_t layout_fields =
		WLR_LAYER_SURFACE_V1_STATE_DESIRED_SIZE |
		WLR_LAYER_SURFACE_V1_STATE_ANCHOR |
		WLR_LAYER_SURFACE_V1_STATE_EXCLUSIVE_ZONE |
		WLR_LAYER_SURFACE_V1_STATE_MARGIN |
		WLR_LAYER_SURFACE_V1_STATE_LAYER |
		WLR_LAYER_SURFACE_V1_STATE_EXCLUSIVE_EDGE;

	bool layout_changed = surface->initial_commit ||
		(surface->current.committed & layout_fields) != 0;
	bool keyboard_changed = surface->initial_commit ||
		(surface->current.committed & WLR_LAYER_SURFACE_V1_STATE_KEYBOARD_INTERACTIVITY) != 0;
	if (layout_changed) {
		configure_layer_surface(layer);
		if (surface->surface->mapped && surface->output)
			refresh_maximized_for_output(layer->server, surface->output);
	}

	bool mapped = surface->surface->mapped;
	if (mapped != layer->mapped) {
		layer->mapped = mapped;
		if (surface->output)
			refresh_maximized_for_output(layer->server, surface->output);
		if (mapped) focus_layer_surface(layer);
		else restore_layer_keyboard_focus(layer);
	} else if (mapped && keyboard_changed) {
		if (surface->current.keyboard_interactive ==
				ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE)
			restore_layer_keyboard_focus(layer);
		else
			focus_layer_surface(layer);
	}
}

static void layer_surface_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_layer_surface *layer =
		wl_container_of(listener, layer, destroy);
	struct shady_server *server = layer->server;
	struct wlr_output *output = layer->layer_surface->output;
	restore_layer_keyboard_focus(layer);

	wl_list_remove(&layer->commit.link);
	wl_list_remove(&layer->destroy.link);
	wl_list_remove(&layer->link);
	free(layer);
	if (output) refresh_maximized_for_output(server, output);
}

void server_new_layer_surface(struct wl_listener *listener, void *data) {
	struct shady_desktop_state *state =
		wl_container_of(listener, state, new_layer_surface);
	struct shady_server *server = state->server;
	struct wlr_layer_surface_v1 *layer_surface = data;

	if (!layer_surface->output) {
		layer_surface->output = fallback_output(server);
	}

	struct shady_layer_surface *layer = calloc(1, sizeof(*layer));
	if (!layer) {
		wlr_layer_surface_v1_destroy(layer_surface);
		return;
	}

	layer->server = server;
	layer->layer_surface = layer_surface;
	layer->scene_layer = wlr_scene_layer_surface_v1_create(
		server->overlay_tree, layer_surface);
	if (!layer->scene_layer) {
		free(layer);
		wlr_layer_surface_v1_destroy(layer_surface);
		return;
	}

	layer->commit.notify = layer_surface_commit;
	wl_signal_add(&layer_surface->surface->events.commit, &layer->commit);

	layer->destroy.notify = layer_surface_destroy;
	wl_signal_add(&layer_surface->events.destroy, &layer->destroy);

	wl_list_insert(&shady_desktop_state(server)->layer_surfaces, &layer->link);
	/* The xdg/layer-shell initial commit initializes the protocol object.
	 * Configure from the surface commit listener after wlroots has processed
	 * that initial commit; configuring here would hit wlroots' initialized
	 * assertion for real layer-shell clients. */
}
