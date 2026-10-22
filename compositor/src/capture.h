#ifndef SHADY_CAPTURE_H
#define SHADY_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>

struct shady_server;
struct wlr_buffer;
struct wlr_scene_output;

/* Capture the next frame drawn, after `delay_ms`, to `path` (NULL: a
 * timestamped file in ~/Pictures). The chosen path is copied to out_path.
 * False while another capture is pending. The result is published as the
 * value screenshot.saved (or screenshot.failed) holding the path. */
bool shady_capture_request(struct shady_server *server, const char *path,
	int delay_ms, char *out_path, size_t out_size);
/* Renderers call this with each frame's buffer before committing it. */
void shady_capture_output_buffer(struct shady_server *server, struct wlr_buffer *buffer);
/* wlr_scene_output_commit() that also serves a pending capture. */
bool shady_capture_scene_output_commit(struct shady_server *server,
	struct wlr_scene_output *scene_output);
void shady_capture_finish(void);

#endif
