#ifndef SHADY_MODULE_SPATIAL_STATE_H
#define SHADY_MODULE_SPATIAL_STATE_H

#include "../../module/module.h"
#include "../../experimental/state.h"

struct shady_server;

struct shady_spatial_state {
	struct shady_experimental_state runtime;
};

static inline struct shady_spatial_state *shady_spatial_state(struct shady_server *server) {
	return shady_module_state(server, "spatial");
}

static inline const struct shady_spatial_state *shady_spatial_state_const(
		const struct shady_server *server) {
	return shady_module_state((struct shady_server *)server, "spatial");
}

static inline struct shady_toplevel_experimental_state *shady_spatial_toplevel_state(
		struct shady_toplevel *toplevel) {
	return shady_toplevel_module_state(toplevel, "spatial");
}

static inline const struct shady_toplevel_experimental_state *shady_spatial_toplevel_state_const(
		const struct shady_toplevel *toplevel) {
	return shady_toplevel_module_state_const(toplevel, "spatial");
}

#endif
