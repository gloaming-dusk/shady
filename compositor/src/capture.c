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
#include <math.h>
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
#include <wlr/types/wlr_output_layout.h>
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
	char output[128]; /* "": the first output to draw */
	bool has_region;
	double x, y, width, height;
	int max_width, max_height;
	void (*done)(const struct shady_capture_result *result, void *data);
	void *data;
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

/* An output that draws nothing (off, or idle) never serves a capture; it
 * fails after this long instead of staying pending. */
#define CAPTURE_TIMEOUT_MS 5000

static void capture_failed(struct shady_server *server) {
	capture.armed = capture.pending = false;
	wlr_log(WLR_ERROR, "screenshot to %s failed: no frame was drawn", capture.path);
	shady_values_set(server, (void *)&capture_owner, "screenshot.failed", capture.path);
	void (*done)(const struct shady_capture_result *, void *) = capture.done;
	void *data = capture.data;
	capture.done = NULL;
	capture.data = NULL;
	if (done) done(&(struct shady_capture_result){ .ok = false, .path = capture.path }, data);
}

/* The delay is over: capture the next frame, for a while. Or that while
 * is over. */
static int capture_timer_fire(void *data) {
	struct shady_server *server = data;
	if (capture.armed) {
		wl_event_source_remove(capture.timer);
		capture.timer = NULL;
		capture_failed(server);
		return 0;
	}
	capture.armed = true;
	wl_event_source_timer_update(capture.timer, CAPTURE_TIMEOUT_MS);
	shady_render_schedule_all_outputs(server);
	return 0;
}

bool shady_capture_request(struct shady_server *server, const char *path,
		int delay_ms, char *out_path, size_t out_size) {
	struct shady_capture_options options = { .path = path, .delay_ms = delay_ms };
	return shady_capture_request_with(server, &options, out_path, out_size, NULL);
}

static bool fail_request(const char **error, const char *message) {
	if (error) *error = message;
	return false;
}

bool shady_capture_request_with(struct shady_server *server,
		const struct shady_capture_options *options, char *out_path, size_t out_size,
		const char **error) {
	if (capture.pending) return fail_request(error, "a screenshot is already pending");
	const char *output = options->output && *options->output ? options->output : NULL;
	if (options->has_region && (options->width <= 0 || options->height <= 0))
		return fail_request(error, "the region is empty");
	/* A region lies on the output under its middle. */
	if (!output && options->has_region) {
		struct wlr_output *under = wlr_output_layout_output_at(server->output_layout,
			options->x + options->width * 0.5, options->y + options->height * 0.5);
		if (!under) return fail_request(error, "the region is on no output");
		output = under->name;
	}
	if (output) {
		bool found = false;
		struct shady_output *o;
		wl_list_for_each(o, &server->outputs, link)
			found |= o->wlr_output && o->wlr_output->enabled && !strcmp(o->wlr_output->name, output);
		if (!found) return fail_request(error, "no such output");
	}
	snprintf(capture.output, sizeof(capture.output), "%s", output ? output : "");
	capture.has_region = options->has_region;
	capture.x = options->x;
	capture.y = options->y;
	capture.width = options->width;
	capture.height = options->height;
	capture.max_width = options->max_width > 0 ? options->max_width : 0;
	capture.max_height = options->max_height > 0 ? options->max_height : 0;
	capture.done = options->done;
	capture.data = options->data;
	int delay_ms = options->delay_ms;
	const char *path = options->path;
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
	capture.timer = wl_event_loop_add_timer(
		wl_display_get_event_loop(server->wl_display), capture_timer_fire, server);
	if (capture.timer) wl_event_source_timer_update(capture.timer, CAPTURE_TIMEOUT_MS);
	shady_render_schedule_all_outputs(server);
	return true;
}

/* The part of the buffer to keep, in buffer pixels, and the image size. */
struct crop {
	int x, y, width, height;
	int out_width, out_height;
};

static bool write_png(struct wlr_renderer *renderer, struct wlr_buffer *buffer,
		const char *path, const struct crop *crop) {
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
		cairo_surface_t *out = image;
		bool whole = crop->x == 0 && crop->y == 0 && crop->width == width &&
			crop->height == height && crop->out_width == width &&
			crop->out_height == height;
		if (!whole && cairo_surface_status(image) == CAIRO_STATUS_SUCCESS) {
			out = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
				crop->out_width, crop->out_height);
			cairo_t *cr = cairo_create(out);
			cairo_scale(cr, (double)crop->out_width / crop->width,
				(double)crop->out_height / crop->height);
			cairo_set_source_surface(cr, image, -crop->x, -crop->y);
			cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
			cairo_paint(cr);
			cairo_destroy(cr);
		}
		/* Write beside the target and rename, so nothing ever sees half a file. */
		char partial[4200];
		snprintf(partial, sizeof(partial), "%s.part", path);
		ok = cairo_surface_status(out) == CAIRO_STATUS_SUCCESS &&
			cairo_surface_write_to_png(out, partial) == CAIRO_STATUS_SUCCESS &&
			rename(partial, path) == 0;
		if (!ok) remove(partial);
		if (out != image) cairo_surface_destroy(out);
		cairo_surface_destroy(image);
	}
	free(pixels);
	return ok;
}

static int clampi(int v, int lo, int hi) {
	return v < lo ? lo : v > hi ? hi : v;
}

void shady_capture_output_buffer(struct shady_server *server,
		struct wlr_output *output, struct wlr_buffer *buffer) {
	if (!capture.armed || !buffer || !server->renderer || !output) return;
	/* Another output's frame: keep waiting for the one asked for. */
	if (capture.output[0] && strcmp(capture.output, output->name) != 0) return;
	capture.armed = false;
	capture.pending = false;
	if (capture.timer) wl_event_source_remove(capture.timer);
	capture.timer = NULL;

	struct wlr_box box = {0};
	wlr_output_layout_get_box(server->output_layout, output, &box);
	int buffer_w = buffer->width, buffer_h = buffer->height;
	/* Buffer pixels per layout unit; a rotated output is kept whole, as drawn. */
	bool normal = output->transform == WL_OUTPUT_TRANSFORM_NORMAL &&
		box.width > 0 && box.height > 0;
	double sx = normal ? (double)buffer_w / box.width : 1.0;
	double sy = normal ? (double)buffer_h / box.height : 1.0;
	struct crop crop = { 0, 0, buffer_w, buffer_h, buffer_w, buffer_h };
	if (capture.has_region && normal) {
		int x0 = clampi((int)floor((capture.x - box.x) * sx), 0, buffer_w);
		int y0 = clampi((int)floor((capture.y - box.y) * sy), 0, buffer_h);
		int x1 = clampi((int)ceil((capture.x + capture.width - box.x) * sx), 0, buffer_w);
		int y1 = clampi((int)ceil((capture.y + capture.height - box.y) * sy), 0, buffer_h);
		crop.x = x0;
		crop.y = y0;
		crop.width = x1 - x0;
		crop.height = y1 - y0;
	}
	double fit = 1.0;
	if (capture.max_width > 0 && crop.width > capture.max_width)
		fit = (double)capture.max_width / crop.width;
	if (capture.max_height > 0 && crop.height * fit > capture.max_height)
		fit = (double)capture.max_height / crop.height;
	crop.out_width = (int)lround(crop.width * fit);
	crop.out_height = (int)lround(crop.height * fit);
	if (crop.out_width < 1) crop.out_width = 1;
	if (crop.out_height < 1) crop.out_height = 1;

	bool ok = crop.width > 0 && crop.height > 0 &&
		write_png(server->renderer, buffer, capture.path, &crop);
	if (ok) {
		wlr_log(WLR_INFO, "screenshot saved to %s", capture.path);
		/* Shells show it; the value changes even for the same second. */
		shady_values_set(server, (void *)&capture_owner, "screenshot.saved", capture.path);
	} else {
		wlr_log(WLR_ERROR, "screenshot to %s failed: %s", capture.path, strerror(errno));
		shady_values_set(server, (void *)&capture_owner, "screenshot.failed", capture.path);
	}

	void (*done)(const struct shady_capture_result *, void *) = capture.done;
	void *data = capture.data;
	capture.done = NULL;
	capture.data = NULL;
	if (!done) return;
	struct shady_capture_result result = {
		.ok = ok,
		.path = capture.path,
		.output = output->name,
		.width = crop.out_width,
		.height = crop.out_height,
		.x = box.x + crop.x / sx,
		.y = box.y + crop.y / sy,
		.logical_width = crop.width / sx,
		.logical_height = crop.height / sy,
		.scale = crop.width > 0 ? crop.out_width / (crop.width / sx) : 1.0,
	};
	done(&result, data);
}

void shady_capture_forget(void *data) {
	if (capture.data != data) return;
	capture.done = NULL;
	capture.data = NULL;
}

bool shady_capture_scene_output_commit(struct shady_server *server,
		struct wlr_scene_output *scene_output) {
	if (!capture.armed || (capture.output[0] &&
			strcmp(capture.output, scene_output->output->name) != 0))
		return wlr_scene_output_commit(scene_output, NULL);
	/* Redraw all of it: an undamaged output would not render a buffer. */
	wlr_damage_ring_add_whole(&scene_output->damage_ring);
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	bool ok = wlr_scene_output_build_state(scene_output, &state, NULL);
	if (ok && (state.committed & WLR_OUTPUT_STATE_BUFFER))
		shady_capture_output_buffer(server, scene_output->output, state.buffer);
	ok = ok && wlr_output_commit_state(scene_output->output, &state);
	wlr_output_state_finish(&state);
	return ok;
}

void shady_capture_finish(void) {
	if (capture.timer) wl_event_source_remove(capture.timer);
	capture.timer = NULL;
	capture.pending = capture.armed = false;
	capture.done = NULL;
	capture.data = NULL;
}
