#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/util/log.h>

#include "shady.h"
#include "session_lock.h"
#include "render/render.h"
#include "modules/desktop/state.h"

static void maybe_send_locked(struct shady_server *server) {
	struct shady_desktop_state *desktop = shady_desktop_state(server);
	if (!desktop || !desktop->session_lock_pending_locked_event ||
			desktop->session_lock_pending_outputs != 0 ||
			!desktop->session_lock) {
		return;
	}
	desktop->session_lock_pending_locked_event = false;
	wlr_log(WLR_DEBUG,
		"session-lock: all locked frames presented; sending locked event");
	wlr_session_lock_v1_send_locked(desktop->session_lock);
}

static void clear_output_wait(struct shady_output *output) {
	if (!output || !output->lock_frame_pending) return;
	struct shady_desktop_state *desktop = shady_desktop_state(output->server);
	output->lock_frame_pending = false;
	output->lock_commit_seq = 0;
	if (desktop && desktop->session_lock_pending_outputs > 0) {
		desktop->session_lock_pending_outputs--;
	}
}

static void damage_lock_tree(struct shady_server *server) {
	struct shady_desktop_state *desktop = shady_desktop_state(server);
	if (!desktop || !desktop->session_lock_tree) return;
	/* Toggling the lock tree before scheduling frames forces wlroots to damage
	 * every output covered by it. The final visible state remains locked. */
	wlr_scene_node_set_enabled(&desktop->session_lock_tree->node, false);
	wlr_scene_node_set_enabled(&desktop->session_lock_tree->node, true);
}

static void mark_output_wait(struct shady_output *output) {
	if (!output || !output->wlr_output->enabled || output->lock_frame_pending) return;
	struct shady_desktop_state *desktop = shady_desktop_state(output->server);
	if (!desktop || !desktop->session_lock_pending_locked_event) return;
	output->lock_frame_pending = true;
	output->lock_commit_seq = 0;
	desktop->session_lock_pending_outputs++;

	/*
	 * Session locking is a security boundary: every enabled output must
	 * actually commit a lock frame before we acknowledge the lock.  Scene
	 * damage from adding the overlay can be coalesced with an already queued
	 * spatial frame, leaving an output with no new commit.  Force whole-output
	 * damage so the next scheduled frame always produces a presentation event.
	 */
	struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(
		output->server->scene, output->wlr_output);
	if (scene_output) {
		wlr_damage_ring_add_whole(&scene_output->damage_ring);
	}
}

static void reset_output_waits(struct shady_server *server) {
	struct shady_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		output->lock_frame_pending = false;
		output->lock_commit_seq = 0;
	}
	struct shady_desktop_state *desktop = shady_desktop_state(server);
	if (desktop) desktop->session_lock_pending_outputs = 0;
}

static void add_lock_background_for_output(struct shady_server *server,
		struct shady_output *output) {
	struct shady_desktop_state *desktop = shady_desktop_state(server);
	if (!desktop || !desktop->session_lock_tree || !output) return;
	int width = 0, height = 0;
	wlr_output_effective_resolution(output->wlr_output, &width, &height);
	struct wlr_box box = {0};
	wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
	float color[4] = {0.f, 0.f, 0.f, 1.f};
	struct wlr_scene_rect *rect =
		wlr_scene_rect_create(desktop->session_lock_tree, width, height, color);
	if (rect) {
		wlr_scene_node_set_position(&rect->node, box.x, box.y);
	}
}

static void add_lock_backgrounds(struct shady_server *server) {
	struct shady_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		add_lock_background_for_output(server, output);
	}
}

void shady_session_lock_output_added(struct shady_output *output) {
	if (!output) return;
	struct shady_server *server = output->server;
	struct shady_desktop_state *desktop = shady_desktop_state(server);
	if (!desktop || !desktop->session_locked || !desktop->session_lock_tree) return;

	add_lock_background_for_output(server, output);
	if (desktop->session_lock_pending_locked_event) {
		mark_output_wait(output);
		damage_lock_tree(server);
	}
	shady_render_schedule_output(output);
}

void shady_session_lock_output_removed(struct shady_output *output) {
	if (!output) return;
	struct shady_server *server = output->server;
	struct shady_desktop_state *desktop = shady_desktop_state(server);
	if (!desktop || !desktop->session_lock_pending_locked_event) return;
	clear_output_wait(output);
	maybe_send_locked(server);
}

void shady_session_lock_output_state_changed(struct shady_output *output) {
	if (!output) return;
	struct shady_server *server = output->server;
	struct shady_desktop_state *desktop = shady_desktop_state(server);
	if (!desktop || !desktop->session_locked) return;

	if (!output->wlr_output->enabled) {
		/* A disabled output no longer displays pixels, so it must not keep the
		 * initial lock acknowledgement waiting for a presentation that can never
		 * happen. */
		if (desktop->session_lock_pending_locked_event) {
			clear_output_wait(output);
			maybe_send_locked(server);
		}
		return;
	}

	/* If an output becomes active while the initial lock handshake is still in
	 * flight, require a newly damaged lock frame from that output as well. */
	if (desktop->session_lock_pending_locked_event) {
		mark_output_wait(output);
		damage_lock_tree(server);
	}
	shady_render_schedule_output(output);
}

void shady_session_lock_output_committed(struct shady_output *output,
		uint32_t previous_commit_seq) {
	if (!output || !output->lock_frame_pending || output->lock_commit_seq != 0) return;
	struct shady_desktop_state *desktop = shady_desktop_state(output->server);
	if (!desktop || !desktop->session_lock_pending_locked_event) return;
	if (output->wlr_output->commit_seq != previous_commit_seq) {
		output->lock_commit_seq = output->wlr_output->commit_seq;
	}
}

void shady_session_lock_output_presented(struct shady_output *output,
		const struct wlr_output_event_present *event) {
	if (!output || !event || !output->lock_frame_pending ||
			output->lock_commit_seq == 0 ||
			event->commit_seq != output->lock_commit_seq) {
		return;
	}

	if (!event->presented) {
		output->lock_commit_seq = 0;
		shady_render_schedule_output(output);
		return;
	}

	struct shady_server *server = output->server;
	wlr_log(WLR_DEBUG,
		"session-lock: locked frame presented output=%s commit_seq=%u",
		output->wlr_output->name ? output->wlr_output->name : "<unnamed>",
		event->commit_seq);
	clear_output_wait(output);
	maybe_send_locked(server);
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
	struct shady_desktop_state *desktop = shady_desktop_state(server);
	struct wlr_session_lock_surface_v1 *lock_surface = data;

	struct shady_lock_surface *surface = calloc(1, sizeof(*surface));
	if (!surface) {
		return;
	}
	surface->lock = lock;
	surface->lock_surface = lock_surface;
	surface->tree = wlr_scene_tree_create(desktop->session_lock_tree);
	if (!surface->tree) {
		free(surface);
		return;
	}

	int width = 0, height = 0;
	wlr_output_effective_resolution(lock_surface->output, &width, &height);
	struct wlr_box box = {0};
	wlr_output_layout_get_box(server->output_layout, lock_surface->output, &box);
	wlr_scene_node_set_position(&surface->tree->node, box.x, box.y);
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
	struct shady_desktop_state *desktop = shady_desktop_state(server);
	lock->unlocked = true;
	desktop->session_locked = false;
	desktop->session_lock = NULL;
	desktop->session_lock_pending_locked_event = false;
	reset_output_waits(server);
	if (desktop->session_lock_tree) {
		wlr_scene_node_destroy(&desktop->session_lock_tree->node);
		desktop->session_lock_tree = NULL;
	}
	shady_render_schedule_all_outputs(server);
}

static void lock_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_session_lock *lock =
		wl_container_of(listener, lock, destroy);
	struct shady_server *server = lock->server;
	struct shady_desktop_state *desktop = shady_desktop_state(server);

	wl_list_remove(&lock->new_surface.link);
	wl_list_remove(&lock->unlock.link);
	wl_list_remove(&lock->destroy.link);

	desktop->session_lock = NULL;
	desktop->session_lock_pending_locked_event = false;
	reset_output_waits(server);
	free(lock);
}

void server_new_session_lock(struct wl_listener *listener, void *data) {
	struct shady_desktop_state *desktop =
		wl_container_of(listener, desktop, new_session_lock);
	struct shady_server *server = desktop->server;
	struct wlr_session_lock_v1 *wlr_lock = data;

	if (desktop->session_locked) {
		wlr_session_lock_v1_destroy(wlr_lock);
		return;
	}

	if (desktop->session_lock_tree) {
		wlr_scene_node_destroy(&desktop->session_lock_tree->node);
	}
	desktop->session_lock_tree = wlr_scene_tree_create(server->overlay_tree);
	if (!desktop->session_lock_tree) {
		wlr_session_lock_v1_destroy(wlr_lock);
		return;
	}
	add_lock_backgrounds(server);

	struct shady_session_lock *lock = calloc(1, sizeof(*lock));
	if (!lock) {
		wlr_scene_node_destroy(&desktop->session_lock_tree->node);
		desktop->session_lock_tree = NULL;
		wlr_session_lock_v1_destroy(wlr_lock);
		return;
	}

	lock->server = server;
	lock->lock = wlr_lock;
	desktop->session_lock = wlr_lock;
	desktop->session_locked = true;
	desktop->session_lock_pending_locked_event = true;
	desktop->session_lock_pending_outputs = 0;

	lock->new_surface.notify = lock_new_surface;
	wl_signal_add(&wlr_lock->events.new_surface, &lock->new_surface);
	lock->unlock.notify = lock_unlock;
	wl_signal_add(&wlr_lock->events.unlock, &lock->unlock);
	lock->destroy.notify = lock_destroy;
	wl_signal_add(&wlr_lock->events.destroy, &lock->destroy);

	wlr_seat_pointer_clear_focus(server->seat);

	struct shady_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		mark_output_wait(output);
	}
	damage_lock_tree(server);
	shady_render_schedule_all_outputs(server);
	maybe_send_locked(server);
}
