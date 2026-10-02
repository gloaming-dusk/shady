#ifndef SHADY_MODULE_FPS_STATE_H
#define SHADY_MODULE_FPS_STATE_H

#include <stdbool.h>
#include "../../module/module.h"

struct shady_server;
struct shady_toplevel;

struct shady_fps_state {
	bool forward, back, left, right;
	bool jump_queued;
	struct shady_toplevel *held_toplevel;
	float hold_distance;
	float grab_offset_x, grab_offset_y, grab_offset_z;
	float grab_local_x, grab_local_y, grab_local_z;
	bool input_capture;
	struct shady_toplevel *expanded_toplevel;
	bool orbit_saved;
	float orbit_yaw, orbit_pitch, orbit_distance;
	float orbit_target_x, orbit_target_y, orbit_target_z;
};

struct shady_fps_toplevel_state {
	bool expanded;
};

static inline struct shady_fps_state *shady_fps_state_for(
		struct shady_server *server) {
	return shady_module_state(server, "fps");
}

static inline const struct shady_fps_state *shady_fps_state_for_const(
		const struct shady_server *server) {
	static const struct shady_fps_state zero = {0};
	const struct shady_fps_state *state =
		shady_module_state((struct shady_server *)server, "fps");
	return state ? state : &zero;
}

static inline struct shady_fps_toplevel_state *shady_fps_toplevel_state(
		struct shady_toplevel *toplevel) {
	return shady_toplevel_module_state(toplevel, "fps");
}

static inline const struct shady_fps_toplevel_state *shady_fps_toplevel_state_const(
		const struct shady_toplevel *toplevel) {
	static const struct shady_fps_toplevel_state zero = {0};
	const struct shady_fps_toplevel_state *state =
		shady_toplevel_module_state_const(toplevel, "fps");
	return state ? state : &zero;
}

#endif
