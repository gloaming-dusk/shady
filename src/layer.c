#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>

#include "shady.h"
#include "modules/desktop/state.h"

static struct wlr_output *fallback_output(struct shady_server *server) {
	if (wl_list_empty(&server->outputs)) {
		return NULL;
	}
	struct shady_output *output =
		wl_container_of(server->outputs.next, output, link);
	return output->wlr_output;
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

static void layer_surface_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_layer_surface *layer =
		wl_container_of(listener, layer, commit);
	configure_layer_surface(layer);
}

static void layer_surface_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_layer_surface *layer =
		wl_container_of(listener, layer, destroy);

	wl_list_remove(&layer->commit.link);
	wl_list_remove(&layer->destroy.link);
	wl_list_remove(&layer->link);
	free(layer);
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
	configure_layer_surface(layer);
}
