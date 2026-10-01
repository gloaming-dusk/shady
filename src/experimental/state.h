#ifndef SHADY_EXPERIMENTAL_STATE_H
#define SHADY_EXPERIMENTAL_STATE_H

#include <stdbool.h>
#include <time.h>

#include "../render/math3d.h"
#include "../world/world.h"

/* Shared state owned only by the spatial foundation module. */
struct shady_experimental_state {
	struct shady_camera camera;
	struct shady_world world;
	bool debug_ray;

	/* One simulation clock per compositor, never one clock per output. */
	struct timespec last_tick;
	bool clock_ready;
};

struct shady_toplevel_experimental_state {
	/* Spatial depth is part of the shared 3D transform. */
	float z;
};

#endif
