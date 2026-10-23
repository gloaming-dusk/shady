/* Adapted from wlroots 0.20.2 TinyWL (CC0). See LICENSES/tinywl-CC0.txt. */
#include <math.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "shady.h"
#include "render/render.h"
#include "session_lock.h"

static void output_frame(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_output *output = wl_container_of(listener, output, frame);
	output->frame_callbacks++;
	output->frame_scheduled = false;
	uint32_t previous_commit_seq = output->wlr_output->commit_seq;
	shady_render_output_frame(output);
	shady_session_lock_output_committed(output, previous_commit_seq);
}

/* Commits of any client surface (toplevels, popups, subsurfaces, layer and
 * cursor surfaces) ask for a frame; the spatial renderer draws only then. */
struct surface_watch {
	struct shady_server *server;
	struct wl_listener commit;
	struct wl_listener destroy;
};

static void surface_watch_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct surface_watch *watch = wl_container_of(listener, watch, commit);
	shady_render_schedule_all_outputs(watch->server);
}

static void surface_watch_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct surface_watch *watch = wl_container_of(listener, watch, destroy);
	wl_list_remove(&watch->commit.link);
	wl_list_remove(&watch->destroy.link);
	free(watch);
}

void server_new_surface(struct wl_listener *listener, void *data) {
	struct shady_server *server = wl_container_of(listener, server, new_surface);
	struct wlr_surface *surface = data;
	struct surface_watch *watch = calloc(1, sizeof(*watch));
	if (!watch) return;
	watch->server = server;
	watch->commit.notify = surface_watch_commit;
	wl_signal_add(&surface->events.commit, &watch->commit);
	watch->destroy.notify = surface_watch_destroy;
	wl_signal_add(&surface->events.destroy, &watch->destroy);
}

static void output_present(struct wl_listener *listener, void *data) {
	struct shady_output *output = wl_container_of(listener, output, present);
	shady_session_lock_output_presented(output, data);
}

static void output_request_state(struct wl_listener *listener, void *data) {
	struct shady_output *output = wl_container_of(listener, output, request_state);
	const struct wlr_output_event_request_state *event = data;
	if (wlr_output_commit_state(output->wlr_output, event->state)) {
		shady_session_lock_output_state_changed(output);
		shady_render_schedule_output(output);
	}
}

static bool point_on_managed_output(struct shady_server *server,
		double x, double y) {
	struct shady_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		struct wlr_box box = {0};
		wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
		if (box.width <= 0 || box.height <= 0) continue;
		if (x >= box.x && x < box.x + box.width &&
				y >= box.y && y < box.y + box.height) return true;
	}
	return false;
}

void shady_recover_toplevels_to_outputs(struct shady_server *server) {
	if (wl_list_empty(&server->outputs)) return;
	struct shady_output *fallback = NULL;
	struct shady_output *candidate;
	wl_list_for_each(candidate, &server->outputs, link) {
		struct wlr_box box = {0};
		wlr_output_layout_get_box(server->output_layout, candidate->wlr_output, &box);
		if (box.width > 0 && box.height > 0) {
			fallback = candidate;
			break;
		}
	}
	if (!fallback) return;

	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->all_toplevels, all_link) {
		if (!toplevel->scene_tree || !toplevel->xdg_toplevel ||
				!toplevel->xdg_toplevel->base->surface->mapped) continue;
		struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
		double cx = toplevel->scene_tree->node.x + geo->width * 0.5;
		double cy = toplevel->scene_tree->node.y + geo->height * 0.5;
		if (point_on_managed_output(server, cx, cy)) continue;
		wlr_log(WLR_INFO, "recovering window onto active output");
		shady_toplevel_recover_to_output(toplevel, fallback->wlr_output);
	}
}

static void output_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_output *output = wl_container_of(listener, output, destroy);
	struct shady_server *server = output->server;

	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->present.link);
	wl_list_remove(&output->request_state.link);
	shady_session_lock_output_removed(output);
#if SHADY_HAS_SPATIAL
	if (server->config.spatial_mode) shady_render_output_destroy(output);
#endif
	shady_event_emit_output(server, SHADY_EVENT_OUTPUT_REMOVED, output);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	const char *render_stats = getenv("SHADY_RENDER_STATS");
	if (render_stats && *render_stats && strcmp(render_stats, "0") != 0) {
		wlr_log(WLR_INFO,
			"render-stats output=%s requests=%" PRIu64 " coalesced=%" PRIu64 " frames=%" PRIu64,
			output->wlr_output->name ? output->wlr_output->name : "<unnamed>",
			output->frame_schedule_requests,
			output->frame_schedule_coalesced,
			output->frame_callbacks);
		if (output->profile_samples > 0) {
			wlr_log(WLR_INFO,
				"render-profile output=%s samples=%" PRIu64
				" avg_us=%.1f effects_us=%.1f windows_us=%.1f overlay_us=%.1f submit_us=%.1f",
				output->wlr_output->name ? output->wlr_output->name : "<unnamed>",
				output->profile_samples,
				(double)output->profile_frame_ns / output->profile_samples / 1000.0,
				(double)output->profile_effects_ns / output->profile_samples / 1000.0,
				(double)output->profile_windows_ns / output->profile_samples / 1000.0,
				(double)output->profile_overlay_ns / output->profile_samples / 1000.0,
				(double)output->profile_submit_ns / output->profile_samples / 1000.0);
			wlr_log(WLR_INFO,
				"render-continuous output=%s camera=%" PRIu64
				" motion=%" PRIu64 " effect=%" PRIu64 " physics=%" PRIu64
				" close=%" PRIu64 " snapshot=%" PRIu64,
				output->wlr_output->name ? output->wlr_output->name : "<unnamed>",
				output->continuous_camera_frames,
				output->continuous_motion_frames,
				output->continuous_effect_frames,
				output->continuous_physics_frames,
				output->continuous_close_frames,
				output->continuous_snapshot_frames);
		}
	}
	shady_recover_toplevels_to_outputs(server);
	shady_output_manager_publish(server);
	free(output);
}

/* The scale an output gets: SHADY_OUTPUT_SCALE, then output_scale, then one
 * guessed from the panel's physical size the way niri and GNOME do it: aim
 * for ~135 DPI on laptop-sized panels and ~110 on monitors of 20" and up, in
 * quarter steps, so the desktop has the same room as there (a 14" 2880x1800
 * panel gets 1.75). Outputs without a physical size (nested, headless) stay
 * at 1. */
static float output_scale_for(struct shady_server *server, struct wlr_output *output,
		const struct wlr_output_mode *mode) {
	const char *env = getenv("SHADY_OUTPUT_SCALE");
	if (env && *env) {
		float scale = strtof(env, NULL);
		if (scale >= 0.5f && scale <= 4.f) return scale;
	}
	if (server->config.output_scale >= 0.5f) return server->config.output_scale;
	int width = mode ? mode->width : output->width;
	int height = mode ? mode->height : output->height;
	if (output->phys_width <= 0 || output->phys_height <= 0 ||
			width <= 0 || height <= 0)
		return 1.f;
	double diag_inches = sqrt((double)output->phys_width * output->phys_width +
		(double)output->phys_height * output->phys_height) / 25.4;
	double target_dpi = diag_inches < 20.0 ? 135.0 : 110.0;
	double dpi = sqrt((double)width * width + (double)height * height) / diag_inches;
	double perfect = dpi / target_dpi;
	float best = 1.f;
	for (int step = 4; step <= 16; step++) {
		float scale = step / 4.f;
		/* Keep at least 800x480 logical pixels. */
		if (width / scale < 800.f || height / scale < 480.f) break;
		if (fabs(scale - perfect) < fabs(best - perfect)) best = scale;
	}
	return best;
}

void server_new_output(struct wl_listener *listener, void *data) {
	struct shady_server *server =
		wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	if (!wlr_output_init_render(wlr_output, server->allocator, server->renderer)) {
		wlr_log(WLR_ERROR, "output %s: failed to initialize rendering",
			wlr_output->name ? wlr_output->name : "<unnamed>");
		return;
	}

	struct wlr_output_state state;
	const char *software_cursors = getenv("SHADY_SOFTWARE_CURSORS");
	if (software_cursors && strcmp(software_cursors, "1") == 0) {
		wlr_output_lock_software_cursors(wlr_output, true);
		wlr_log(WLR_INFO, "output %s: using software cursors",
			wlr_output->name ? wlr_output->name : "<unnamed>");
	}
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);

	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode != NULL) {
		wlr_output_state_set_mode(&state, mode);
	}
	wlr_output_state_set_scale(&state, output_scale_for(server, wlr_output, mode));

	if (!wlr_output_commit_state(wlr_output, &state)) {
		wlr_log(WLR_ERROR, "output %s: failed to enable/commit preferred mode",
			wlr_output->name ? wlr_output->name : "<unnamed>");
		wlr_output_state_finish(&state);
		return;
	}
	wlr_output_state_finish(&state);
	wlr_log(WLR_INFO, "output %s enabled: %dx%d@%.3fHz scale=%.2f",
		wlr_output->name ? wlr_output->name : "<unnamed>",
		wlr_output->width, wlr_output->height,
		wlr_output->refresh > 0 ? wlr_output->refresh / 1000.0 : 0.0,
		wlr_output->scale);

	struct shady_output *output = calloc(1, sizeof(*output));
	output->wlr_output = wlr_output;
	output->server = server;
	output->render_requested = true;

	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

	output->present.notify = output_present;
	wl_signal_add(&wlr_output->events.present, &output->present);

	output->request_state.notify = output_request_state;
	wl_signal_add(&wlr_output->events.request_state, &output->request_state);

	output->destroy.notify = output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);

	wl_list_insert(&server->outputs, &output->link);

	struct wlr_output_layout_output *l_output = wlr_output_layout_add_auto(
		server->output_layout, wlr_output);
	struct wlr_scene_output *scene_output =
		wlr_scene_output_create(server->scene, wlr_output);
	wlr_scene_output_layout_add_output(server->scene_layout, l_output, scene_output);
	shady_session_lock_output_added(output);
	shady_output_manager_publish(server);
	shady_event_emit_output(server, SHADY_EVENT_OUTPUT_ADDED, output);
}
