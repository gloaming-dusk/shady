#ifndef SHADY_EXPERIMENTAL_STATE_H
#define SHADY_EXPERIMENTAL_STATE_H

#include <stdbool.h>
#include <time.h>

#include "../render/math3d.h"
#include "../world/world.h"
#include "../modules/fps/state.h"
#include "../modules/physics/state.h"
#include "../modules/window_motion/state.h"
#include "../modules/close_animation/state.h"

/*
 * State owned by Shady's experimental spatial desktop.
 *
 * Keeping this behind one field is deliberate: the compositor core should
 * not need to know which experimental subsystems exist internally.
 */
struct shady_experimental_state {
	struct shady_camera camera;
	struct shady_world world;
	struct shady_fps_state fps;
	struct shady_physics_state physics;
	bool debug_ray;

	/* One simulation clock per compositor, never one clock per output. */
	struct timespec last_tick;
	bool clock_ready;
};

struct shady_toplevel_experimental_state {
	/* Spatial depth in the 3D world. */
	float z;

	/* FPS presentation state. */
	bool fps_expanded;

	struct shady_window_motion_state motion;
	struct shady_window_physics_state physics;
	struct shady_close_animation_state close;
};

#endif
