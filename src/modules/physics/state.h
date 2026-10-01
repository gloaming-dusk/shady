#ifndef SHADY_MODULE_PHYSICS_STATE_H
#define SHADY_MODULE_PHYSICS_STATE_H

#include <stdbool.h>
#include "../../module/module.h"

struct shady_server;
struct shady_toplevel;

struct shady_physics_state {
	bool gravity_enabled;
};

struct shady_window_physics_state {
	float vx, vy, vz;
};

static inline struct shady_physics_state *shady_physics_state_for(
		struct shady_server *server) {
	return shady_module_state(server, "physics");
}

static inline struct shady_window_physics_state *shady_physics_toplevel_state(
		struct shady_toplevel *toplevel) {
	return shady_toplevel_module_state(toplevel, "physics");
}

static inline const struct shady_window_physics_state *
shady_physics_toplevel_state_const(const struct shady_toplevel *toplevel) {
	return shady_toplevel_module_state_const(toplevel, "physics");
}

#endif
