#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_session_lock_v1.h>

#include "shady.h"
#include "render/render.h"

static void add_lock_backgrounds(struct shady_server *server) {
	struct shady_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		int width = 0, height = 0;
		wlr_output_effective_resolution(output->wlr_output, &width, &height);
		double lx = 0, ly = 0;
		wlr_output_layout_output_coords(server->output_layout,
			output->wlr_output, &lx, &ly);
		float color[4] = {0.f, 0.f, 0.f, 1.f};
		struct wlr_scene_rect *rect =
			wlr_scene_rect_create(server->session_lock_tree,
				width, height, color);
		if (rect) {
			wlr_scene_node_set_position(&rect->node, (int)lx, (int)ly);
		}
	}
}

static void lock_surface_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_lock_surface *surface =
		wl_container_of(listener, surface, destroy);
	wl_list_remove(&surface->destroy.link);
	free(surface);
}

static void lock_new_surface(struct wl_listener *listener, void *data) {
	struct shady_session_lock *lock =
		wl_container_of(listener, lock, new_surface);
	struct shady_server *server = lock->server;
	struct wlr_session_lock_surface_v1 *lock_surface = data;

	struct shady_lock_surface *surface = calloc(1, sizeof(*surface));
	if (!surface) {
		return;
	}
	surface->lock = lock;
	surface->lock_surface = lock_surface;
	surface->tree = wlr_scene_tree_create(server->session_lock_tree);
	if (!surface->tree) {
		free(surface);
		return;
	}

	int width = 0, height = 0;
	wlr_output_effective_resolution(lock_surface->output, &width, &height);
	double lx = 0, ly = 0;
	wlr_output_layout_output_coords(server->output_layout,
		lock_surface->output, &lx, &ly);
	wlr_scene_node_set_position(&surface->tree->node, (int)lx, (int)ly);
	wlr_scene_surface_create(surface->tree, lock_surface->surface);
	wlr_session_lock_surface_v1_configure(lock_surface, width, height);

	surface->destroy.notify = lock_surface_destroy;
	wl_signal_add(&lock_surface->events.destroy, &surface->destroy);

	wlr_seat_pointer_clear_focus(server->seat);
	wlr_seat_keyboard_notify_enter(server->seat, lock_surface->surface,
		NULL, 0, NULL);
	shady_render_schedule_all_outputs(server);
}

static void lock_unlock(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_session_lock *lock =
		wl_container_of(listener, lock, unlock);
	struct shady_server *server = lock->server;
	lock->unlocked = true;
	server->session_locked = false;
	server->session_lock = NULL;
	if (server->session_lock_tree) {
		wlr_scene_node_destroy(&server->session_lock_tree->node);
		server->session_lock_tree = NULL;
	}
	shady_render_schedule_all_outputs(server);
}

static void lock_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_session_lock *lock =
		wl_container_of(listener, lock, destroy);
	struct shady_server *server = lock->server;

	wl_list_remove(&lock->new_surface.link);
	wl_list_remove(&lock->unlock.link);
	wl_list_remove(&lock->destroy.link);

	/*
	 * If the client disappears without unlocking, keep the black lock tree
	 * and locked state. This avoids exposing the previous desktop.
	 */
	if (lock->unlocked) {
		server->session_lock = NULL;
	}
	free(lock);
}

void server_new_session_lock(struct wl_listener *listener, void *data) {
	struct shady_server *server =
		wl_container_of(listener, server, new_session_lock);
	struct wlr_session_lock_v1 *wlr_lock = data;

	if (server->session_locked) {
		wlr_session_lock_v1_destroy(wlr_lock);
		return;
	}

	if (server->session_lock_tree) {
		wlr_scene_node_destroy(&server->session_lock_tree->node);
	}
	server->session_lock_tree = wlr_scene_tree_create(&server->scene->tree);
	if (!server->session_lock_tree) {
		wlr_session_lock_v1_destroy(wlr_lock);
		return;
	}
	add_lock_backgrounds(server);

	struct shady_session_lock *lock = calloc(1, sizeof(*lock));
	if (!lock) {
		wlr_scene_node_destroy(&server->session_lock_tree->node);
		server->session_lock_tree = NULL;
		wlr_session_lock_v1_destroy(wlr_lock);
		return;
	}

	lock->server = server;
	lock->lock = wlr_lock;
	server->session_lock = wlr_lock;
	server->session_locked = true;

	lock->new_surface.notify = lock_new_surface;
	wl_signal_add(&wlr_lock->events.new_surface, &lock->new_surface);
	lock->unlock.notify = lock_unlock;
	wl_signal_add(&wlr_lock->events.unlock, &lock->unlock);
	lock->destroy.notify = lock_destroy;
	wl_signal_add(&wlr_lock->events.destroy, &lock->destroy);

	wlr_seat_pointer_clear_focus(server->seat);
	wlr_session_lock_v1_send_locked(wlr_lock);
	shady_render_schedule_all_outputs(server);
}
