#include "runtime.h"

#include <time.h>
#include <wayland-server-core.h>

#include "../shady.h"
#include "../modules/spatial/state.h"
#include "../module/module.h"

bool shady_experimental_update(struct shady_server *server,
		float logical_w, float logical_h) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	if (!shady_spatial_state(server)->runtime.clock_ready) {
		shady_spatial_state(server)->runtime.last_tick = now;
		shady_spatial_state(server)->runtime.clock_ready = true;
		return false;
	}

	float dt =
		(float)(now.tv_sec - shady_spatial_state(server)->runtime.last_tick.tv_sec) +
		(float)(now.tv_nsec - shady_spatial_state(server)->runtime.last_tick.tv_nsec) /
			1000000000.0f;

	/*
	 * Multiple outputs may request frames back-to-back. Treat those as the
	 * same compositor tick rather than advancing physics once per output.
	 */
	if (dt < 0.0005f) {
		return false;
	}

	shady_spatial_state(server)->runtime.last_tick = now;
	if (dt > 0.033f) {
		dt = 0.033f;
	}

	shady_modules_tick(server, dt, logical_w, logical_h);
	return true;
}
