#include "runtime.h"

#include <time.h>
#include <wayland-server-core.h>

#include "../shady.h"
#include "../modules/fps/fps.h"
#include "../modules/physics/physics.h"
#include "../modules/window_motion/window_motion.h"
#include "../modules/close_animation/close_animation.h"

bool shady_experimental_update(struct shady_server *server,
		float logical_w, float logical_h) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	if (!server->experimental.clock_ready) {
		server->experimental.last_tick = now;
		server->experimental.clock_ready = true;
		return false;
	}

	float dt =
		(float)(now.tv_sec - server->experimental.last_tick.tv_sec) +
		(float)(now.tv_nsec - server->experimental.last_tick.tv_nsec) /
			1000000000.0f;

	/*
	 * Multiple outputs may request frames back-to-back. Treat those as the
	 * same compositor tick rather than advancing physics once per output.
	 */
	if (dt < 0.0005f) {
		return false;
	}

	server->experimental.last_tick = now;
	if (dt > 0.033f) {
		dt = 0.033f;
	}

	shady_fps_update(server, dt);
	shady_physics_update(server, dt, logical_w, logical_h);

	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		shady_window_motion_update_toplevel(server, toplevel, dt);
		shady_close_animation_update_toplevel(toplevel, dt);
	}

	shady_fps_update_held_window(server, logical_w, logical_h);
	return true;
}
