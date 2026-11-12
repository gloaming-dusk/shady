#ifndef SHADY_CAPTURE_H
#define SHADY_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>

struct shady_server;
struct wlr_buffer;
struct wlr_output;
struct wlr_scene_output;

/* Capture the next frame drawn, after `delay_ms`, to `path` (NULL: a
 * timestamped file in ~/Pictures). The chosen path is copied to out_path.
 * False while another capture is pending. The result is published as the
 * value screenshot.saved (or screenshot.failed) holding the path. */
bool shady_capture_request(struct shady_server *server, const char *path,
	int delay_ms, char *out_path, size_t out_size);

/* What a capture holds, and how it maps back to layout coordinates: the
 * image pixel (px, py) is the layout point (x + px / scale, y + py / scale). */
struct shady_capture_result {
	bool ok;
	const char *path;
	const char *output;
	int width, height;              /* image pixels */
	double x, y;                    /* layout point at the image's top-left */
	double logical_width, logical_height;
	double scale;                   /* image pixels per layout unit */
};

struct shady_capture_options {
	const char *path;   /* NULL: a timestamped file in ~/Pictures */
	int delay_ms;
	const char *output; /* NULL: the output under `region`, else the first to draw */
	bool has_region;    /* only this part, in layout coordinates */
	double x, y, width, height;
	int max_width, max_height; /* 0: any; larger images are scaled down to fit */
	void (*done)(const struct shady_capture_result *result, void *data);
	void *data;
};

/* As shady_capture_request, with options. False with *error set when it
 * cannot start. `done` runs once the file is written, or failed. */
bool shady_capture_request_with(struct shady_server *server,
	const struct shady_capture_options *options, char *out_path, size_t out_size,
	const char **error);
/* The pending capture no longer calls back with `data`. */
void shady_capture_forget(void *data);
/* Renderers call this with each frame's buffer before committing it. */
void shady_capture_output_buffer(struct shady_server *server,
	struct wlr_output *output, struct wlr_buffer *buffer);
/* wlr_scene_output_commit() that also serves a pending capture. */
bool shady_capture_scene_output_commit(struct shady_server *server,
	struct wlr_scene_output *scene_output);
void shady_capture_finish(void);

#endif
