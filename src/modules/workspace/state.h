#ifndef SHADY_WORKSPACE_STATE_H
#define SHADY_WORKSPACE_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "../../module/module.h"

#define SHADY_MAX_WORKSPACES 16
#define SHADY_WORKSPACE_NAME_MAX 64

struct shady_workspace_state {
	struct shady_server *server;
	char names[SHADY_MAX_WORKSPACES][SHADY_WORKSPACE_NAME_MAX];
	size_t count;
	size_t current;
};

struct shady_workspace_toplevel_state {
	size_t workspace;
	bool assigned;
};

static inline struct shady_workspace_state *shady_workspace_state_for(
		struct shady_server *server) {
	return shady_module_state(server, "workspace");
}

static inline struct shady_workspace_toplevel_state *shady_workspace_toplevel_state_for(
		struct shady_toplevel *toplevel) {
	return shady_toplevel_module_state(toplevel, "workspace");
}

#endif
