/*
 * Screenshots taken by the compositor itself: the next frame an output
 * draws is read back and written as a PNG, so capturing needs no grim and
 * shows the 3D scene exactly as drawn. Requested over IPC (`screenshot`),
 * from Lua (`shady.screenshot()`) and from shady-shell (`shell.screenshot()`).
 */
#define _POSIX_C_SOURCE 200809L
#include "capture.h"

#include <cairo.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "shady.h"
#include "ipc/values.h"
#include "render/render.h"

/* drm_fourcc.h's XRGB8888 ('XR24'), without depending on libdrm's headers. */
#define CAPTURE_FORMAT_XRGB8888 0x34325258u

static struct {
	char path[4096];
	bool pending; /* requested, maybe still waiting for its delay */
	bool armed;   /* the next frame drawn is captured */
	struct wl_event_source *timer;
} capture;

/* Owner tag for the published value. */
static const char capture_owner;

static void default_path(char *out, size_t size) {
	const char *dir = getenv("XDG_PICTURES_DIR");
	char fallback[4096];
	if (!dir || !*dir) {
		const char *home = getenv("HOME");
		snprintf(fallback, sizeof(fallback), "%.4000s/Pictures", home && *home ? home : "/tmp");
		dir = fallback;
	}
	mkdir(dir, 0755);
	char stamp[32];
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
	snprintf(out, size, "%.4000s/shady-%s.png", dir, stamp);
}

static int capture_timer_fire(void *data) {
	struct shady_server *server = data;
	wl_event_source_remove(capture.timer);
	capture.timer = NULL;
	capture.armed = true;
	shady_render_schedule_all_outputs(server);
	return 0;
}

bool shady_capture_request(struct shady_server *server, const char *path,
		int delay_ms, char *out_path, size_t out_size) {
	if (capture.pending) return false;
	if (path && *path) snprintf(capture.path, sizeof(capture.path), "%s", path);
	else default_path(capture.path, sizeof(capture.path));
	if (out_path) snprintf(out_path, out_size, "%s", capture.path);
	capture.pending = true;
	if (delay_ms > 0) {
		capture.timer = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->wl_display), capture_timer_fire, server);
		if (capture.timer && wl_event_source_timer_update(capture.timer, delay_ms) == 0)
			return true;
		if (capture.timer) wl_event_source_remove(capture.timer);
		capture.timer = NULL;
	}
	capture.armed = true;
	shady_render_schedule_all_outputs(server);
	return true;
}

static bool write_png(struct wlr_renderer *renderer, struct wlr_buffer *buffer,
		const char *path) {
	struct wlr_texture *texture = wlr_texture_from_buffer(renderer, buffer);
	if (!texture) return false;
	int width = (int)texture->width, height = (int)texture->height;
	int stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, width);
	unsigned char *pixels = malloc((size_t)stride * (size_t)height);
	bool ok = pixels && wlr_texture_read_pixels(texture,
		&(struct wlr_texture_read_pixels_options){
			.data = pixels,
			.format = CAPTURE_FORMAT_XRGB8888, /* cairo RGB24 in memory */
			.stride = (uint32_t)stride,
		});
	wlr_texture_destroy(texture);
	if (ok) {
		cairo_surface_t *image = cairo_image_surface_create_for_data(
			pixels, CAIRO_FORMAT_RGB24, width, height, stride);
		/* Write beside the target and rename, so nothing ever sees half a file. */
		char partial[4200];
		snprintf(partial, sizeof(partial), "%s.part", path);
		ok = cairo_surface_status(image) == CAIRO_STATUS_SUCCESS &&
			cairo_surface_write_to_png(image, partial) == CAIRO_STATUS_SUCCESS &&
			rename(partial, path) == 0;
		if (!ok) remove(partial);
		cairo_surface_destroy(image);
	}
	free(pixels);
	return ok;
}

void shady_capture_output_buffer(struct shady_server *server,
		struct wlr_buffer *buffer) {
	if (!capture.armed || !buffer || !server->renderer) return;
	capture.armed = false;
	capture.pending = false;
	if (write_png(server->renderer, buffer, capture.path)) {
		wlr_log(WLR_INFO, "screenshot saved to %s", capture.path);
		/* Shells show it; the value changes even for the same second. */
		shady_values_set(server, (void *)&capture_owner, "screenshot.saved", capture.path);
	} else {
		wlr_log(WLR_ERROR, "screenshot to %s failed: %s", capture.path, strerror(errno));
		shady_values_set(server, (void *)&capture_owner, "screenshot.failed", capture.path);
	}
}

bool shady_capture_scene_output_commit(struct shady_server *server,
		struct wlr_scene_output *scene_output) {
	if (!capture.armed)
		return wlr_scene_output_commit(scene_output, NULL);
	/* Redraw all of it: an undamaged output would not render a buffer. */
	wlr_damage_ring_add_whole(&scene_output->damage_ring);
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	bool ok = wlr_scene_output_build_state(scene_output, &state, NULL);
	if (ok && (state.committed & WLR_OUTPUT_STATE_BUFFER))
		shady_capture_output_buffer(server, state.buffer);
	ok = ok && wlr_output_commit_state(scene_output->output, &state);
	wlr_output_state_finish(&state);
	return ok;
}

void shady_capture_finish(void) {
	if (capture.timer) wl_event_source_remove(capture.timer);
	capture.timer = NULL;
	capture.pending = capture.armed = false;
}
