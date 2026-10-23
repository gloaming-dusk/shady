#ifndef SHADY_RENDER_CLOSE_SNAPSHOT_H
#define SHADY_RENDER_CLOSE_SNAPSHOT_H

#include <stdbool.h>
#include <stdint.h>
#include <GLES2/gl2.h>
#include <wayland-server-core.h>
#include <shady/plugin.h>

struct shady_server;
struct shady_toplevel;

/*
 * The last picture of a window whose close was cancelled once (ARMED): when
 * the client really goes, the renderer keeps drawing it while the close
 * animation plays it out. The renderer owns it and the close-animation
 * module advances it, so both use this one definition.
 */
struct shady_close_snapshot {
	struct wl_list link;

	struct shady_toplevel *toplevel;
	struct shady_server *server;

	GLuint texture;

	int texture_width;
	int texture_height;
	float uv_rect[4]; /* the window's part of the texture */

	float x;
	float y;
	float width;
	float height;
	float tilt_x;
	float tilt_y;
	float z;
	float wobble_x;
	float wobble_y;

	bool has_alpha;

	bool dirty;
	bool animating;

	float progress;
	uint32_t close_style;
	float close_duration;
	float close_strength;
	float close_direction_x;
	float close_direction_y;
	shady_shader_program plugin_shader_program;
	void *plugin_shader_owner;
	float plugin_shader_params[SHADY_WINDOW_SHADER_PARAMS * 4];
};

#endif
