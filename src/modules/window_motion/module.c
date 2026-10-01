#include "../../module/module.h"
#include "../../shady.h"
#include "window_motion.h"

static void motion_tick(struct shady_server *server, float dt,
		float logical_w, float logical_h) {
	(void)logical_w; (void)logical_h;
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		shady_window_motion_update_toplevel(server, toplevel, dt);
	}
}

static void motion_moved(struct shady_toplevel *toplevel, double x, double y) {
	shady_window_motion_drag(toplevel->server, toplevel, x, y);
}

static const char *const provides[] = { "spatial.window-motion", NULL };
static const char *const requires[] = { "spatial.window-state", NULL };

static const struct shady_module module = {
	.name = "window-motion",
	.provides = provides,
	.requires = requires,
	.toplevel_moved = motion_moved,
	.tick = motion_tick,
};

const struct shady_module *shady_window_motion_module(void) { return &module; }
