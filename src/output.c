/* Adapted from wlroots 0.20.2 TinyWL (CC0). See LICENSES/tinywl-CC0.txt. */
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

static void output_frame(struct wl_listener *listener, void *data) {
	(void)data;
	struct shady_output *output = wl_container_of(listener, output, frame);
	output->frame_callbacks++;
	output->frame_scheduled = false;
	shady_render_output_frame(output);
}

static void output_request_state(struct wl_listener *listener, void *data) {
	struct shady_output *output = wl_container_of(listener, output, request_state);
	const struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(output->wlr_output, event->state);
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
		double cx = toplevel->scene_tree->node.x + geo->x + geo->width * 0.5;
		double cy = toplevel->scene_tree->node.y + geo->y + geo->height * 0.5;
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
	wl_list_remove(&output->request_state.link);
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
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);

	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode != NULL) {
		wlr_output_state_set_mode(&state, mode);
	}

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

	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

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
	shady_output_manager_publish(server);
	shady_event_emit_output(server, SHADY_EVENT_OUTPUT_ADDED, output);
}
