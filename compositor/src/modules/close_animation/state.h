#ifndef SHADY_MODULE_CLOSE_ANIMATION_STATE_H
#define SHADY_MODULE_CLOSE_ANIMATION_STATE_H

#include "../../module/module.h"

struct shady_toplevel;

enum shady_close_state {
	SHADY_CLOSE_IDLE,
	SHADY_CLOSE_CRUMPLING,
	SHADY_CLOSE_WAITING,
	SHADY_CLOSE_RESTORING,
	SHADY_CLOSE_ARMED,
};

struct shady_close_animation_state {
	enum shady_close_state state;
	float progress;
	float wait_time;
	uint32_t style;
	float duration;
	float strength;
	float direction_x;
	float direction_y;
};

static inline struct shady_close_animation_state *shady_close_state_for(
		struct shady_toplevel *toplevel) {
	return shady_toplevel_module_state(toplevel, "close-animation");
}

static inline const struct shady_close_animation_state *shady_close_state_for_const(
		const struct shady_toplevel *toplevel) {
	static const struct shady_close_animation_state zero = {0};
	const struct shady_close_animation_state *state =
		shady_toplevel_module_state_const(toplevel, "close-animation");
	return state ? state : &zero;
}

#endif
