#ifndef SHADY_MODULE_WINDOW_MOTION_STATE_H
#define SHADY_MODULE_WINDOW_MOTION_STATE_H

#include <stdbool.h>
#include "../../module/module.h"

struct shady_toplevel;

struct shady_window_motion_state {
	float wobble_x, wobble_y;
	float wobble_vx, wobble_vy;
	float tilt_x, tilt_y;
	float tilt_vx, tilt_vy;
	double last_move_x, last_move_y;
	bool wobble_dragging;
};

static inline struct shady_window_motion_state *shady_window_motion_state_for(
		struct shady_toplevel *toplevel) {
	return shady_toplevel_module_state(toplevel, "window-motion");
}

static inline const struct shady_window_motion_state *
shady_window_motion_state_for_const(const struct shady_toplevel *toplevel) {
	static const struct shady_window_motion_state zero = {0};
	const struct shady_window_motion_state *state =
		shady_toplevel_module_state_const(toplevel, "window-motion");
	return state ? state : &zero;
}

#endif
