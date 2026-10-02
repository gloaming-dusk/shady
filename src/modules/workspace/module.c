#include "../../module/module.h"
#include "../../shady.h"
#include <stdio.h>
#include <wlr/types/wlr_scene.h>
#include "state.h"
#include "workspace.h"

static bool workspace_init(struct shady_server *server) {
	struct shady_workspace_state *state = shady_workspace_state_for(server);
	state->server = server;
	state->count = 1;
	state->current = 0;
	snprintf(state->names[0], SHADY_WORKSPACE_NAME_MAX, "main");
	return true;
}

static void workspace_toplevel_map(struct shady_toplevel *toplevel) {
	struct shady_workspace_state *state =
		shady_workspace_state_for(toplevel->server);
	struct shady_workspace_toplevel_state *window =
		shady_workspace_toplevel_state_for(toplevel);
	if (!state || !window) return;
	if (!window->assigned) {
		window->workspace = state->current;
		window->assigned = true;
	}
	wlr_scene_node_set_enabled(&toplevel->scene_tree->node,
		window->workspace == state->current);
}

static const char *const workspace_provides[] = {
	"desktop.workspace",
	NULL,
};

static const struct shady_module workspace_module = {
	.name = "workspace",
	.provides = workspace_provides,
	.state_size = sizeof(struct shady_workspace_state),
	.toplevel_state_size = sizeof(struct shady_workspace_toplevel_state),
	.init = workspace_init,
	.toplevel_map = workspace_toplevel_map,
};

const struct shady_module *shady_workspace_module(void) {
	return &workspace_module;
}
